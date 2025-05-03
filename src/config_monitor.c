#include "src/renderer/core.h"
#define _GNU_SOURCE // Для strdup, mkostemp, ftruncate64 (хотя последние два тут не нужны)
#include "config_monitor.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <limits.h>
#include <unistd.h>
#include <sys/inotify.h>
#include <libgen.h> // Для dirname (можно было бы и без него, но так проще)

#include "output.h" // Нужен для доступа к списку outputs и их рендерерам
#include "ipc.h"

// Максимальный размер буфера для чтения событий inotify
#define INOTIFY_EVENT_BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))
// Максимальная длина команды, читаемой из файла (как в ipc.h)
#define MAX_COMMAND_LEN 255 // Предполагаем, что это значение из ipc.h или другого места

// --- Статические переменные модуля ---
static int inotify_fd = -1;
static int inotify_watch_descriptor = -1;
static char command_file_path[PATH_MAX] = {0};
static char command_dir[PATH_MAX] = {0};
static char command_basename[NAME_MAX + 1] = {0};

// Текущее состояние
static char *current_command_arg = NULL; // Хранит текущую команду (или ANIMATION_COMMAND)
static bool current_animation_enabled = false;

// Callback и пользовательские данные
static config_change_callback_t change_callback = NULL;
static void *callback_user_data = NULL;

void on_config_changed(const char *new_command, bool new_animation_state, void *user_data) {
    UNUSED(user_data); // Не используем user_data в этой реализации

    printf("Config Handler: Configuration changed! New Command: '%s', Animation State: %s\n",
           new_command ? new_command : "<null>", new_animation_state ? "ON" : "OFF");

    // Если анимация была выключена, закрываем все IPC соединения,
    // так как они больше не должны использоваться.
    if (!new_animation_state) {
        printf("Config Handler: Animation turned OFF. Closing all active IPC client connections.\n");
        ipc_close_all_clients();
        // Основной цикл перестанет слушать ipc_listener_fd и клиентские fd
    }

    // Применяем новую команду (или NULL, если команда не изменилась) ко всем рендерерам
    bool redraw_needed = false;
    for (struct client_output *output = outputs_list_head; output; output = output->next) {
        if (output->renderer_state_gl && output->configured) {
            // Передаем новую команду рендереру. renderer_handle_command должен уметь обрабатывать NULL.
            if (renderer_core_handle_command(output->renderer_state_gl, new_command)) {
                 printf("  -> O:%u: Config command applied successfully.\n", output->wl_name);
                 redraw_needed = true;
            } else {
                 fprintf(stderr, "Warning: Renderer on O:%u failed to handle config command '%s'\n",
                         output->wl_name, new_command ? new_command : "<null>");
            }
        }
    }

    // Если команда повлияла на рендереры или если состояние анимации изменилось
    // (например, чтобы показать/скрыть статический кадр), инициируем перерисовку.
    // Всегда перерисовываем при смене конфига, чтобы отразить изменения.
    if (redraw_needed || true) { // Всегда перерисовываем для надежности
        printf("Config Handler: Triggering redraw due to config change.\n");
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint32_t ms = (uint32_t)(((uint64_t)ts.tv_sec * 1000) + ((uint64_t)ts.tv_nsec / 1000000));

        for (struct client_output *output = outputs_list_head; output; output = output->next) {
             if (output->configured && output->renderer_state_gl) {
                 present_output_frame(output, ms);
             }
        }
         // Может потребоваться wl_display_flush() здесь или в основном цикле
    }
}

// --- Вспомогательные функции ---

// Внутренняя функция для чтения файла и обработки изменений
// Возвращает true, если чтение/обработка прошли успешно (даже если файл не найден),
// false при критической ошибке ввода/вывода.
static bool read_and_process_command_file(void) {
    if (command_file_path[0] == '\0') {
        // Путь не установлен, считаем, что команды нет
        if (current_command_arg != NULL || current_animation_enabled != false) {
             printf("ConfigMonitor: Path not set, resetting state.\n");
             free(current_command_arg);
             current_command_arg = NULL;
             current_animation_enabled = false;
             if (change_callback) {
                 change_callback(NULL, false, callback_user_data);
             }
        }
        return true; // Не ошибка, просто нет файла для чтения
    }

    FILE *f = fopen(command_file_path, "r");
    char buffer[MAX_COMMAND_LEN + 2] = {0}; // +1 для null, +1 для \n
    bool file_exists = true;
    char *command_from_file = NULL;

    if (!f) {
        if (errno == ENOENT) {
            file_exists = false;
            // printf("ConfigMonitor: Command file '%s' not found.\n", command_file_path);
        } else {
            perror("ConfigMonitor: fopen command file failed");
            // Не меняем состояние при ошибке чтения, просто сообщаем
            return false;
        }
    }

    if (file_exists) {
        if (fgets(buffer, sizeof(buffer), f)) {
            buffer[strcspn(buffer, "\n\r")] = 0; // Убираем \n или \r
            if (buffer[0] != '\0') {
                command_from_file = buffer;
            }
        } else if (ferror(f)) {
            perror("ConfigMonitor: fgets from command file failed");
            fclose(f);
            return false; // Ошибка чтения, не меняем состояние
        }
        // else: EOF сразу (пустой файл), command_from_file остается NULL
        fclose(f);
    }

    // --- Логика определения нового состояния ---
    bool new_mode_is_animation = false;
    char *new_command_arg_for_state = NULL; // Что будет сохранено в current_command_arg

    if (command_from_file && strcmp(command_from_file, ANIMATION_COMMAND) == 0) {
        new_mode_is_animation = true;
        new_command_arg_for_state = ANIMATION_COMMAND; // Используем макрос напрямую
    } else {
        new_mode_is_animation = false;
        if (command_from_file) { // Не NULL и не пустая строка (проверено выше)
             new_command_arg_for_state = command_from_file; // Указывает на buffer
        } else { // Файл не найден, пуст или ошибка чтения (обработанная)
             new_command_arg_for_state = NULL;
        }
    }

    // --- Проверяем, изменилось ли состояние ---
    bool state_changed = false;
    if (new_mode_is_animation != current_animation_enabled ||
        (current_command_arg == NULL && new_command_arg_for_state != NULL) ||
        (current_command_arg != NULL && new_command_arg_for_state == NULL) ||
        (current_command_arg != NULL && new_command_arg_for_state != NULL && strcmp(current_command_arg, new_command_arg_for_state) != 0))
    {
        state_changed = true;
    }

    if (state_changed) {
        printf("ConfigMonitor: State change detected. New mode: %s, Command: '%s'\n",
               new_mode_is_animation ? "Animation" : "Static",
               new_command_arg_for_state ? new_command_arg_for_state : "(null)");

        // Обновляем сохраненное состояние
        free(current_command_arg); // Освобождаем старое значение, если было
        current_command_arg = NULL; // На случай ошибки strdup

        if (new_command_arg_for_state != NULL) {
            // Нужно скопировать строку, т.к. new_command_arg_for_state
            // может указывать на buffer или ANIMATION_COMMAND.
            // Мы всегда храним собственную копию (или NULL).
            current_command_arg = strdup(new_command_arg_for_state);
            if (!current_command_arg) {
                perror("ConfigMonitor: strdup failed for new command");
                current_animation_enabled = false; // Сброс в безопасное состояние?
                // Вызываем колбэк с ошибкой (NULL, false)
                 if (change_callback) {
                    change_callback(NULL, false, callback_user_data);
                 }
                return false; // Ошибка выделения памяти
            }
        }
        // Если new_command_arg_for_state == NULL, то current_command_arg уже NULL.

        current_animation_enabled = new_mode_is_animation;

        // Вызываем callback
        if (change_callback) {
            change_callback(current_command_arg, current_animation_enabled, callback_user_data);
        }
    }
    /* else { printf("ConfigMonitor: State unchanged.\n"); } */

    return true; // Успешная обработка
}


// --- Реализация публичных функций ---

bool config_monitor_init(config_change_callback_t callback, void *user_data) {
    if (!callback) {
        fprintf(stderr, "ConfigMonitor Error: Callback function cannot be NULL.\n");
        return false;
    }

    // Сброс состояния на всякий случай
    config_monitor_cleanup();
    change_callback = callback;
    callback_user_data = user_data;

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

    // Сохраняем директорию и имя файла
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

    printf("ConfigMonitor: Watching directory '%s' for events related to '%s' (fd: %d, wd: %d)\n",
           command_dir, command_basename, inotify_fd, inotify_watch_descriptor);

    // Прочитать начальное состояние файла и вызвать callback
    if (!read_and_process_command_file()) {
         fprintf(stderr, "ConfigMonitor: Failed to process initial command file state.\n");
         // Не фатально для init, но состояние может быть неверным.
         // Callback уже был вызван с (NULL, false) в случае ошибки strdup.
    }

    return true;
}

int config_monitor_get_fd(void) {
    return inotify_fd;
}

void config_monitor_handle_event(void) {
    if (inotify_fd < 0) return; // Не инициализирован

    char buffer[INOTIFY_EVENT_BUF_LEN] __attribute__ ((aligned(__alignof__(struct inotify_event))));
    ssize_t len;

    // Читаем все доступные события
    while ((len = read(inotify_fd, buffer, sizeof(buffer))) > 0) {
        const struct inotify_event *event;
        bool relevant_event_found = false;

        for (char *ptr = buffer; ptr < buffer + len; ) {
            // Проверка, что можем прочитать заголовок события
            if (ptr + sizeof(struct inotify_event) > buffer + len) {
                fprintf(stderr, "ConfigMonitor: Partial inotify event received (header).\n");
                break; // Прерываем обработку этого буфера
            }
            event = (const struct inotify_event *) ptr;

            // Проверка, что можем прочитать имя файла, если оно есть
            if (event->len > 0 && (ptr + sizeof(struct inotify_event) + event->len > buffer + len)) {
                fprintf(stderr, "ConfigMonitor: Partial inotify event received (name).\n");
                break; // Прерываем обработку этого буфера
            }

            // Проверяем, относится ли событие к нашему файлу
            if (event->wd == inotify_watch_descriptor && event->len > 0 && strcmp(event->name, command_basename) == 0) {
                // Проверяем тип события
                if (event->mask & (IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE | IN_DELETE | IN_MOVED_FROM)) {
                     printf("ConfigMonitor: Relevant event (mask 0x%x) for '%s' detected.\n", event->mask, command_basename);
                    relevant_event_found = true;
                    // Нет смысла проверять остальные события в этом буфере,
                    // так как мы все равно перечитаем файл один раз.
                    break;
                }
            }
             // Переход к следующему событию
             ptr += sizeof(struct inotify_event) + event->len;
        }

        if (relevant_event_found) {
            // Перечитываем файл и обрабатываем изменение состояния (если оно есть)
            read_and_process_command_file();
            // После обработки релевантного события, можно выйти из цикла while(read),
            // т.к. состояние уже обновлено по последнему изменению файла.
            // Если хочется обработать *каждое* событие отдельно (что обычно излишне),
            // то этот break не нужен.
             break;
        }
    } // конец while(read > 0)

    // Обработка ошибок read
    if (len < 0 && errno != EAGAIN) {
        perror("ConfigMonitor: read from inotify_fd failed");
        // Можно решить остановить мониторинг при ошибке
        fprintf(stderr, "ConfigMonitor: Disabling file watching due to read error.\n");
        config_monitor_cleanup(); // Закрываем fd и сбрасываем состояние
         // Уведомляем об ошибке через callback? Сбрасываем в (NULL, false)?
         if (change_callback) {
             // Проверяем, нужно ли менять состояние перед вызовом
             if (current_command_arg != NULL || current_animation_enabled != false) {
                 free(current_command_arg);
                 current_command_arg = NULL;
                 current_animation_enabled = false;
                 change_callback(NULL, false, callback_user_data);
             }
         }
    }
}

bool config_monitor_is_animation_enabled(void) {
    return current_animation_enabled;
}

const char* config_monitor_get_current_command(void) {
    return current_command_arg; // Может быть NULL
}

void config_monitor_cleanup(void) {
    if (inotify_fd >= 0) {
        printf("ConfigMonitor: Cleaning up...\n");
        if (inotify_watch_descriptor >= 0) {
            inotify_rm_watch(inotify_fd, inotify_watch_descriptor);
            inotify_watch_descriptor = -1;
        }
        close(inotify_fd);
        inotify_fd = -1;
    }
    free(current_command_arg);
    current_command_arg = NULL;
    current_animation_enabled = false;
    // Сбрасываем путь и колбэк
    command_file_path[0] = '\0';
    command_dir[0] = '\0';
    command_basename[0] = '\0';
    change_callback = NULL;
    callback_user_data = NULL;
}
