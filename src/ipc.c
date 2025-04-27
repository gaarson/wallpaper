#define _GNU_SOURCE
#include "ipc.h"

#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stddef.h>

#define MAX_COMMAND_LEN 1024

#define MAX_CLIENTS 5
#define SOCKET_PATH_TEMPLATE "%s/%s"

static int listener_fd = -1;
static int client_fds[MAX_CLIENTS];
static int n_clients = 0;
static char socket_path[PATH_MAX] = {0};
static ipc_command_handler_t global_command_handler = NULL;

static int find_client_slot(int client_fd) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (client_fds[i] == client_fd) {
            return i;
        }
    }
    return -1;
}

bool ipc_setup(const char *socket_name, ipc_command_handler_t handler) {
    if (listener_fd != -1) {
        fprintf(stderr, "IPC already set up.\n");
        return true;
    }
    if (!socket_name || socket_name[0] == '\0') {
        fprintf(stderr, "Invalid socket name provided for IPC setup.\n");
        return false;
    }
    if (!handler) {
         fprintf(stderr, "IPC setup requires a valid command handler.\n");
         return false;
    }

    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (!runtime_dir) {
        fprintf(stderr, "XDG_RUNTIME_DIR not set. Cannot create IPC socket securely.\n");
        return false;
    }

    int written = snprintf(socket_path, sizeof(socket_path), SOCKET_PATH_TEMPLATE, runtime_dir, socket_name);
    if (written < 0 || (size_t)written >= sizeof(socket_path)) {
        fprintf(stderr, "Failed to format socket path or path too long.\n");
        socket_path[0] = '\0';
        return false;
    }

    listener_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listener_fd == -1) {
        perror("IPC: Failed to create listener socket");
        socket_path[0] = '\0';
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    size_t path_len = strlen(socket_path);

    if (path_len >= sizeof(addr.sun_path)) {
         fprintf(stderr, "IPC: Socket path '%s' is too long for sockaddr_un (max %zu)\n",
                 socket_path, sizeof(addr.sun_path) -1);
         close(listener_fd); listener_fd = -1;
         socket_path[0] = '\0';
         return false;
    }
    strncpy(addr.sun_path, socket_path, path_len);
    addr.sun_path[path_len] = '\0';

    socklen_t addr_len = offsetof(struct sockaddr_un, sun_path) + path_len + 1;

    if (unlink(socket_path) == -1 && errno != ENOENT) {
         perror("IPC Warning: Failed to unlink old socket file, bind might fail");
    }

    if (bind(listener_fd, (struct sockaddr*)&addr, addr_len) == -1) {
        perror("IPC: Failed to bind listener socket");
        close(listener_fd); listener_fd = -1;
        unlink(socket_path);
        socket_path[0] = '\0';
        return false;
    }

    if (listen(listener_fd, MAX_CLIENTS) == -1) {
        perror("IPC: Failed to listen on socket");
        close(listener_fd); listener_fd = -1;
        unlink(socket_path);
        socket_path[0] = '\0';
        return false;
    }

    global_command_handler = handler;

    printf("IPC: Listener socket listening on %s (fd: %d)\n", socket_path, listener_fd);

    for (int i = 0; i < MAX_CLIENTS; ++i) client_fds[i] = -1;
    n_clients = 0;
    return true;
}

void ipc_cleanup(void) {
    if (listener_fd >= 0) {
        close(listener_fd);
        listener_fd = -1;
        printf("IPC: Closed Listener FD.\n");
    }
    if (socket_path[0] != '\0') {
        if (unlink(socket_path) == 0) {
              printf("IPC: Unlinked socket file: %s\n", socket_path);
        } else if (errno != ENOENT) {
            perror("IPC Warning: Failed to unlink socket file during cleanup");
        }
        socket_path[0] = '\0';
    }

    ipc_close_all_clients();

    printf("IPC: Cleanup finished.\n");
}


int ipc_get_listener_fd(void) {
    return listener_fd;
}

int ipc_accept_client(void) {
    if (listener_fd < 0) return -1;

    int client_fd = accept4(listener_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client_fd < 0) {
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("IPC: accept4 failed");
        }
        return -1;
    }

    int client_slot = -1;
    for(int i = 0; i < MAX_CLIENTS; ++i) {
        if (client_fds[i] == -1) {
            client_slot = i;
            break;
        }
    }

    if (client_slot != -1) {
        printf("IPC: Accepted client fd %d -> slot %d\n", client_fd, client_slot);
        client_fds[client_slot] = client_fd;
        n_clients++;
        return client_fd;
    } else {
        fprintf(stderr, "IPC: Max clients (%d) reached, rejecting fd %d\n", MAX_CLIENTS, client_fd);
        close(client_fd);
        return -1;
    }
}

ipc_read_result_t ipc_read_command(int client_fd, char *buffer, size_t buffer_size) {
    if (!buffer || buffer_size <= MAX_COMMAND_LEN) {
         fprintf(stderr, "IPC: Buffer is NULL or too small for MAX_COMMAND_LEN (%d required, %zu provided).\n",
                 MAX_COMMAND_LEN + 1, buffer_size);
         return IPC_READ_RESULT_BUFFER_TOO_SMALL;
    }

    int slot = find_client_slot(client_fd);
    if (slot == -1) {
        fprintf(stderr, "IPC: Attempt to read from unknown or closed client fd %d\n", client_fd);
        return IPC_READ_RESULT_INVALID_FD;
    }

    ssize_t n_read = read(client_fd, buffer, buffer_size - 1);

    if (n_read > 0) {
        buffer[n_read] = '\0';

        char *cmd_end_ptr = buffer + n_read;
        char *current_cmd = buffer;

        while (current_cmd < cmd_end_ptr) {
            char *cmd_terminator;
            bool found_newline = false;

            char* newline_ptr = strchr(current_cmd, '\n');

            if (newline_ptr != NULL) {
                cmd_terminator = newline_ptr;
                found_newline = true;
                *cmd_terminator = '\0';
            } else {
                cmd_terminator = cmd_end_ptr;
            }

            size_t command_len = cmd_terminator - current_cmd;

            if (command_len > MAX_COMMAND_LEN) {
                 fprintf(stderr, "IPC: Client fd %d (slot %d) sent command exceeding MAX_COMMAND_LEN (%zu > %d). Discarding fragment.\n",
                         client_fd, slot, command_len, MAX_COMMAND_LEN);
                 if (!found_newline) {
                   return IPC_READ_RESULT_TOO_LONG;
                 }

            } else if (command_len > 0) {
                 if (global_command_handler) {
                     global_command_handler(client_fd, current_cmd, command_len);
                 } else {
                     fprintf(stderr, "IPC: Warning - received command but no handler set.\n");
                 }
            }

            if (found_newline) {
                current_cmd = cmd_terminator + 1;
            } else {
                break;
            }
        }

        return IPC_READ_RESULT_SUCCESS;

    } else if (n_read == 0) {
        printf("IPC: Client fd %d (slot %d) disconnected (EOF).\n", client_fd, slot);
        close(client_fd);
        client_fds[slot] = -1;
        n_clients--;
        return IPC_READ_RESULT_EOF;
    } else {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return IPC_READ_RESULT_WOULD_BLOCK;
        } else {
            perror("IPC: read from client failed");
            fprintf(stderr, "IPC: Closing client fd %d (slot %d) due to read error.\n", client_fd, slot);
            close(client_fd);
            client_fds[slot] = -1;
            n_clients--;
            return IPC_READ_RESULT_ERROR;
        }
    }
}

void ipc_close_client(int client_fd) {
     int slot = find_client_slot(client_fd);
     if (slot != -1) {
         printf("IPC: Force closing client fd %d (slot %d)\n", client_fd, slot);
         close(client_fd);
         client_fds[slot] = -1;
         n_clients--;
     } else {
         fprintf(stderr, "IPC: Attempt to close unknown or already closed client fd %d\n", client_fd);
     }
}

void ipc_close_all_clients(void) {
    if (n_clients <= 0) {
        printf("IPC: No active clients to close.\n");
        return;
    }

    printf("IPC: Closing all active client connections (%d)...\n", n_clients);
    int closed_count = 0;
    int initial_clients = n_clients;

    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (client_fds[i] >= 0) {
            printf("IPC: Closing client fd %d (slot %d)\n", client_fds[i], i);
            close(client_fds[i]);
            client_fds[i] = -1;
            closed_count++;
        }
    }

    n_clients = 0;

    if (closed_count != initial_clients) {
         fprintf(stderr, "IPC Warning: Expected to close %d clients, but closed %d. Resetting count to 0.\n", initial_clients, closed_count);
    } else {
        printf("IPC: Closed %d client FDs.\n", closed_count);
    }
}

int ipc_get_client_count(void) {
    return n_clients;
}

int ipc_get_active_clients(int *dest_fds, int max_dest_size) {
    if (!dest_fds || max_dest_size <= 0) return 0;
    int count = 0;
    for (int i = 0; i < MAX_CLIENTS && count < max_dest_size; ++i) {
        if (client_fds[i] != -1) {
            dest_fds[count++] = client_fds[i];
        }
    }
    return count;
}
