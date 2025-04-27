#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <time.h>
#include <stdint.h>
#include <sys/timerfd.h>
#include <sys/inotify.h>
#include <libgen.h> // Для dirname

#include <cairo.h>
#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"

#include "renderer.h"
#include "ipc.h"

#define MAX_IPC_CLIENTS 5
#define MAX_POLL_FDS (4 + MAX_IPC_CLIENTS)

#define IPC_READ_BUFFER_SIZE (IPC_MAX_COMMAND_LEN + 1)
#define INOTIFY_EVENT_BUF_LEN (10 * (sizeof(struct inotify_event) + NAME_MAX + 1))

#define ANIMATION_COMMAND "%SETUP_ANIMATION%"
#define COMMAND_FILENAME "wallpaper_command.txt"

#define UNUSED(x) (void)(x)

static struct wl_display *display = NULL;
static struct wl_registry *registry = NULL;
static struct zwlr_layer_shell_v1 *layer_shell = NULL;
static struct wl_compositor *compositor = NULL;
static struct wl_shm *shm = NULL;

struct client_output {
    struct wl_output *wl_output;
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    int width, height;
    bool configured;
    RendererState *renderer_state;
    uint32_t wl_name;
};

static struct client_output *outputs = NULL;
static int n_outputs = 0;

static int timer_fd = -1;
static long timer_interval_nsec = 1000000000L / 60;

static bool use_inotify = true; // Включаем по умолчанию
static int inotify_fd = -1;
static int inotify_watch_descriptor = -1;
static char command_file_path[PATH_MAX] = {0};
static char command_dir[PATH_MAX] = {0};
static char command_basename[NAME_MAX + 1] = {0};

static char *current_command_arg = NULL;
static bool animation_enabled = false;

static void cleanup(void);
static bool setup_timer(long interval_ns);
static bool setup_inotify(void);
static bool render_and_commit_output(struct client_output *output, uint32_t time_ms);
static void buffer_handle_release(void *data, struct wl_buffer *buffer);
static bool read_command_from_file(void);
static void init_wayland(void);
static void handle_command_change(const char *new_command_str);

static void process_ipc_command(int client_fd, const char *command, size_t command_len) {
    UNUSED(client_fd);
    UNUSED(command_len);
    printf("Main: Processing IPC command: '%s'\n", command);
    handle_command_change(command);
}

static void handle_command_change(const char *new_command_str) {
    printf("DEBUG: handle_command_change received command: '%s'\n",
           new_command_str ? new_command_str : "(null)");

    bool trigger_immediate_redraw = false;  // Нужна ли немедленная перерисовка?
    char *new_command_arg_for_renderer = NULL; // Что передать рендереру?
    char *new_stored_command_arg = NULL;    // Что сохранить в current_command_arg?
    bool intended_animation_state = animation_enabled; // Начинаем с текущего состояния

    // 1. Интерпретируем команду
    if (new_command_str) {
        if (strcmp(new_command_str, ANIMATION_COMMAND) == 0) {
            // *** ЛОГИКА ПЕРЕКЛЮЧЕНИЯ (TOGGLE) ***
            intended_animation_state = !animation_enabled; // Инвертируем текущее состояние

            if (intended_animation_state) {
                // Переключили В состояние "Анимация ВКЛ"
                printf("DEBUG: Command (%s) toggled animation ON.\n", ANIMATION_COMMAND);
                new_stored_command_arg = strdup(ANIMATION_COMMAND); // Сохраняем команду анимации
            } else {
                // Переключили В состояние "Анимация ВЫКЛ"
                printf("DEBUG: Command (%s) toggled animation OFF.\n", ANIMATION_COMMAND);
                new_stored_command_arg = NULL; // Очищаем сохраненную команду
            }
            // Передаем рендереру саму команду ANIMATION_COMMAND, чтобы он знал,
            // что произошло переключение анимационного режима (вкл или выкл)
            new_command_arg_for_renderer = strdup(ANIMATION_COMMAND);
            trigger_immediate_redraw = true; // Перерисовка нужна, чтобы показать эффект вкл/выкл

        } else if (new_command_str[0] != '\0') {
            // Любая другая непустая команда (не ANIMATION_COMMAND)
            if (animation_enabled) {
                // Анимация включена: считаем команду модификатором
                intended_animation_state = true; // Анимация продолжает работать
                new_command_arg_for_renderer = strdup(new_command_str); // Передаем модификатор рендереру
                // Сохраняем модификатор как текущее состояние? Или оставить ANIMATION_COMMAND?
                // Решим СОХРАНЯТЬ модификатор, чтобы current_command_arg отражал последнее изменение.
                new_stored_command_arg = strdup(new_command_str);
                printf("DEBUG: Modifier command '%s' received while animation ON.\n", new_command_str);
                // trigger_immediate_redraw = true; // Раскомментируй, если эффект модификатора должен быть виден немедленно
            } else {
                // Анимация выключена: считаем команду установкой статического состояния
                intended_animation_state = false; // Анимация остается выключенной
                new_command_arg_for_renderer = strdup(new_command_str); // Передаем рендереру
                new_stored_command_arg = strdup(new_command_str); // Сохраняем команду статического состояния
                printf("DEBUG: Static state command '%s' received while animation OFF.\n", new_command_str);
                trigger_immediate_redraw = true; // Нужна перерисовка статики
            }
        } else { // Пустая строка ""
            intended_animation_state = false; // Считаем остановкой/очисткой
            new_command_arg_for_renderer = NULL; // NULL для рендерера?
            new_stored_command_arg = NULL;
            printf("DEBUG: Empty command received - stopping animation/clearing.\n");
            trigger_immediate_redraw = true; // Перерисовка для очистки
        }
    } else { // NULL command (например, файл удален)
        intended_animation_state = false; // Считаем остановкой/очисткой
        new_command_arg_for_renderer = NULL;
        new_stored_command_arg = NULL;
        printf("DEBUG: NULL command received - stopping animation/clearing.\n");
        trigger_immediate_redraw = true; // Перерисовка для очистки
    }

    // 2. Проверка на ошибки выделения памяти (упрощенная)
    // Проверяем, если ожидалась строка, а получили NULL (с учетом намеренных NULL)
    if (new_command_str && new_command_str[0] != '\0' && !new_command_arg_for_renderer) {
         perror("strdup failed for renderer command");
         free(new_stored_command_arg);
         return;
    }
     if (new_command_str && new_command_str[0] != '\0' &&
        !(intended_animation_state == false && strcmp(new_command_str, ANIMATION_COMMAND) == 0) && // Исключаем NULL при выключении через toggle
        !(new_command_str[0] == '\0') && // Исключаем NULL для пустой команды
        !(new_command_str == NULL) && // Исключаем NULL для NULL команды
        !new_stored_command_arg)
     {
        perror("strdup failed for stored command");
        free(new_command_arg_for_renderer);
        return;
     }


    // 3. Проверяем, изменилось ли общее состояние (флаг анимации ИЛИ сохраненный аргумент)
    if (intended_animation_state != animation_enabled ||
        (current_command_arg == NULL && new_stored_command_arg != NULL) ||
        (current_command_arg != NULL && new_stored_command_arg == NULL) ||
        (current_command_arg != NULL && new_stored_command_arg != NULL && strcmp(current_command_arg, new_stored_command_arg) != 0))
    {
        printf(" -> State change detected. New animation state: %s, New stored command arg: '%s'\n",
               intended_animation_state ? "true" : "false", new_stored_command_arg ? new_stored_command_arg : "(null)");

        // Обновляем глобальное состояние
        animation_enabled = intended_animation_state;
        free(current_command_arg);
        current_command_arg = new_stored_command_arg; // Забираем владение new_stored_command_arg
        new_stored_command_arg = NULL;
    } else {
        printf(" -> State did not change.\n");
    }

    // 4. Вызываем renderer_handle_command, если для него была команда
    if (new_command_arg_for_renderer) {
        printf(" -> Calling renderer_handle_command with: %s\n", new_command_arg_for_renderer);
        for (int i = 0; i < n_outputs; ++i) {
            if (outputs[i].renderer_state && outputs[i].configured) {
                if (!renderer_handle_command(outputs[i].renderer_state, new_command_arg_for_renderer)) {
                    fprintf(stderr, "Warning: Renderer for output %u failed to handle command '%s'\n", outputs[i].wl_name, new_command_arg_for_renderer);
                }
            }
        }
    }

    // 5. Освобождаем память, которая не перешла в current_command_arg
    free(new_command_arg_for_renderer);
    free(new_stored_command_arg);


    // 6. Выполняем немедленную перерисовку, если флаг установлен
    if (trigger_immediate_redraw) {
        printf(" -> Triggering immediate redraw.\n");
        struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
        uint32_t ms = (uint32_t)(((uint64_t)ts.tv_sec * 1000) + ((uint64_t)ts.tv_nsec / 1000000));
        for (int i = 0; i < n_outputs; ++i) {
             if(outputs[i].configured) {
                 render_and_commit_output(&outputs[i], ms);
             }
        }
    }

    printf("DEBUG: handle_command_change finished. animation_enabled = %s, current_command_arg = '%s'\n",
           animation_enabled ? "true" : "false", current_command_arg ? current_command_arg : "(null)");
}

static void handle_global(void *data, struct wl_registry *reg,
                          uint32_t name, const char *iface, uint32_t ver) {
    UNUSED(data); UNUSED(ver);

    if (strcmp(iface, wl_compositor_interface.name) == 0) {
        compositor = wl_registry_bind(reg, name, &wl_compositor_interface, 4);
    } else if (strcmp(iface, wl_shm_interface.name) == 0) {
        shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
    } else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
        layer_shell = wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface, 2);
    } else if (strcmp(iface, wl_output_interface.name) == 0) {
        struct client_output *new_outputs = realloc(outputs, sizeof(*outputs) * (size_t)(n_outputs + 1));
        if (!new_outputs) {
            perror("realloc outputs failed");
            cleanup();
            exit(EXIT_FAILURE);
        }
        outputs = new_outputs;
        struct client_output *output = &outputs[n_outputs];
        memset(output, 0, sizeof(*output));
        output->wl_output = wl_registry_bind(reg, name, &wl_output_interface, 3); // v3 for name/desc
        output->wl_name = name;
        n_outputs++;
        printf("Found output: %u\n", name);
    }
}

static void handle_global_remove(void *data, struct wl_registry *reg, uint32_t name) {
     UNUSED(data); UNUSED(reg);
     printf("Wayland global removed: %u\n", name);
     int removed_index = -1;
     for(int i = 0; i < n_outputs; ++i) {
         if(outputs[i].wl_name == name) {
             printf("  -> Output %u (%d) is being removed.\n", name, i);
             outputs[i].configured = false;
             if (outputs[i].renderer_state) {
                 renderer_cleanup(outputs[i].renderer_state);
                 outputs[i].renderer_state = NULL;
             }
             if (outputs[i].layer_surface) {
                 zwlr_layer_surface_v1_destroy(outputs[i].layer_surface);
                 outputs[i].layer_surface = NULL;
             }
             if (outputs[i].surface) {
                 wl_surface_destroy(outputs[i].surface);
                 outputs[i].surface = NULL;
             }
             if (outputs[i].wl_output) {
                 wl_output_release(outputs[i].wl_output); // Use release for v2+
                 outputs[i].wl_output = NULL;
             }
             removed_index = i;
             break;
         }
     }

     if (removed_index != -1 && removed_index < n_outputs - 1) {
         // Shift elements to fill the gap
         memmove(&outputs[removed_index], &outputs[removed_index + 1],
                 sizeof(struct client_output) * (size_t)(n_outputs - 1 - removed_index));
     }
     if (removed_index != -1) {
        n_outputs--;
        if (n_outputs > 0) {
            struct client_output *new_outputs = realloc(outputs, sizeof(*outputs) * (size_t)n_outputs);
             if (!new_outputs && n_outputs > 0) { // Only fail realloc if new size > 0
                perror("realloc after removing output failed");
                // Continue, but memory might be overallocated
             } else if (new_outputs || n_outputs == 0) {
                outputs = new_outputs;
             }
        } else {
             free(outputs);
             outputs = NULL;
        }
     }
}

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove
};

static void output_handle_geometry(void *d, struct wl_output *o, int32_t x, int32_t y, int32_t phys_w, int32_t phys_h, int32_t subpixel, const char *make, const char *model, int32_t transform) {
    UNUSED(d); UNUSED(o); UNUSED(x); UNUSED(y); UNUSED(phys_w); UNUSED(phys_h); UNUSED(subpixel); UNUSED(make); UNUSED(model); UNUSED(transform);
}

static void output_handle_mode(void *d, struct wl_output *o, uint32_t flags, int32_t w, int32_t h, int32_t refresh) {
    UNUSED(o); UNUSED(refresh);
    struct client_output *out = d;
    if (flags & WL_OUTPUT_MODE_CURRENT) {
         printf("Output %u Mode: %dx%d @ %.3f Hz%s\n", out->wl_name, w, h, refresh / 1000.0, (flags & WL_OUTPUT_MODE_PREFERRED) ? " (Preferred)" : "");
         // Note: Layer surface configure event is the authority for size
    }
}

static void output_handle_done(void *data, struct wl_output *output) {
    UNUSED(data); UNUSED(output);
}

static void output_handle_scale(void *data, struct wl_output *output, int32_t factor) {
    UNUSED(output);
    struct client_output *out = data;
    printf("Output %u Scale: %d\n", out->wl_name, factor);
}

static void output_handle_name(void *data, struct wl_output *wl_output, const char *name) {
    UNUSED(wl_output);
    struct client_output *out = data;
    printf("Output %u Name: %s\n", out->wl_name, name);
}

static void output_handle_description(void *data, struct wl_output *wl_output, const char *description) {
    UNUSED(wl_output);
    struct client_output *out = data;
    printf("Output %u Description: %s\n", out->wl_name, description);
}


static const struct wl_output_listener output_listener = {
    .geometry = output_handle_geometry,
    .mode     = output_handle_mode,
    .done     = output_handle_done,
    .scale    = output_handle_scale,
    .name = output_handle_name,             // Added for v2+
    .description = output_handle_description // Added for v2+
};


static void layer_surface_handle_configure(void *d, struct zwlr_layer_surface_v1 *layer_surface, uint32_t serial, uint32_t width, uint32_t height) {
    struct client_output *output = d;
    printf("Layer surface configure for output %u: %ux%u (Serial: %u)\n", output->wl_name, width, height, serial);

    bool needs_reinit = (!output->configured || output->width != (int)width || output->height != (int)height);
    output->width = (int)width;
    output->height = (int)height;
    output->configured = true;
    zwlr_layer_surface_v1_ack_configure(layer_surface, serial);

    if (needs_reinit && output->width > 0 && output->height > 0) {
        printf(" -> Re-initializing renderer for output %u (%dx%d) with command: %s\n",
               output->wl_name, output->width, output->height, current_command_arg ? current_command_arg : "(null)");
        if (output->renderer_state) {
            renderer_cleanup(output->renderer_state);
        }
        output->renderer_state = renderer_init(output->width, output->height, current_command_arg);
        if (!output->renderer_state) {
            fprintf(stderr, "Error: Failed to initialize renderer for output %u\n", output->wl_name);
            output->configured = false; // Mark as not ready
        } else {
            struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
            uint32_t ms = (uint32_t)(((uint64_t)ts.tv_sec * 1000) + ((uint64_t)ts.tv_nsec / 1000000));
            render_and_commit_output(output, ms);
        }
    } else if (needs_reinit) { // width or height is 0
        if (output->renderer_state) {
            printf(" -> Cleaning up renderer for output %u due to zero size\n", output->wl_name);
            renderer_cleanup(output->renderer_state);
            output->renderer_state = NULL;
        }
    } else if (output->configured && output->renderer_state) {
        // Size didn't change, but maybe redraw needed?
        // Let timer or explicit command change handle redraws.
    }
}

static void layer_surface_handle_closed(void *d, struct zwlr_layer_surface_v1 *layer_surface) {
    struct client_output *output = d;
    printf("Layer surface closed for output %u.\n", output->wl_name);
    output->configured = false; // Mark as unusable
    // Resources associated with this output might be cleaned up
    // in handle_global_remove if the output itself is removed.
    // If just the layer surface is closed, we might want to handle it here.
    if(output->layer_surface == layer_surface) {
        zwlr_layer_surface_v1_destroy(output->layer_surface);
        output->layer_surface = NULL;
    }
     if(output->surface) {
        wl_surface_destroy(output->surface);
        output->surface = NULL;
     }
     if (output->renderer_state) {
         renderer_cleanup(output->renderer_state);
         output->renderer_state = NULL;
     }
    // Consider removing the output from the list or marking it inactive.
    // For simplicity, we'll rely on handle_global_remove for now.
}

static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_handle_configure,
    .closed = layer_surface_handle_closed,
};

static void buffer_handle_release(void *data, struct wl_buffer *buffer) {
    UNUSED(data);
    wl_buffer_destroy(buffer);
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_handle_release
};

static int create_shm_file(size_t size) {
    char template[] = "/wp-shm-XXXXXX"; // Relative path part
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    char shm_path[PATH_MAX];
    int fd = -1;
    int written = -1;

    if (size == 0) {
        fprintf(stderr, "Error: Attempted to create shm file with zero size.\n");
        errno = EINVAL;
        return -1;
    }

    if (runtime_dir) {
        written = snprintf(shm_path, sizeof(shm_path), "%s%s", runtime_dir, template);
        if (written > 0 && (size_t)written < sizeof(shm_path)) {
             // O_CLOEXEC is good practice here
            fd = mkostemp(shm_path, O_CLOEXEC);
            if (fd >= 0) {
                unlink(shm_path); // Unlink immediately after opening
            } else {
                perror("mkostemp in XDG_RUNTIME_DIR failed, falling back to /tmp");
            }
        } else {
            fprintf(stderr, "Error: XDG_RUNTIME_DIR path too long or snprintf failed.\n");
             // Fall through to try /tmp
        }
    }

    // Fallback to /dev/shm (preferred over /tmp for SHM)
    if (fd < 0) {
        written = snprintf(shm_path, sizeof(shm_path), "/dev/shm%s", template);
         if (written > 0 && (size_t)written < sizeof(shm_path)) {
            fd = mkostemp(shm_path, O_CLOEXEC);
            if (fd >= 0) {
                unlink(shm_path);
            } else {
                perror("mkostemp in /dev/shm failed");
                // Consider one last fallback to /tmp if really needed,
                // but /dev/shm should usually work.
            }
         } else {
             fprintf(stderr, "Error: /dev/shm path too long or snprintf failed.\n");
         }
    }

    if (fd < 0) {
         fprintf(stderr, "Error: Failed to create temporary file in all locations.\n");
         errno = EIO; // Indicate a general I/O failure
         return -1;
    }

    // Check size against OFF_T_MAX if available, otherwise LLONG_MAX
    #ifdef OFF_T_MAX
        if (size > (size_t)OFF_T_MAX) {
            fprintf(stderr, "Requested shm size %zu too large for ftruncate.\n", size);
            close(fd);
            errno = EFBIG;
            return -1;
        }
    #else
        // Fallback check, less precise but better than nothing
        if ((unsigned long long)size > (unsigned long long)LLONG_MAX) {
            fprintf(stderr, "Requested shm size %zu likely too large for ftruncate.\n", size);
            close(fd);
            errno = EFBIG;
            return -1;
        }
    #endif


    // Use ftruncate64 if available for large file support
    #ifdef _GNU_SOURCE
    if (ftruncate64(fd, (off64_t)size) == -1) {
    #else
    if (ftruncate(fd, (off_t)size) == -1) {
    #endif
        perror("ftruncate failed");
        close(fd);
        return -1;
    }

    return fd;
}


static bool render_and_commit_output(struct client_output *output, uint32_t time_ms) {
    if (!output || !output->configured || !output->surface || !output->renderer_state || output->width <= 0 || output->height <= 0) {
        return false;
    }

    cairo_format_t format = CAIRO_FORMAT_ARGB32;
    int stride = cairo_format_stride_for_width(format, output->width);
    if (stride <= 0) {
        fprintf(stderr, "RenderCommit: Failed to calculate stride (width %d) for output %u\n", output->width, output->wl_name);
        return false;
    }
    size_t stride_sz = (size_t)stride;
    size_t size = 0;

    if ((size_t)output->height > SIZE_MAX / stride_sz) {
         fprintf(stderr, "RenderCommit: Requested buffer size calculation overflows SIZE_MAX for output %u (%d x %zu)\n", output->wl_name, output->height, stride_sz);
         return false;
    }
    size = stride_sz * (size_t)output->height;
    if (size == 0) {
         fprintf(stderr, "RenderCommit: Calculated buffer size is zero for output %u\n", output->wl_name);
         return false;
    }

    int fd = create_shm_file(size);
    if (fd < 0) {
        return false;
    }

    // MAP_SHARED is crucial for SHM
    void *map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        perror("RenderCommit: mmap failed");
        close(fd);
        return false;
    }

    bool success = renderer_render_frame(output->renderer_state, map, output->width, output->height, stride, time_ms);

    if (munmap(map, size) == -1) {
        perror("RenderCommit: munmap failed");
        // Continue, but it's an error. The buffer might still be usable by Wayland.
    }

    if (!success) {
        fprintf(stderr, "RenderCommit: Renderer failed for output %u\n", output->wl_name);
        close(fd);
        return false;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, (int64_t)size);
    // We can close the fd immediately after creating the pool
    if (close(fd) == -1) {
         perror("RenderCommit: close shm fd failed (after pool creation)");
         // Wayland probably still holds the fd via the pool, but log the error.
    }
    if (!pool) {
        fprintf(stderr, "RenderCommit: wl_shm_create_pool failed for output %u\n", output->wl_name);
        // fd is already closed or closing failed
        return false;
    }

    // WL_SHM_FORMAT_ARGB8888 corresponds to CAIRO_FORMAT_ARGB32
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, output->width, output->height, stride, WL_SHM_FORMAT_ARGB8888);
    wl_shm_pool_destroy(pool); // Pool is no longer needed after buffer creation
    if (!buffer) {
        fprintf(stderr, "RenderCommit: wl_shm_pool_create_buffer failed for output %u\n", output->wl_name);
        return false;
    }

    wl_buffer_add_listener(buffer, &buffer_listener, NULL);

    wl_surface_attach(output->surface, buffer, 0, 0);
    wl_surface_damage_buffer(output->surface, 0, 0, INT32_MAX, INT32_MAX); // Damage entire buffer
    wl_surface_commit(output->surface);

    return true;
}

static bool setup_timer(long interval_ns) {
    timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timer_fd == -1) {
        perror("timerfd_create failed");
        return false;
    }

    if (interval_ns <= 0) {
        fprintf(stderr, "Invalid timer interval: %ld ns. Must be positive.\n", interval_ns);
        close(timer_fd);
        timer_fd = -1;
        errno = EINVAL;
        return false;
    }

    struct itimerspec timer_spec = {0};
    const long NSEC_PER_SEC = 1000000000L;
    timer_spec.it_interval.tv_sec = interval_ns / NSEC_PER_SEC;
    timer_spec.it_interval.tv_nsec = interval_ns % NSEC_PER_SEC;
    timer_spec.it_value = timer_spec.it_interval; // Start immediately

    // Ensure the interval is not zero if input was very small but positive
    if (timer_spec.it_interval.tv_sec == 0 && timer_spec.it_interval.tv_nsec == 0) {
         timer_spec.it_interval.tv_nsec = 1; // Set to minimum possible interval
         timer_spec.it_value.tv_nsec = 1;
         fprintf(stderr, "Warning: Calculated timer interval was zero, adjusted to 1 ns.\n");
    }


    if (timerfd_settime(timer_fd, 0, &timer_spec, NULL) == -1) {
        perror("timerfd_settime failed");
        close(timer_fd); timer_fd = -1;
        return false;
    }
    printf("Animation Timer configured with interval %ld ns (fd: %d)\n", interval_ns, timer_fd);
    return true;
}

static bool setup_inotify(void) {
    if (!use_inotify) return true;

    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    const char *dir_to_watch = NULL;

    if (runtime_dir) {
        dir_to_watch = runtime_dir;
    } else {
        fprintf(stderr, "Warning: XDG_RUNTIME_DIR not set, using /tmp for command file.\n");
        dir_to_watch = "/tmp";
    }

    int written = snprintf(command_file_path, sizeof(command_file_path), "%s/%s", dir_to_watch, COMMAND_FILENAME);
    if (written < 0 || (size_t)written >= sizeof(command_file_path)) {
        fprintf(stderr, "Error: Failed to format command file path or path too long.\n");
        command_file_path[0] = '\0';
        command_dir[0] = '\0';
        command_basename[0] = '\0';
        errno = ENAMETOOLONG; // Or EIO
        return false;
    }

    // Store the directory and basename separately for watch and event filtering
    strncpy(command_dir, dir_to_watch, sizeof(command_dir) - 1);
    command_dir[sizeof(command_dir) - 1] = '\0';
    strncpy(command_basename, COMMAND_FILENAME, sizeof(command_basename) - 1);
    command_basename[sizeof(command_basename) - 1] = '\0';


    inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd < 0) {
        perror("inotify_init1 failed");
        return false;
    }

    // Watch the directory for relevant events
    uint32_t watch_mask = IN_CLOSE_WRITE | IN_MOVED_TO | IN_DELETE | IN_MOVED_FROM | IN_CREATE;
    inotify_watch_descriptor = inotify_add_watch(inotify_fd, command_dir, watch_mask);

    if (inotify_watch_descriptor < 0) {
        perror("inotify_add_watch failed");
        fprintf(stderr, "  (Could not watch directory: %s)\n", command_dir);
        close(inotify_fd); inotify_fd = -1;
        return false;
    }

    printf("Inotify watching directory '%s' for events related to '%s' (fd: %d, wd: %d)\n",
           command_dir, command_basename, inotify_fd, inotify_watch_descriptor);

    // Attempt to read the initial state of the file
    // This will call handle_command_change if the file exists and is readable
    read_command_from_file();

    return true;
}

static bool read_command_from_file(void) {
     if (!use_inotify || command_file_path[0] == '\0') {
         // Inotify not enabled or path not set, treat as no command from file
         handle_command_change(NULL);
         return true; // Indicate attempted read (even if skipped)
     }

     FILE *f = fopen(command_file_path, "r");
     char buffer[IPC_MAX_COMMAND_LEN + 2] = {0}; // +1 for null, +1 for potential newline
     bool file_exists = true;

     if (!f) {
         if (errno == ENOENT) {
             file_exists = false;
             printf("Command file '%s' not found.\n", command_file_path);
         } else {
             perror("fopen command file failed");
             // Don't change state on read error, just report it.
             return false;
         }
     }

     char *command_from_file = NULL;
     if (file_exists) {
         if (fgets(buffer, sizeof(buffer), f)) {
             buffer[strcspn(buffer, "\n\r")] = 0; // Remove trailing newline/CR
             if (buffer[0] != '\0') {
                 command_from_file = buffer; // Point to the content if not empty
             }
             // else: File exists but is empty, command_from_file remains NULL
         } else {
             if (ferror(f)) {
                 perror("fgets from command file failed");
                 // Don't change state on read error
                 fclose(f);
                 return false;
             }
             // else: EOF reached immediately (empty file), command_from_file remains NULL
         }
         fclose(f);
     }

     // Pass the read command (or NULL if file not found/empty/read error handled)
     // to the central handler.
     handle_command_change(command_from_file);

     return true; // Signify read attempt was processed
}


static void init_wayland(void) {
    display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "Failed to connect to Wayland display.\n");
        exit(EXIT_FAILURE);
    }

    registry = wl_display_get_registry(display);
    if (!registry) {
        fprintf(stderr, "Failed to get Wayland registry.\n");
        wl_display_disconnect(display);
        exit(EXIT_FAILURE);
    }
    wl_registry_add_listener(registry, &registry_listener, NULL);

    wl_display_roundtrip(display); // Initial roundtrip to get globals

    if (!compositor || !shm || !layer_shell) {
        fprintf(stderr, "Missing required Wayland globals:\n");
        if (!compositor) fprintf(stderr, " - wl_compositor\n");
        if (!shm) fprintf(stderr, " - wl_shm\n");
        if (!layer_shell) fprintf(stderr, " - zwlr_layer_shell_v1\n");
        cleanup();
        exit(EXIT_FAILURE);
    }
    printf("Required Wayland globals found.\n");

    // Add listeners to outputs found during the first roundtrip
    for (int i = 0; i < n_outputs; i++) {
        if (outputs[i].wl_output) {
             wl_output_add_listener(outputs[i].wl_output, &output_listener, &outputs[i]);
        }
    }

    wl_display_roundtrip(display); // Roundtrip to get output info (modes, scale, etc.)
    printf("Output information received.\n");

    // Create surfaces for each known output
    for (int i = 0; i < n_outputs; i++) {
        struct client_output *output = &outputs[i];
        if (!output->wl_output) continue;

        output->surface = wl_compositor_create_surface(compositor);
        if (!output->surface) {
            fprintf(stderr, "wl_compositor_create_surface failed for output %u\n", output->wl_name);
            // Mark this output as unusable? For now, just skip layer creation.
            continue;
        }
        printf("Created wl_surface for output %u\n", output->wl_name);

        output->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
            layer_shell, output->surface, output->wl_output,
            ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "wallpaper-rs"); // Namespace
        if (!output->layer_surface) {
            fprintf(stderr, "zwlr_layer_shell_v1_get_layer_surface failed for output %u\n", output->wl_name);
            wl_surface_destroy(output->surface); output->surface = NULL;
            continue;
        }
        printf("Created layer_surface for output %u\n", output->wl_name);

        zwlr_layer_surface_v1_add_listener(output->layer_surface, &layer_surface_listener, output);

        zwlr_layer_surface_v1_set_size(output->layer_surface, 0, 0); // Use compositor size
        zwlr_layer_surface_v1_set_anchor(output->layer_surface,
            ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT |
            ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
        zwlr_layer_surface_v1_set_exclusive_zone(output->layer_surface, -1); // No exclusive zone
        zwlr_layer_surface_v1_set_keyboard_interactivity(output->layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);


        // Commit the surface state to trigger configuration
        wl_surface_commit(output->surface);
    }

    printf("Waiting for layer surface configuration...\n");
    wl_display_roundtrip(display); // Wait for configure events
    printf("Wayland initialization sequence complete.\n");
}


static void cleanup(void) {
    printf("Cleaning up resources...\n");

    if (timer_fd >= 0) {
        close(timer_fd);
        timer_fd = -1;
        printf("Closed Timer FD.\n");
    }

    ipc_cleanup(); // Use IPC module's cleanup

    if (inotify_fd >= 0) {
        if (inotify_watch_descriptor >= 0) {
            inotify_rm_watch(inotify_fd, inotify_watch_descriptor);
            inotify_watch_descriptor = -1;
        }
        close(inotify_fd);
        inotify_fd = -1;
        printf("Closed Inotify FD.\n");
    }

    free(current_command_arg);
    current_command_arg = NULL;


    if (outputs) {
        for (int i = 0; i < n_outputs; ++i) {
            if (outputs[i].renderer_state) {
                renderer_cleanup(outputs[i].renderer_state);
                outputs[i].renderer_state = NULL;
            }
            if (outputs[i].layer_surface) {
                zwlr_layer_surface_v1_destroy(outputs[i].layer_surface);
                 outputs[i].layer_surface = NULL;
            }
            if (outputs[i].surface) {
                wl_surface_destroy(outputs[i].surface);
                outputs[i].surface = NULL;
            }
            if (outputs[i].wl_output) {
                 wl_output_release(outputs[i].wl_output);
                 outputs[i].wl_output = NULL;
            }
        }
        free(outputs);
        outputs = NULL;
        n_outputs = 0;
        printf("Cleaned up outputs.\n");
    }

    // Destroy Wayland globals last
    if (layer_shell) { zwlr_layer_shell_v1_destroy(layer_shell); layer_shell = NULL; }
    if (shm) { wl_shm_destroy(shm); shm = NULL; }
    if (compositor) { wl_compositor_destroy(compositor); compositor = NULL; }
    if (registry) { wl_registry_destroy(registry); registry = NULL; }

    if (display) {
        printf("Disconnecting from Wayland display...\n");
        // Consider a final flush, though disconnect should handle it.
        // wl_display_flush(display);
        wl_display_disconnect(display);
        display = NULL;
    }
    printf("Cleanup finished.\n");
}


int main(int argc, char **argv) {
    const char *arg_value = (argc > 1) ? argv[1] : NULL;
    bool command_from_arg = false;

    if (arg_value) {
        printf("Using initial argument: %s\n", arg_value);
        handle_command_change(arg_value); // Set initial state from arg
        command_from_arg = true;
    } else {
         printf("No initial argument provided.\n");
         // Initial command will be set by setup_inotify -> read_command_from_file
         // or default to NULL if file doesn't exist.
    }

    init_wayland();
    if(n_outputs == 0) {
         fprintf(stderr, "No Wayland outputs found or successfully initialized.\n");
         cleanup();
         return EXIT_FAILURE;
    }

    if (!ipc_setup("wallpaper_control.sock", process_ipc_command)) {
        fprintf(stderr, "Warning: Failed to setup IPC socket, continuing without IPC.\n");
    }

    if (!setup_timer(timer_interval_nsec)) {
        fprintf(stderr, "Failed to setup animation timer.\n");
        cleanup();
        return EXIT_FAILURE;
    }

    // Setup inotify AFTER Wayland init but BEFORE renderer init.
    // If no command line arg was given, this reads the initial command from file.
    if (!setup_inotify()) {
        fprintf(stderr, "Warning: Failed to setup inotify, command file watching disabled.\n");
        use_inotify = false;
        // If no arg and inotify failed, ensure initial state is NULL/clear
        if (!command_from_arg) {
             handle_command_change(NULL);
        }
    }

    printf("Initializing renderers for configured outputs...\n");
    int configured_outputs = 0;
    for (int i = 0; i < n_outputs; ++i) {
        struct client_output *output = &outputs[i];
        // Check configured flag set by layer_surface_handle_configure
        if (output->configured && output->width > 0 && output->height > 0) {
             printf("  Initializing renderer for output %u (%dx%d) with command: %s\n",
                    output->wl_name, output->width, output->height, current_command_arg ? current_command_arg : "(null)");
             output->renderer_state = renderer_init(output->width, output->height, current_command_arg);
             if (!output->renderer_state) {
                 fprintf(stderr, "Error: Failed to initialize renderer for output %u\n", output->wl_name);
                 // Mark as not fully usable?
                 output->configured = false; // Consider this output unusable for rendering
             } else {
                 configured_outputs++;
             }
        } else {
             printf("  Skipping renderer init for output %u (not configured or zero size: %dx%d)\n", output->wl_name, output->width, output->height);
             output->renderer_state = NULL;
        }
    }

    if (configured_outputs == 0 && n_outputs > 0) {
        fprintf(stderr, "Error: No outputs were successfully configured with a renderer.\n");
        cleanup();
        return EXIT_FAILURE;
    }


    struct pollfd fds_poll[MAX_POLL_FDS];
    int n_poll_fds = 0;

    int wayland_fd = -1;
    int ipc_listener_fd = -1;

    if(display) {
        wayland_fd = wl_display_get_fd(display);
        if (wayland_fd >= 0 && n_poll_fds < MAX_POLL_FDS) {
            fds_poll[n_poll_fds].fd = wayland_fd;
            fds_poll[n_poll_fds].events = POLLIN;
            fds_poll[n_poll_fds].revents = 0;
            n_poll_fds++;
        } else if (wayland_fd < 0) {
            fprintf(stderr, "Error: Failed to get Wayland display FD.\n");
            cleanup();
            return EXIT_FAILURE;
        } else {
             fprintf(stderr, "Error: Poll array too small for Wayland FD.\n");
             cleanup();
             return EXIT_FAILURE;
        }
    }

    if(use_inotify && inotify_fd >= 0 && n_poll_fds < MAX_POLL_FDS) {
        fds_poll[n_poll_fds].fd = inotify_fd;
        fds_poll[n_poll_fds].events = POLLIN;
        fds_poll[n_poll_fds].revents = 0;
        n_poll_fds++;
    } else if (use_inotify && inotify_fd >= 0) {
         fprintf(stderr, "Warning: Poll array too small for Inotify FD. Disabling file watch.\n");
         use_inotify = false; // Can't poll it
         inotify_rm_watch(inotify_fd, inotify_watch_descriptor);
         close(inotify_fd); inotify_fd = -1; inotify_watch_descriptor = -1;
    }


    if(timer_fd >= 0 && n_poll_fds < MAX_POLL_FDS) {
        fds_poll[n_poll_fds].fd = timer_fd;
        fds_poll[n_poll_fds].events = POLLIN;
        fds_poll[n_poll_fds].revents = 0;
        n_poll_fds++;
    } else if (timer_fd >= 0) {
         fprintf(stderr, "Error: Poll array too small for Timer FD.\n");
         cleanup();
         return EXIT_FAILURE;
    }

    ipc_listener_fd = ipc_get_listener_fd();
    if(ipc_listener_fd >= 0 && n_poll_fds < MAX_POLL_FDS) {
        fds_poll[n_poll_fds].fd = ipc_listener_fd;
        fds_poll[n_poll_fds].events = POLLIN;
        fds_poll[n_poll_fds].revents = 0;
        n_poll_fds++;
    } else if (ipc_listener_fd >= 0) {
         fprintf(stderr, "Warning: Poll array too small for IPC Listener FD. Disabling IPC.\n");
         ipc_cleanup(); // Clean up listener if we can't poll it
         ipc_listener_fd = -1;
    }

    for (int i = n_poll_fds; i < MAX_POLL_FDS; ++i) {
        fds_poll[i].fd = -1;
        fds_poll[i].events = 0;
        fds_poll[i].revents = 0;
    }

    char ipc_read_buffer[IPC_READ_BUFFER_SIZE];
    char inotify_event_buffer[INOTIFY_EVENT_BUF_LEN] __attribute__ ((aligned(__alignof__(struct inotify_event))));


    printf("Starting main event loop with %d initial poll fds...\n", n_poll_fds);
    int loop_count = 0;
    bool running = true;
    bool wayland_read_prepared = false;


    while (running) {

        // Flush before preparing read might be slightly better
        int flush_ret = wl_display_flush(display);
        if (flush_ret < 0 && errno != EAGAIN) {
            perror("wl_display_flush before prepare_read failed");
            if (errno == EPIPE) { // Connection broken
                fprintf(stderr, "Wayland connection lost (EPIPE on flush).\n");
                running = false;
                continue;
            }
            // Other errors might be recoverable, but log them.
        }

        // Prepare Wayland read if no events pending
        if (wl_display_prepare_read(display) == 0) {
             wayland_read_prepared = true;
        } else {
            // Events were already pending, dispatch them now
            wayland_read_prepared = false;
            int dispatch_ret = wl_display_dispatch_pending(display);
            if (dispatch_ret < 0) {
                fprintf(stderr, "wl_display_dispatch_pending() before poll failed.\n");
                running = false;
                continue;
            }
        }

        // Recalculate active client FDs for poll each iteration
        int current_poll_count = 0;
        // Add static FDs first
        for (int i=0; i<n_poll_fds; ++i) {
            if (fds_poll[i].fd == wayland_fd || fds_poll[i].fd == timer_fd || fds_poll[i].fd == inotify_fd || fds_poll[i].fd == ipc_listener_fd) {
                if (fds_poll[i].fd != -1) { // Check if FD is still valid
                     // Ensure events are correct (e.g., Wayland only needs POLLIN if read prepared)
                     fds_poll[i].events = POLLIN;
                     if (fds_poll[i].fd == wayland_fd && !wayland_read_prepared) {
                         fds_poll[i].events = 0; // Don't poll Wayland if not prepared
                     }
                     fds_poll[i].revents = 0;
                     current_poll_count++;
                 }
            }
        }
        // Add active client FDs dynamically
        int client_fds[MAX_IPC_CLIENTS];
        int num_clients = ipc_get_active_clients(client_fds, MAX_IPC_CLIENTS);
        int client_start_index = current_poll_count;

        for(int i=0; i < num_clients && current_poll_count < MAX_POLL_FDS; ++i) {
            fds_poll[current_poll_count].fd = client_fds[i];
            fds_poll[current_poll_count].events = POLLIN;
            fds_poll[current_poll_count].revents = 0;
            current_poll_count++;
        }
         if (num_clients > 0 && current_poll_count == client_start_index) {
             // This means poll array was too small for even static FDs + 1 client
             fprintf(stderr, "Warning: Poll array too small to monitor IPC clients.\n");
         }
         // Mark remaining slots as unused
         for (int i = current_poll_count; i < MAX_POLL_FDS; ++i) {
              fds_poll[i].fd = -1;
         }


        int poll_ret = poll(fds_poll, (nfds_t)current_poll_count, -1);

        if (poll_ret < 0) {
            if (errno == EINTR) continue;
            perror("poll failed");
            if (wayland_read_prepared) wl_display_cancel_read(display);
            running = false;
            continue;
        }

        // Handle Wayland events first if prepared and ready
        if (wayland_read_prepared) {
             bool wayland_ready = false;
             for(int i = 0; i < current_poll_count; ++i) {
                 if (fds_poll[i].fd == wayland_fd && (fds_poll[i].revents & POLLIN)) {
                     wayland_ready = true;
                     break;
                 }
             }

             if (wayland_ready) {
                 int read_ret = wl_display_read_events(display);
                 if (read_ret < 0) {
                     if (errno == EPIPE) {
                         fprintf(stderr, "Wayland connection closed by compositor (EPIPE on read).\n");
                     } else {
                         perror("wl_display_read_events failed");
                     }
                     running = false; // Exit on read error/closure
                     continue;
                 }
                 // Dispatch events read
                 int dispatch_ret = wl_display_dispatch_pending(display);
                  if (dispatch_ret < 0) {
                     fprintf(stderr, "wl_display_dispatch_pending() after read failed.\n");
                     running = false; // Exit on dispatch error
                     continue;
                  }
             } else {
                  // Poll returned, but Wayland FD not ready - cancel prepared read
                  wl_display_cancel_read(display);
             }
             wayland_read_prepared = false; // Reset flag regardless
        } else {
             // Wayland wasn't prepared, but might have had events dispatched before poll.
             // We could dispatch again here, but often it's handled by next loop iteration.
             // wl_display_dispatch_pending(display); // Optional
        }


        loop_count++;

        // Handle other FDs (iterate through the polled set)
        for (int i = 0; i < current_poll_count; ++i) {
            if (fds_poll[i].fd == -1) continue; // Skip inactive slots
            if (fds_poll[i].fd == wayland_fd) continue; // Wayland handled above

            short revents = fds_poll[i].revents;
            int current_fd = fds_poll[i].fd;

            if (revents & (POLLERR | POLLHUP | POLLNVAL)) {
                 printf("Main: Error/Hup/Nval event (0x%x) on fd %d.\n", revents, current_fd);
                 if (current_fd == ipc_listener_fd) {
                     fprintf(stderr, "Error on IPC listener socket, disabling IPC.\n");
                     ipc_cleanup(); // Attempt cleanup
                     ipc_listener_fd = -1;
                     fds_poll[i].fd = -1; // Stop polling it
                 } else if (current_fd == timer_fd) {
                     fprintf(stderr, "Error on Timer FD, disabling timer.\n");
                     close(timer_fd); timer_fd = -1;
                     fds_poll[i].fd = -1;
                 } else if (current_fd == inotify_fd) {
                     fprintf(stderr, "Error on Inotify FD, disabling file watch.\n");
                     use_inotify = false;
                     if (inotify_watch_descriptor >= 0) inotify_rm_watch(inotify_fd, inotify_watch_descriptor);
                     close(inotify_fd); inotify_fd = -1; inotify_watch_descriptor = -1;
                     fds_poll[i].fd = -1;
                 } else { // Assume IPC client
                     printf("Closing client fd %d due to HUP/ERR/NVAL.\n", current_fd);
                     ipc_close_client(current_fd);
                     // Client FD removed from poll set automatically on next loop iteration
                     // because ipc_get_client_fds won't return it.
                     // Mark current poll slot inactive just in case.
                     fds_poll[i].fd = -1;
                 }
                 continue;
            }

            if (revents & POLLIN) {
                if (current_fd == ipc_listener_fd) {
                    int new_client_fd = ipc_accept_client();
                    if (new_client_fd != -1) {
                        printf("Main: Accepted new client fd %d\n", new_client_fd);
                        // New client will be added to poll set on next loop iteration
                    }
                    // else: Accept failed or would block, ignore for now.

                } else if (current_fd == timer_fd) {
                    uint64_t expirations;
                    ssize_t n_read = read(timer_fd, &expirations, sizeof(expirations));

                    if (n_read == sizeof(expirations)) {
                        if (animation_enabled) {
                            struct timespec current_time_ts; clock_gettime(CLOCK_MONOTONIC, &current_time_ts);
                            uint32_t current_time_ms = (uint32_t)(((uint64_t)current_time_ts.tv_sec * 1000) + ((uint64_t)current_time_ts.tv_nsec / 1000000));
                            for (int out_idx = 0; out_idx < n_outputs; ++out_idx) {
                                render_and_commit_output(&outputs[out_idx], current_time_ms);
                            }
                        }
                    } else if (n_read == -1 && errno != EAGAIN) {
                        perror("read from timer_fd failed");
                        close(timer_fd); timer_fd = -1;
                        fds_poll[i].fd = -1; // Stop polling
                    }

                } else if (current_fd == inotify_fd) {
                    //printf("[Loop %d] Handling Inotify events...\n", loop_count);
                    ssize_t len = read(inotify_fd, inotify_event_buffer, sizeof(inotify_event_buffer));
                    if (len < 0 && errno != EAGAIN) {
                        perror("read inotify_fd failed");
                         use_inotify = false; // Disable on error
                         if (inotify_watch_descriptor >= 0) inotify_rm_watch(inotify_fd, inotify_watch_descriptor);
                         close(inotify_fd); inotify_fd = -1; inotify_watch_descriptor = -1;
                         fds_poll[i].fd = -1;
                    } else if (len > 0) {
                        const struct inotify_event *event;
                        for (char *ptr = inotify_event_buffer; ptr < inotify_event_buffer + len;
                             ptr += sizeof(struct inotify_event) + event->len) {

                             // Bounds check before casting ptr
                             if ((size_t)(ptr - inotify_event_buffer + sizeof(struct inotify_event)) > (size_t)len) {
                                 fprintf(stderr, "Inotify read alignment error (event header).\n");
                                 break;
                             }

                             event = (const struct inotify_event *) ptr;

                             // Bounds check for name length
                             if (event->len > 0) {
                                if ((size_t)(ptr - inotify_event_buffer + sizeof(struct inotify_event) + event->len) > (size_t)len) {
                                     fprintf(stderr, "Inotify read alignment error (name length).\n");
                                     break;
                                }
                             }

                            // Check if the event is for our specific file
                            if (event->len > 0 && strcmp(event->name, command_basename) == 0) {
                                if (event->mask & (IN_CLOSE_WRITE | IN_MOVED_TO | IN_CREATE)) {
                                    printf("  Command file '%s' changed/created (mask: 0x%x), reading...\n", event->name, event->mask);
                                    read_command_from_file(); // Reads file and calls handle_command_change
                                } else if (event->mask & (IN_DELETE | IN_MOVED_FROM)) {
                                    printf("  Command file '%s' deleted/moved_from (mask: 0x%x).\n", event->name, event->mask);
                                    // Treat deletion as a clear command
                                    handle_command_change(NULL);
                                }
                            }
                        }
                    } // else len == 0 or EAGAIN

                } else { // Must be an IPC client FD
                    ipc_read_result_t read_res = ipc_read_command(current_fd, ipc_read_buffer, sizeof(ipc_read_buffer));

                    switch (read_res) {
                        case IPC_READ_RESULT_SUCCESS:
                            // Callback already handled it.
                            break;
                        case IPC_READ_RESULT_EOF:
                        case IPC_READ_RESULT_ERROR:
                        case IPC_READ_RESULT_INVALID_FD:
                            printf("Main: IPC client fd %d disconnected or error (result: %d).\n", current_fd, read_res);
                            // ipc_read_command should close the fd internally.
                            // The FD will be removed from poll set on next loop.
                            fds_poll[i].fd = -1; // Mark slot as inactive for this iteration
                            break;
                        case IPC_READ_RESULT_WOULD_BLOCK:
                            break; // No data ready
                        case IPC_READ_RESULT_TOO_LONG:
                             fprintf(stderr, "Main: Warning - client fd %d sent command exceeding max length.\n", current_fd);
                             break;
                        case IPC_READ_RESULT_NO_HANDLER:
                             fprintf(stderr, "Main: Error - IPC command received but no handler set (fd %d).\n", current_fd);
                             break;
                        case IPC_READ_RESULT_BUFFER_TOO_SMALL:
                              fprintf(stderr, "Main: Fatal Error - IPC read buffer too small for fd %d. Exiting.\n", current_fd);
                              running = false;
                              break;
                    }
                } // End if/else for FD type
            } // End if (revents & POLLIN)
        } // End for loop polling FDs

    } // End while(running)

    printf("Exiting application.\n");
    cleanup();
    return EXIT_SUCCESS;
}
