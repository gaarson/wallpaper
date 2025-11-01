#include "src/renderer/core.h"
#define _GNU_SOURCE 
#include "config_monitor.h"

#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <libgen.h> 

#include "output.h" 
#include "ipc.h"

#include "timer.h" // <--- ДОБАВЬ
#include "output.h" // <--- ДОБАВЬ (если еще нет)
extern void update_animation_timer(void); // <--- ДОБАВЬ

#define INOTIFY_EVENT_BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))
#define MAX_COMMAND_LEN 255 

static int inotify_fd = -1;
static int inotify_watch_descriptor = -1;
static char command_file_path[PATH_MAX] = {0};
static char command_dir[PATH_MAX] = {0};
static char command_basename[NAME_MAX + 1] = {0};

static bool is_initial_load = true; 


static char *current_command_arg = NULL; 

static config_change_callback_t change_callback = NULL;
static void *callback_user_data = NULL;

void on_config_changed(const char *new_command, void *user_data) {
    UNUSED(user_data); 

    log_debug("Config Handler: Configuration changed! New Command: '%s'\n",
           new_command ? new_command : "<null>");

    bool redraw_needed = false;
    for (struct client_output *output = outputs_list_head; output; output = output->next) {
        if (output->renderer_state_gl && output->configured) {
            
                if (renderer_core_set_mode(output->renderer_state_gl, new_command)) { 
                    log_debug("  -> O:%u: Config command applied successfully.\n", output->wl_name);
                    redraw_needed = true;
                } else {
                    fprintf(stderr, "Warning: Renderer on O:%u failed to handle config command '%s'\n",
                         output->wl_name, new_command ? new_command : "<null>");
            }
        }
    }
    update_animation_timer(); 
    if (redraw_needed || true) { 
        log_debug("Config Handler: Triggering redraw due to config change.\n");
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint32_t ms = (uint32_t)(((uint64_t)ts.tv_sec * 1000) + ((uint64_t)ts.tv_nsec / 1000000));

        for (struct client_output *output = outputs_list_head; output; output = output->next) {
             if (output->configured && output->renderer_state_gl) {
                 present_output_frame(output, ms);
             }
        }
         
    }
}

static bool read_and_process_command_file(void) {
    if (command_file_path[0] == '\0') {
        
        if (current_command_arg != NULL) {
             log_debug("ConfigMonitor: Path not set, resetting state.\n");
             free(current_command_arg);
             current_command_arg = NULL;
             if (change_callback) {
                 change_callback(NULL, callback_user_data);
             }
        }
        return true; 
    }

    FILE *f = fopen(command_file_path, "r");
    char buffer[MAX_COMMAND_LEN + 2] = {0}; 
    bool file_exists = true;
    char *command_from_file = NULL;

    if (!f) {
        if (errno == ENOENT) {
            file_exists = false;
            
        } else {
            perror("ConfigMonitor: fopen command file failed");
            
            return false;
        }
    }

    if (file_exists) {
        if (fgets(buffer, sizeof(buffer), f)) {
            buffer[strcspn(buffer, "\n\r")] = 0; 
            if (buffer[0] != '\0') {
                command_from_file = buffer;
            }
        } else if (ferror(f)) {
            perror("ConfigMonitor: fgets from command file failed");
            fclose(f);
            return false; 
        }
        
        fclose(f);
    }

    
    
    char *new_command_arg_for_state = NULL;

    if (command_from_file) {
        
        new_command_arg_for_state = command_from_file;
    } else { 
        
        new_command_arg_for_state = NULL;
    }

    
    bool state_changed = false;
    if ((current_command_arg == NULL && new_command_arg_for_state != NULL) ||
        (current_command_arg != NULL && new_command_arg_for_state == NULL) ||
        (current_command_arg != NULL && new_command_arg_for_state != NULL && 
         strcmp(current_command_arg, new_command_arg_for_state) != 0))
    {
        state_changed = true;
    }

    if (state_changed) {
        log_debug("ConfigMonitor: State change detected. New Command: '%s'\n",
               new_command_arg_for_state ? new_command_arg_for_state : "(null)");

        
        free(current_command_arg);
        current_command_arg = NULL;

        if (new_command_arg_for_state != NULL) {
            current_command_arg = strdup(new_command_arg_for_state);
            if (!current_command_arg) {
                perror("ConfigMonitor strdup");
                if (change_callback && !is_initial_load) {
                    change_callback(NULL, callback_user_data); 
                }
                return false; 
            }
        }

        
        if (change_callback) {
            if (is_initial_load) {
                log_debug("ConfigMonitor: Initial state loaded. Deferring callback.\n");
            } else {
                log_debug("ConfigMonitor: State *changed* by event. Calling callback.\n");
                
                change_callback(current_command_arg, callback_user_data);
            }
        }
    }

    return true; 
}




bool config_monitor_init(config_change_callback_t callback, void *user_data) {
    if (!callback) {
        fprintf(stderr, "ConfigMonitor Error: Callback function cannot be NULL.\n");
        return false;
    }

    
    config_monitor_cleanup();
    change_callback = callback;
    callback_user_data = user_data;
    is_initial_load = true; 

    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    const char *dir_to_watch = NULL;

    if (runtime_dir) {
        dir_to_watch = runtime_dir;
    } else {
        fprintf(stderr, "ConfigMonitor Warning: XDG_RUNTIME_DIR not set, using /tmp for command file.\n");
        dir_to_watch = "/tmp";
    }

    int written = snprintf(command_file_path, sizeof(command_file_path), "%s/%s", dir_to_watch, COMMAND_FILENAME);
    if (written < 0 || (size_t)written >= sizeof(command_file_path)) {
        fprintf(stderr, "ConfigMonitor Error: Failed to format command file path or path too long.\n");
        command_file_path[0] = '\0';
        errno = ENAMETOOLONG;
        return false;
    }

    
    strncpy(command_dir, dir_to_watch, sizeof(command_dir) - 1);
    command_dir[sizeof(command_dir) - 1] = '\0';
    strncpy(command_basename, COMMAND_FILENAME, sizeof(command_basename) - 1);
    command_basename[sizeof(command_basename) - 1] = '\0';

    inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd < 0) {
        perror("ConfigMonitor: inotify_init1 failed");
        return false;
    }

    uint32_t watch_mask = IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE | IN_MOVED_FROM | IN_CREATE;
    inotify_watch_descriptor = inotify_add_watch(inotify_fd, command_dir, watch_mask);

    if (inotify_watch_descriptor < 0) {
        perror("ConfigMonitor: inotify_add_watch failed");
        fprintf(stderr, "  (Could not watch directory: %s)\n", command_dir);
        close(inotify_fd); inotify_fd = -1;
        return false;
    }

    log_debug("ConfigMonitor: Watching directory '%s' for events related to '%s' (fd: %d, wd: %d)\n",
           command_dir, command_basename, inotify_fd, inotify_watch_descriptor);

    
    if (!read_and_process_command_file()) {
         fprintf(stderr, "ConfigMonitor: Failed to process initial command file state.\n");
         
         
    }

    is_initial_load = false;

    return true;
}

int config_monitor_get_fd(void) {
    return inotify_fd;
}

void config_monitor_handle_event(void) {
    if (inotify_fd < 0) return; 

    char buffer[INOTIFY_EVENT_BUF_LEN] __attribute__ ((aligned(__alignof__(struct inotify_event))));
    ssize_t len;

    
    while ((len = read(inotify_fd, buffer, sizeof(buffer))) > 0) {
        const struct inotify_event *event;
        bool relevant_event_found = false;

        for (char *ptr = buffer; ptr < buffer + len; ) {
            
            if (ptr + sizeof(struct inotify_event) > buffer + len) {
                fprintf(stderr, "ConfigMonitor: Partial inotify event received (header).\n");
                break; 
            }
            event = (const struct inotify_event *) ptr;

            
            if (event->len > 0 && (ptr + sizeof(struct inotify_event) + event->len > buffer + len)) {
                fprintf(stderr, "ConfigMonitor: Partial inotify event received (name).\n");
                break; 
            }

            
            if (event->wd == inotify_watch_descriptor && event->len > 0 && strcmp(event->name, command_basename) == 0) {
                
                if (event->mask & (IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE | IN_MOVED_FROM)) {
                     log_debug("ConfigMonitor: Relevant event (mask 0x%x) for '%s' detected.\n", event->mask, command_basename);
                    relevant_event_found = true;
                    
                    
                    break;
                }
            }
             
             ptr += sizeof(struct inotify_event) + event->len;
        }

        if (relevant_event_found) {
            
            read_and_process_command_file();
            
            
            
            
             break;
        }
    } 

    
    if (len < 0 && errno != EAGAIN) {
        perror("ConfigMonitor: read from inotify_fd failed");
        
        fprintf(stderr, "ConfigMonitor: Disabling file watching due to read error.\n");
        config_monitor_cleanup(); 
         
         if (change_callback) {
             
             if (current_command_arg != NULL) {
                 free(current_command_arg);
                 current_command_arg = NULL;
                 change_callback(NULL, callback_user_data);
             }
         }
    }
}

const char* config_monitor_get_current_command(void) {
    return current_command_arg; 
}

void config_monitor_cleanup(void) {
    if (inotify_fd >= 0) {
        log_debug("ConfigMonitor: Cleaning up...\n");
        if (inotify_watch_descriptor >= 0) {
            inotify_rm_watch(inotify_fd, inotify_watch_descriptor);
            inotify_watch_descriptor = -1;
        }
        close(inotify_fd);
        inotify_fd = -1;
    }
    free(current_command_arg);
    current_command_arg = NULL;
    
    command_file_path[0] = '\0';
    command_dir[0] = '\0';
    command_basename[0] = '\0';
    change_callback = NULL;
    callback_user_data = NULL;
}
