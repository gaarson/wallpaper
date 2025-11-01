#ifndef IPC_H
#define IPC_H

#include <stddef.h> 
#include <stdbool.h> 

#define IPC_MAX_COMMAND_LEN 1024

typedef enum {
    IPC_READ_RESULT_SUCCESS,
    IPC_READ_RESULT_WOULD_BLOCK,
    IPC_READ_RESULT_EOF,
    IPC_READ_RESULT_ERROR,
    IPC_READ_RESULT_TOO_LONG,
    IPC_READ_RESULT_NO_HANDLER,
    IPC_READ_RESULT_INVALID_FD,
    IPC_READ_RESULT_BUFFER_TOO_SMALL
} ipc_read_result_t;

typedef void (*ipc_command_handler_t)(int, const char *, size_t);
void process_ipc_command(int client_fd, const char *command, size_t command_len);
void handle_animation_modifier(const char *modifier_command);

bool ipc_setup(const char *socket_name, ipc_command_handler_t handler);

void ipc_cleanup(void);

int ipc_get_listener_fd(void);

int ipc_accept_client(void);

ipc_read_result_t ipc_read_command(int client_fd, char *buffer, size_t buffer_size);

void ipc_close_client(int client_fd);
void ipc_close_all_clients();


int ipc_get_client_count(void);


int ipc_get_active_clients(int *dest_fds, int max_dest_size);

#endif 
