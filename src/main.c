#include "common.h"
#include "wayland_setup.h"
#include "output.h"
#include "timer.h"
#include "ipc.h"
#include "config_monitor.h"

static int g_ipc_listener_fd = -1;
static int g_config_monitor_fd = -1;
int g_timer_fd = -1;
const long g_frame_interval_ns = 1000000000L / 60; // 60 FPS

void update_animation_timer(void) {
    if (g_timer_fd < 0) return; // Таймер еще не создан

    bool any_output_is_animated = false;

    // Проверяем *все* выходы
    for (struct client_output *output = outputs_list_head; output; output = output->next) {
        if (output->renderer_state_gl && 
            renderer_core_needs_redraw(output->renderer_state_gl)) 
        {
            // Нашли хотя бы один анимированный
            any_output_is_animated = true;
            break;
        }
    }

    if (any_output_is_animated) {
        printf("GlobalTimer: At least one output is animated. Arming timer (fd: %d).\n", g_timer_fd);
        arm_timer(g_timer_fd, g_frame_interval_ns);
    } else {
        printf("GlobalTimer: No outputs are animated. Disarming timer (fd: %d).\n", g_timer_fd);
        disarm_timer(g_timer_fd);
    }
}

static void cleanup_application(void);

int main(int argc, char **argv) {
    UNUSED(argc); UNUSED(argv); 

    printf("Starting Animated Wallpaper Application...\n");

    printf("Config Monitor initialized (fd: %d)\n", g_config_monitor_fd);

    g_timer_fd = setup_timer(g_frame_interval_ns);
    if (g_timer_fd < 0) {
        fprintf(stderr, "Fatal: Failed to setup animation timer.\n");
        cleanup_application();
        return EXIT_FAILURE;
    }
    disarm_timer(g_timer_fd);

    if (!config_monitor_init(on_config_changed, NULL)) {
        fprintf(stderr, "Fatal: Config monitor initialization failed.\n");
        return EXIT_FAILURE;
    }
    g_config_monitor_fd = config_monitor_get_fd();
    if (g_config_monitor_fd < 0) {
         fprintf(stderr, "Fatal: Config monitor fd is invalid after init.\n");
         return EXIT_FAILURE;
    }
    printf("Animation timer initialized (fd: %d)\n", g_timer_fd);

    if (!init_wayland()) {
        fprintf(stderr, "Fatal: Wayland initialization failed.\n");
        return EXIT_FAILURE;
    }

    if (ipc_setup("wallpaper_control.sock", process_ipc_command)) {
        g_ipc_listener_fd = ipc_get_listener_fd();
        if (g_ipc_listener_fd < 0) {
            fprintf(stderr, "Warning: IPC setup reported success, but listener FD is invalid.\n");
        } else {
            printf("IPC Listener setup on 'wallpaper_control.sock' (fd: %d)\n", g_ipc_listener_fd);
        }
    } else {
        fprintf(stderr, "Warning: IPC setup failed. IPC control will be unavailable.\n");
        g_ipc_listener_fd = -1; 
    }

    int initial_rendered_outputs = 0;
    for(struct client_output *o = outputs_list_head; o; o = o->next) {
        if (o->renderer_state_gl && o->configured) {
             initial_rendered_outputs++;
        }
    }
     if (n_outputs > 0 && initial_rendered_outputs == 0) {
         fprintf(stderr,"Warning: No outputs were fully configured and rendered after initial setup.\n");
     } else if (initial_rendered_outputs > 0) {
         printf("Main: Initial setup complete. %d output(s) configured and rendered.\n", initial_rendered_outputs);
     } else {
          printf("Main: Initial setup complete. No outputs found yet.\n");
     }


    struct pollfd fds_poll[MAX_POLL_FDS];
    int wayland_fd = wl_display_get_fd(g_display);
    if (wayland_fd < 0) {
        fprintf(stderr, "Fatal: Invalid Wayland display fd.\n");
        cleanup_application();
        return EXIT_FAILURE;
    }

    char ipc_read_buffer[IPC_READ_BUFFER_SIZE]; 
    bool running = true;
    bool wayland_read_prepared = false;

    printf("Entering main event loop...\n");
    while (running) {
        
        
        while (wl_display_prepare_read(g_display) != 0) {
            if (wl_display_dispatch_pending(g_display) < 0) {
                 fprintf(stderr, "Error: wl_display_dispatch_pending failed. Closing.\n");
                 running = false;
                 goto cleanup_exit; 
            }
        }

        int flush_ret = wl_display_flush(g_display);
        if (flush_ret < 0 && errno == EPIPE) {
             fprintf(stderr, "Error: Wayland connection lost (EPIPE on flush).\n");
             running = false;
             continue;
        } else if (flush_ret < 0 && errno != EAGAIN) {
             perror("Warning: wl_display_flush failed");
        }

        int current_poll_count = 0;

        fds_poll[current_poll_count].fd = wayland_fd;
        fds_poll[current_poll_count].events = POLLIN;
        fds_poll[current_poll_count].revents = 0;
        current_poll_count++;

        if (g_timer_fd >= 0) {
            if (current_poll_count < MAX_POLL_FDS) {
                fds_poll[current_poll_count].fd = g_timer_fd;
                fds_poll[current_poll_count].events = POLLIN;
                fds_poll[current_poll_count].revents = 0;
                current_poll_count++;
            } else { goto poll_array_full; }
        }

        if (g_config_monitor_fd >= 0) {
            if (current_poll_count < MAX_POLL_FDS) {
                fds_poll[current_poll_count].fd = g_config_monitor_fd;
                fds_poll[current_poll_count].events = POLLIN;
                fds_poll[current_poll_count].revents = 0;
                current_poll_count++;
            } else { goto poll_array_full; }
        }

        if (g_ipc_listener_fd >= 0) {
            if (current_poll_count < MAX_POLL_FDS) {
                fds_poll[current_poll_count].fd = g_ipc_listener_fd;
                fds_poll[current_poll_count].events = POLLIN;
                fds_poll[current_poll_count].revents = 0;
                current_poll_count++;
            } else { goto poll_array_full; }
        }

        int client_fds[MAX_IPC_CLIENTS];
        int num_clients = ipc_get_active_clients(client_fds, MAX_IPC_CLIENTS);
        for (int i = 0; i < num_clients; ++i) {
            if (current_poll_count < MAX_POLL_FDS) {
                fds_poll[current_poll_count].fd = client_fds[i];
                fds_poll[current_poll_count].events = POLLIN;
                fds_poll[current_poll_count].revents = 0;
                current_poll_count++;
            } else {
                fprintf(stderr,"Warning: Poll array full, cannot add IPC client fd %d.\n", client_fds[i]);
                break;
            }
        }
        
        for (int i = current_poll_count; i < MAX_POLL_FDS; ++i) {
            fds_poll[i].fd = -1;
        }

        int poll_ret = poll(fds_poll, (nfds_t)current_poll_count, -1); 

        if (poll_ret < 0) {
            if (errno == EINTR) {
                wl_display_cancel_read(g_display);
                continue;
            }
            perror("Fatal: poll failed");
            wl_display_cancel_read(g_display);
            running = false;
            continue;
        }

        if (fds_poll[0].revents & (POLLIN | POLLERR | POLLHUP)) {
            if (wl_display_read_events(g_display) < 0) {
                if (errno == EPIPE) {
                    fprintf(stderr, "Error: Wayland connection lost (EPIPE on read_events).\n");
                } else {
                    perror("Error: wl_display_read_events failed");
                }
                running = false;
                continue; 
            }
        } else {
            wl_display_cancel_read(g_display);
        }

        if (wl_display_dispatch_pending(g_display) < 0) {
            fprintf(stderr, "Error: wl_display_dispatch_pending failed after read. Closing.\n");
            running = false;
            continue;
        }

        if (!running) continue; 

        for (int i = 1; i < current_poll_count; ++i) { 
            if (fds_poll[i].fd == -1) continue; 

            short revents = fds_poll[i].revents;
            int current_fd = fds_poll[i].fd;

            
            if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
                fprintf(stderr, "Error: Poll reported error (0x%x) on fd %d.\n", revents, current_fd);
                if (current_fd == g_ipc_listener_fd) {
                    ipc_cleanup(); 
                    g_ipc_listener_fd = -1;
                } else if (current_fd == g_config_monitor_fd) {
                    config_monitor_cleanup(); g_config_monitor_fd = -1;
                } else {
                    ipc_close_client(current_fd);
                }
                continue; 
            }

            if (revents & POLLIN) {
                if (current_fd == g_config_monitor_fd) {
                    printf("Config monitor event detected.\n");
                    config_monitor_handle_event(); 
                    if (config_monitor_get_fd() < 0) {
                        g_config_monitor_fd = -1;
                    }

                } else if (current_fd == g_timer_fd) {
                    // Прочитать событие таймера, чтобы он "перезарядился"
                    uint64_t expirations;
                    ssize_t n_read = read(g_timer_fd, &expirations, sizeof(expirations));
                    if (n_read < 0 && errno != EAGAIN) {
                         perror("Warning: read from timer_fd failed");
                    }
 
                    // Получить текущее время для анимации
                    struct timespec ts;
                    clock_gettime(CLOCK_MONOTONIC, &ts);
                    uint32_t ms = (uint32_t)(((uint64_t)ts.tv_sec * 1000) + ((uint64_t)ts.tv_nsec / 1000000));
 
                    // Перерисовать все "анимированные" выходы
                    for (struct client_output *output = outputs_list_head; output; output = output->next) {
                        if (output->renderer_state_gl && output->configured &&
                            renderer_core_needs_redraw(output->renderer_state_gl))
                        {
                            present_output_frame(output, ms);
                        }
                    }
 
                } else if (current_fd == g_ipc_listener_fd) {
                    printf("IPC listener event: Accepting new client.\n");
                    int new_client_fd = ipc_accept_client();
                    if (new_client_fd < 0) {
                        if (errno != EAGAIN && errno != EWOULDBLOCK) {
                            perror("Error accepting IPC client");
                        }
                    } else {
                        printf("IPC client accepted (fd: %d)\n", new_client_fd);
                    }

                } else {
                    ipc_read_result_t read_res = ipc_read_command(current_fd, ipc_read_buffer, sizeof(ipc_read_buffer));

                    switch (read_res) {
                        case IPC_READ_RESULT_SUCCESS:
                            break;
                        case IPC_READ_RESULT_WOULD_BLOCK:
                            break;
                        case IPC_READ_RESULT_EOF:
                            break;
                        case IPC_READ_RESULT_ERROR:
                            break;
                        case IPC_READ_RESULT_BUFFER_TOO_SMALL:
                            fprintf(stderr, "Error: IPC command from fd %d exceeded buffer size (%zu).\n", current_fd, sizeof(ipc_read_buffer));
                            ipc_close_client(current_fd);
                            break;
                        case IPC_READ_RESULT_TOO_LONG:
                            fprintf(stderr, "Error: IPC command from fd %d too long (internal logic).\n", current_fd);
                            ipc_close_client(current_fd);
                            break;
                        case IPC_READ_RESULT_NO_HANDLER:
                            fprintf(stderr, "Error: No IPC handler configured, but received data on fd %d.\n", current_fd);
                            ipc_close_client(current_fd);
                            break;
                        case IPC_READ_RESULT_INVALID_FD:
                            fprintf(stderr, "Error: ipc_read_command called with invalid fd %d.\n", current_fd);
                            break;
                        default:
                            fprintf(stderr, "Warning: Unknown ipc_read_result_t value (%d) received for fd %d.\n", read_res, current_fd);
                            ipc_close_client(current_fd);
                            break;
                    }
                }
            } 
        } 
    } 

    printf("Exiting main loop.\n");
    goto cleanup_exit; 

poll_array_full:
    fprintf(stderr, "Fatal: Poll array size (MAX_POLL_FDS=%d) is too small for the number of file descriptors.\n", MAX_POLL_FDS);
    
    if (wayland_read_prepared) { wl_display_cancel_read(g_display); }
    

cleanup_exit:
    cleanup_application();
    printf("Application finished.\n");
    return EXIT_SUCCESS; 
}


static void cleanup_application(void) {
     printf("--- Starting Application Cleanup ---\n");

     ipc_cleanup(); g_ipc_listener_fd = -1;

     config_monitor_cleanup(); g_config_monitor_fd = -1;
     cleanup_timer(g_timer_fd); g_timer_fd = -1;

     cleanup_wayland();

    printf("--- Application Cleanup Finished ---\n");
}
