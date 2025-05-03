#include "common.h"
#include "wayland_setup.h"
#include "output.h"
#include "timer.h"
#include "ipc.h"
#include "config_monitor.h"
// ipc.h, config_monitor.h, renderer.h включены через common.h

// --- Глобальные переменные для main ---
static int g_timer_fd = -1;
static int g_ipc_listener_fd = -1;
static int g_config_monitor_fd = -1;
static long g_timer_interval_nsec = 1000000000L / 60; // ~60 FPS по умолчанию

// --- Прототип функции общей очистки ---
static void cleanup_application(void);

int main(int argc, char **argv) {
    UNUSED(argc); UNUSED(argv); // Пока не используем аргументы командной строки

    printf("Starting Animated Wallpaper Application...\n");

    // 1. Инициализация Wayland
    if (!init_wayland()) {
        fprintf(stderr, "Fatal: Wayland initialization failed.\n");
        return EXIT_FAILURE;
    }
    // Не выходим, если нет выходов - они могут появиться позже

    // 2. Настройка IPC
    // process_ipc_command находится в ipc_handler.c
    if (ipc_setup("wallpaper_control.sock", process_ipc_command)) {
        g_ipc_listener_fd = ipc_get_listener_fd();
        if (g_ipc_listener_fd < 0) {
            fprintf(stderr, "Warning: IPC setup reported success, but listener FD is invalid.\n");
        } else {
            printf("IPC Listener setup on 'wallpaper_control.sock' (fd: %d)\n", g_ipc_listener_fd);
        }
    } else {
        fprintf(stderr, "Warning: IPC setup failed. IPC control will be unavailable.\n");
        g_ipc_listener_fd = -1; // Убедимся, что он -1
    }

    // 3. Настройка таймера
    // setup_timer находится в timer.c
    g_timer_fd = setup_timer(g_timer_interval_nsec);
    if (g_timer_fd < 0) {
        fprintf(stderr, "Fatal: Timer setup failed.\n");
        cleanup_application();
        return EXIT_FAILURE;
    }

    // 4. Настройка мониторинга конфигурации
    // on_config_changed находится в config_handler.c
    if (!config_monitor_init(on_config_changed, NULL)) {
        fprintf(stderr, "Fatal: Config monitor initialization failed.\n");
        cleanup_application();
        return EXIT_FAILURE;
    }
    g_config_monitor_fd = config_monitor_get_fd();
    if (g_config_monitor_fd < 0) {
         fprintf(stderr, "Fatal: Config monitor fd is invalid after init.\n");
         cleanup_application();
         return EXIT_FAILURE;
    }
    printf("Config Monitor initialized (fd: %d)\n", g_config_monitor_fd);

    // Небольшая проверка после инициализации
    // Может быть 0 выходов, но если есть выходы, должен быть хотя бы 1 рендерер после roundtrip'ов
    int initial_rendered_outputs = 0;
    for(struct client_output *o = outputs_list_head; o; o = o->next) {
        if (o->renderer_state_gl && o->configured) {
             initial_rendered_outputs++;
        }
    }
     if (n_outputs > 0 && initial_rendered_outputs == 0) {
         // Это может быть нормально, если configure/scale придут позже
         fprintf(stderr,"Warning: No outputs were fully configured and rendered after initial setup.\n");
     } else if (initial_rendered_outputs > 0) {
         printf("Main: Initial setup complete. %d output(s) configured and rendered.\n", initial_rendered_outputs);
     } else {
          printf("Main: Initial setup complete. No outputs found yet.\n");
     }


    // --- Основной цикл обработки событий ---
    struct pollfd fds_poll[MAX_POLL_FDS];
    int wayland_fd = wl_display_get_fd(g_display);
    if (wayland_fd < 0) {
        fprintf(stderr, "Fatal: Invalid Wayland display fd.\n");
        cleanup_application();
        return EXIT_FAILURE;
    }

    char ipc_read_buffer[IPC_READ_BUFFER_SIZE]; // Буфер для чтения команд IPC
    bool running = true;
    bool wayland_read_prepared = false;

    printf("Entering main event loop...\n");
    while (running) {
        // --- Подготовка к poll ---

        // 1. Сброс буфера Wayland перед ожиданием
        // Это отправляет все запросы (attach, commit, destroy и т.д.) композитору
        int flush_ret = wl_display_flush(g_display);
        if (flush_ret < 0 && errno != EAGAIN && errno != EPIPE) {
            // EAGAIN - буфер переполнен, попробуем позже (poll разбудит)
            // EPIPE - соединение разорвано, выходим
            perror("Fatal: wl_display_flush failed");
            running = false;
            continue;
        } else if (errno == EPIPE) {
             fprintf(stderr, "Error: Wayland connection lost (EPIPE on flush).\n");
             running = false;
             continue;
        }


        // 2. Подготовка к чтению событий Wayland (если нет ожидающих событий)
        // или диспетчеризация уже ожидающих событий
        if (wl_display_prepare_read(g_display) == 0) {
            // Успешно подготовились к чтению, можно ждать событий в poll
            wayland_read_prepared = true;
        } else {
            // Не удалось подготовиться, значит есть события для диспетчеризации
            wayland_read_prepared = false;
            if (wl_display_dispatch_pending(g_display) < 0) {
                 fprintf(stderr, "Error: wl_display_dispatch_pending failed. Closing.\n");
                 running = false;
                 continue;
            }
             // После диспетчеризации могут появиться новые запросы, делаем flush еще раз
              flush_ret = wl_display_flush(g_display);
             if (flush_ret < 0 && errno != EAGAIN && errno != EPIPE) {
                perror("Fatal: wl_display_flush failed after dispatch_pending");
                running = false;
                continue;
             } else if (errno == EPIPE) {
                fprintf(stderr, "Error: Wayland connection lost (EPIPE on flush after dispatch).\n");
                running = false;
                continue;
             }
        }

        // 3. Заполнение массива pollfd
        int current_poll_count = 0;

        // Wayland FD
        if (current_poll_count < MAX_POLL_FDS) {
            fds_poll[current_poll_count].fd = wayland_fd;
            // Ждем входящих событий только если успешно вызвали wl_display_prepare_read
            fds_poll[current_poll_count].events = wayland_read_prepared ? POLLIN : 0;
            fds_poll[current_poll_count].revents = 0;
            current_poll_count++;
        } else { goto poll_array_full; }

        // Timer FD
        if (g_timer_fd >= 0) {
             if (current_poll_count < MAX_POLL_FDS) {
                fds_poll[current_poll_count].fd = g_timer_fd;
                fds_poll[current_poll_count].events = POLLIN;
                fds_poll[current_poll_count].revents = 0;
                current_poll_count++;
             } else { goto poll_array_full; }
        } // Если таймера нет, просто не добавляем

        // Config Monitor FD
        if (g_config_monitor_fd >= 0) {
             if (current_poll_count < MAX_POLL_FDS) {
                fds_poll[current_poll_count].fd = g_config_monitor_fd;
                fds_poll[current_poll_count].events = POLLIN;
                fds_poll[current_poll_count].revents = 0;
                current_poll_count++;
            } else { fprintf(stderr,"Warning: Poll array full, cannot add config monitor fd.\n"); /* Не фатально? */ }
        }

        // IPC Listener FD и Client FDs (только если анимация включена)
        if (config_monitor_is_animation_enabled()) {
            // IPC Listener
            if (g_ipc_listener_fd >= 0) {
                 if (current_poll_count < MAX_POLL_FDS) {
                    fds_poll[current_poll_count].fd = g_ipc_listener_fd;
                    fds_poll[current_poll_count].events = POLLIN; // Ждем новых подключений
                    fds_poll[current_poll_count].revents = 0;
                    current_poll_count++;
                 } else { fprintf(stderr,"Warning: Poll array full, cannot add IPC listener fd.\n"); }
            }
            // IPC Clients
            int client_fds[MAX_IPC_CLIENTS];
            int num_clients = ipc_get_active_clients(client_fds, MAX_IPC_CLIENTS);
            for (int i = 0; i < num_clients; ++i) {
                 if (current_poll_count < MAX_POLL_FDS) {
                     fds_poll[current_poll_count].fd = client_fds[i];
                     fds_poll[current_poll_count].events = POLLIN; // Ждем команд от клиента
                     fds_poll[current_poll_count].revents = 0;
                     current_poll_count++;
                 } else {
                      fprintf(stderr,"Warning: Poll array full, cannot add IPC client fd %d.\n", client_fds[i]);
                      break; // Больше не добавляем клиентов
                 }
            }
        }

        // Обнуляем хвост массива pollfd (на всякий случай)
        for (int i = current_poll_count; i < MAX_POLL_FDS; ++i) {
            fds_poll[i].fd = -1;
        }


        // --- Ожидание событий (poll) ---
        int poll_ret = poll(fds_poll, (nfds_t)current_poll_count, -1); // Ждем бесконечно

        if (poll_ret < 0) {
            if (errno == EINTR) {
                // Прервано сигналом, просто повторяем цикл
                 // printf("Poll interrupted by signal, continuing...\n");
                continue;
            }
            // Реальная ошибка poll
            perror("Fatal: poll failed");
            if (wayland_read_prepared) {
                // Если мы подготовились к чтению, нужно отменить его перед выходом
                wl_display_cancel_read(g_display);
            }
            running = false;
            continue;
        }

        // --- Обработка событий после poll ---

        // 1. Обработка событий Wayland
        if (wayland_read_prepared) {
             // Проверяем, есть ли событие на Wayland FD
             bool wayland_event_ready = false;
             for(int i=0; i < current_poll_count; ++i) {
                 if (fds_poll[i].fd == wayland_fd) {
                     if (fds_poll[i].revents & (POLLIN | POLLERR | POLLHUP)) {
                         wayland_event_ready = true;
                     }
                     if (fds_poll[i].revents & (POLLERR | POLLHUP)) {
                          fprintf(stderr, "Error: Poll reported error (0x%x) on Wayland display fd.\n", fds_poll[i].revents);
                          running = false; // Ошибка на сокете Wayland - выходим
                     }
                     break; // Нашли Wayland FD, дальше не ищем
                 }
             }

             if (running && wayland_event_ready) {
                 // Читаем и диспетчеризуем события Wayland
                 if (wl_display_read_events(g_display) < 0) {
                     if (errno == EPIPE) {
                         fprintf(stderr, "Error: Wayland connection lost (EPIPE on read_events).\n");
                     } else {
                         perror("Error: wl_display_read_events failed");
                     }
                     running = false;
                     // Не отменяем read, так как чтение уже провалилось
                 } else {
                     // Успешно прочитали, теперь диспетчеризуем
                     if (wl_display_dispatch_pending(g_display) < 0) {
                         fprintf(stderr, "Error: wl_display_dispatch_pending failed after read. Closing.\n");
                         running = false;
                     }
                 }
             } else if (running) {
                 // События на Wayland FD не было (или была ошибка), отменяем подготовку к чтению
                 wl_display_cancel_read(g_display);
             }
             wayland_read_prepared = false; // В любом случае, подготовка больше неактуальна
        } // end if (wayland_read_prepared)


        // 2. Обработка событий на других FD (таймер, конфиг, IPC)
        if (!running) continue; // Если Wayland отвалился, не обрабатываем остальное

        for (int i = 0; i < current_poll_count; ++i) {
            if (fds_poll[i].fd == -1 || fds_poll[i].fd == wayland_fd) {
                continue; // Пропускаем неактивные и Wayland FD
            }

            short revents = fds_poll[i].revents;
            int current_fd = fds_poll[i].fd;

            // Проверка на ошибки
            if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
                fprintf(stderr, "Error: Poll reported error (0x%x) on fd %d.\n", revents, current_fd);
                 // Закрываем проблемный FD и удаляем его из мониторинга
                 if (current_fd == g_ipc_listener_fd) {
                     ipc_cleanup(); // Закрывает и слушателя, и клиентов
                     g_ipc_listener_fd = -1;
                 } else if (current_fd == g_timer_fd) {
                     cleanup_timer(g_timer_fd); g_timer_fd = -1;
                 } else if (current_fd == g_config_monitor_fd) {
                     config_monitor_cleanup(); g_config_monitor_fd = -1;
                 } else {
                     // Предполагаем, что это IPC клиент
                     ipc_close_client(current_fd);
                     // ipc_get_active_clients в следующей итерации его не вернет
                 }
                 // Установка fd в -1 здесь не нужна, так как массив pollfd перестраивается на каждой итерации
                continue; // Переходим к следующему FD
            }

            // Обработка входящих данных
            if (revents & POLLIN) {
                if (current_fd == g_timer_fd) {
                    // --- Событие таймера ---
                    uint64_t expirations;
                    ssize_t nread = read(g_timer_fd, &expirations, sizeof(expirations));

                    if (nread == sizeof(expirations)) {
                        // Если анимация включена, рендерим новый кадр
                        if (config_monitor_is_animation_enabled()) {
                             // printf("Timer event: Triggering frame render.\n"); // Отладка
                             struct timespec current_time;
                             clock_gettime(CLOCK_MONOTONIC, &current_time);
                             uint32_t time_ms = (uint32_t)(((uint64_t)current_time.tv_sec * 1000) + ((uint64_t)current_time.tv_nsec / 1000000));
                             // Рендерим для всех активных выходов
                             for (struct client_output *output = outputs_list_head; output; output = output->next) {
                                 if (output->configured && output->renderer_state_gl) {
                                     present_output_frame(output, time_ms);
                                 }
                             }
                             // Flush после рендеринга сделает следующий вызов wl_display_flush в начале цикла
                        } else {
                            // printf("Timer event: Animation disabled, skipping render.\n"); // Отладка
                        }
                    } else if (nread < 0 && errno != EAGAIN) {
                        perror("Error reading from timer fd");
                        cleanup_timer(g_timer_fd); g_timer_fd = -1; // Отключаем таймер при ошибке
                    } else if (nread != sizeof(expirations) && nread >= 0) {
                         fprintf(stderr, "Warning: Read unexpected number of bytes (%zd) from timer fd.\n", nread);
                    }
                     // Если EAGAIN, просто игнорируем - прочитаем в следующий раз

                } else if (current_fd == g_config_monitor_fd) {
                    // --- Событие от монитора конфигурации ---
                    printf("Config monitor event detected.\n");
                    config_monitor_handle_event(); // Вызовет on_config_changed, если нужно
                    // Проверяем, не закрылся ли fd монитора после обработки
                    if (config_monitor_get_fd() < 0) {
                         fprintf(stderr,"Info: Config monitor fd closed after event handling.\n");
                         g_config_monitor_fd = -1;
                    }

                } else if (current_fd == g_ipc_listener_fd) {
                    // --- Новое IPC подключение ---
                     // Проверяем еще раз, т.к. состояние могло измениться после config event
                     if (config_monitor_is_animation_enabled()) {
                        printf("IPC listener event: Accepting new client.\n");
                        int new_client_fd = ipc_accept_client();
                        if (new_client_fd < 0) {
                            // EAGAIN/EWOULDBLOCK - не ошибка, просто нет ожидающих соединений
                            if (errno != EAGAIN && errno != EWOULDBLOCK) {
                                 perror("Error accepting IPC client");
                                 // Возможно, стоит закрыть listener? Зависит от ошибки.
                            }
                        } else {
                             printf("IPC client accepted (fd: %d)\n", new_client_fd);
                             // Новый клиент будет добавлен в poll на следующей итерации
                        }
                     }

                } else {
                     // --- Данные от IPC клиента ---
                     // Проверяем еще раз на всякий случай
                    if (config_monitor_is_animation_enabled()) {
                         // printf("IPC client event on fd %d.\n", current_fd); // Отладка
                         // Используем буфер, объявленный ранее
                         ipc_read_result_t read_res = ipc_read_command(current_fd, ipc_read_buffer, sizeof(ipc_read_buffer));

                         switch (read_res) {
                             // Используем имена из предупреждений компилятора
                             case IPC_READ_RESULT_SUCCESS: // Вероятно, это эквивалент "OK"
                                 // Команда успешно прочитана и обработчик (process_ipc_command) уже вызван внутри ipc_read_command
                                 // printf("Debug: IPC_READ_RESULT_SUCCESS on fd %d.\n", current_fd);
                                 break;
                             case IPC_READ_RESULT_WOULD_BLOCK: // Возможно, это означает "частичное чтение" или "нужно ждать еще"
                                 // Команда не полная, ждем еще данных
                                 // printf("Debug: IPC_READ_RESULT_WOULD_BLOCK on fd %d.\n", current_fd);
                                 break;
                             case IPC_READ_RESULT_EOF:
                                 printf("IPC client fd %d disconnected (EOF).\n", current_fd);
                                 // ipc_close_client() будет вызван внутри ipc_read_command или внтури ipc модуля при EOF.
                                 // Клиент будет удален из poll на следующей итерации автоматически,
                                 // так как ipc_get_active_clients его больше не вернет.
                                 break;
                             case IPC_READ_RESULT_ERROR:
                                 fprintf(stderr, "Error reading from IPC client fd %d.\n", current_fd);
                                 // Аналогично EOF, модуль IPC должен обработать закрытие.
                                 break;
                             case IPC_READ_RESULT_BUFFER_TOO_SMALL: // Это было в оригинальном коде
                                 fprintf(stderr, "Error: IPC command from fd %d exceeded buffer size (%zu).\n", current_fd, sizeof(ipc_read_buffer));
                                 ipc_close_client(current_fd); // Принудительно закрываем этого клиента
                                 break;
                             case IPC_READ_RESULT_TOO_LONG: // Из предупреждений
                                  fprintf(stderr, "Error: IPC command from fd %d too long (internal logic).\n", current_fd);
                                  ipc_close_client(current_fd);
                                  break;
                             case IPC_READ_RESULT_NO_HANDLER: // Из предупреждений
                                  fprintf(stderr, "Error: No IPC handler configured, but received data on fd %d.\n", current_fd);
                                  ipc_close_client(current_fd);
                                  break;
                             case IPC_READ_RESULT_INVALID_FD:
                                 fprintf(stderr, "Error: ipc_read_command called with invalid fd %d.\n", current_fd);
                                 break;
                             // Добавляем default на случай появления новых кодов возврата
                             default:
                                fprintf(stderr, "Warning: Unknown ipc_read_result_t value (%d) received for fd %d.\n", read_res, current_fd);
                                ipc_close_client(current_fd); // Безопаснее закрыть клиента
                                break;
                         }
                         
                    }
                }
            } // end if (revents & POLLIN)
        } // end for (loop through poll fds)
    } // end while (running)

    printf("Exiting main loop.\n");
    goto cleanup_exit; // Используем goto для единой точки выхода и очистки

poll_array_full:
    fprintf(stderr, "Fatal: Poll array size (MAX_POLL_FDS=%d) is too small for the number of file descriptors.\n", MAX_POLL_FDS);
    // Попытка отменить read перед выходом, если нужно
    if (wayland_read_prepared) { wl_display_cancel_read(g_display); }
    // Немедленный выход с очисткой

cleanup_exit:
    cleanup_application();
    printf("Application finished.\n");
    return EXIT_SUCCESS; // Или EXIT_FAILURE, если выход был из-за ошибки
}


// --- Общая функция очистки ресурсов ---
static void cleanup_application(void) {
     printf("--- Starting Application Cleanup ---\n");
     // Порядок важен: сначала останавливаем источники событий, потом чистим ресурсы

     // 1. Закрываем таймер
     cleanup_timer(g_timer_fd); g_timer_fd = -1;

     // 2. Очищаем IPC (закрывает слушателя и всех клиентов)
     ipc_cleanup(); g_ipc_listener_fd = -1;

     // 3. Очищаем монитор конфигурации
     config_monitor_cleanup(); g_config_monitor_fd = -1;

     // 4. Очищаем Wayland (включая все выходы, поверхности, буферы и т.д.)
     // Эта функция должна быть последней из Wayland-зависимых
     cleanup_wayland();

     // Другие ресурсы, если есть (например, глобальные состояния рендерера)
     // ...

    printf("--- Application Cleanup Finished ---\n");
}
