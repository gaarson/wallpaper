#include "output.h"
#include "wayland_setup.h" // Нужны глобальные Wayland объекты (display, compositor, etc.)
#include "buffer.h"       // Нужны функции для работы с буферами
#include <assert.h>       // Для assert

// --- Глобальные переменные ---
struct client_output *outputs_list_head = NULL;
int n_outputs = 0;

// --- Статические прототипы (вспомогательные функции) ---
static void initialize_output_struct(struct client_output *output, struct wl_output *wl_out, uint32_t name);
static void destroy_output_resources(struct client_output *output);
static double calculate_current_scale(const struct client_output *output);
static void create_wayland_output_objects(struct client_output *output);

// Объявления для обработчиков wl_output, которые были лямбдами
static void output_handle_geometry_impl(void *data, struct wl_output *o, int32_t x, int32_t y, int32_t w, int32_t h, int32_t sub, const char *mk, const char *md, int32_t tr);
static void output_handle_mode_impl(void *data, struct wl_output *o, uint32_t fl, int32_t w, int32_t h, int32_t re);
static void output_handle_done_impl(void *data, struct wl_output *o);

// --- Управление списком выходов ---

struct client_output* find_output_by_wl_name(uint32_t name) {
    for (struct client_output *o = outputs_list_head; o; o = o->next) {
        if (o->wl_name == name) {
            return o;
        }
    }
    return NULL;
}

struct client_output* find_output_by_wl_output(struct wl_output *wl_out) {
     for (struct client_output *o = outputs_list_head; o; o = o->next) {
        if (o->wl_output == wl_out) {
            return o;
        }
    }
    return NULL;
}


static void initialize_output_struct(struct client_output *output, struct wl_output *wl_out, uint32_t name) {
     memset(output, 0, sizeof(*output));
     output->wl_output = wl_out;
     output->wl_name = name;
     output->scale_factor = 1; // Стандартное значение по умолчанию
     output->fractional_scale = 0.0; // Нет информации о дробном масштабе
     output->scale_received = false;
     output->configured = false;
     output->current_buffer_index = -1;
     output->buffers[0].fd = -1; // Инициализация дескрипторов
     output->buffers[1].fd = -1;
     output->buffers[0].wl_buffer = NULL;
     output->buffers[1].wl_buffer = NULL;
     output->buffers[0].map = NULL;
     output->buffers[1].map = NULL;
}

struct client_output* add_output(struct wl_output *wl_out, uint32_t name) {
    if (!wl_out) return NULL;
    if (find_output_by_wl_output(wl_out)) {
        fprintf(stderr, "Warning: Attempted to add already existing output (wl_output: %p, name: %u)\n", (void*)wl_out, name);
        return find_output_by_wl_output(wl_out); // Возвращаем существующий
    }

    struct client_output *new_output = malloc(sizeof(*new_output));
    if (!new_output) {
        perror("Failed to allocate memory for new output");
        return NULL;
    }

    initialize_output_struct(new_output, wl_out, name);

    // Добавляем в начало списка
    new_output->next = outputs_list_head;
    new_output->prev = NULL;
    if (outputs_list_head) {
        outputs_list_head->prev = new_output;
    }
    outputs_list_head = new_output;
    n_outputs++;

    printf("Output added: %u (Total: %d)\n", name, n_outputs);

    // Добавляем листенер к wl_output
    // Статические функции-обработчики для geometry, mode, done
    static const struct wl_output_listener output_listener = {
        .geometry = output_handle_geometry_impl, // Указатель на функцию
        .mode = output_handle_mode_impl,       // Указатель на функцию
        .done = output_handle_done_impl,         // Указатель на функцию
        .scale = output_handle_scale,            // Эти уже были функциями
        .name = output_handle_name,
        .description = output_handle_description // И эта
    };
    wl_output_add_listener(new_output->wl_output, &output_listener, new_output);

    // Создаем surface и layer surface
    create_wayland_output_objects(new_output);

    return new_output;
}

static void output_handle_geometry_impl(void *data, struct wl_output *o, int32_t x, int32_t y, int32_t w, int32_t h, int32_t sub, const char *mk, const char *md, int32_t tr) {
    UNUSED(data); UNUSED(o); UNUSED(x); UNUSED(y); UNUSED(w); UNUSED(h); UNUSED(sub); UNUSED(mk); UNUSED(md); UNUSED(tr);
    // struct client_output* output = data;
    // printf("Output %u Geometry: pos(%d,%d) phys(%dmm,%dmm) subpixel(%d) make(%s) model(%s) transform(%d)\n",
    //       output->wl_name, x, y, w, h, sub, mk, md, tr); // Можно раскомментировать для отладки
}

static void output_handle_mode_impl(void *data, struct wl_output *o, uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
    UNUSED(data); UNUSED(o); UNUSED(flags); UNUSED(width); UNUSED(height); UNUSED(refresh);
    // struct client_output* output = data;
    // printf("Output %u Mode: %dx%d @ %.3f Hz (flags 0x%x - %s %s)\n",
    //       output->wl_name, width, height, (float)refresh / 1000.0, flags,
    //       (flags & WL_OUTPUT_MODE_CURRENT) ? "current" : "",
    //       (flags & WL_OUTPUT_MODE_PREFERRED) ? "preferred" : ""); // Можно раскомментировать для отладки
}

static void output_handle_done_impl(void *data, struct wl_output *o) {
    UNUSED(data); UNUSED(o);
    // struct client_output* output = data;
    // printf("Output %u Done\n", output->wl_name); // Вызывается после отправки всех событий geometry/mode/scale
}


// Освобождает ресурсы, связанные с одним выходом
static void destroy_output_resources(struct client_output *output) {
     printf("Destroying resources for Output %u\n", output->wl_name);
     output->configured = false; // Предотвращаем рендеринг во время очистки

     if (output->renderer_state) {
         renderer_cleanup(output->renderer_state);
         output->renderer_state = NULL;
         printf("  -> Renderer cleaned up.\n");
     }
     if (output->layer_surface) {
         zwlr_layer_surface_v1_destroy(output->layer_surface);
         output->layer_surface = NULL;
          printf("  -> Layer surface destroyed.\n");
     }
     if (output->viewport) {
         wp_viewport_destroy(output->viewport);
         output->viewport = NULL;
          printf("  -> Viewport destroyed.\n");
     }
     if (output->fractional_scale_obj) {
         wp_fractional_scale_v1_destroy(output->fractional_scale_obj);
         output->fractional_scale_obj = NULL;
          printf("  -> Fractional scale object destroyed.\n");
     }
     cleanup_output_buffers(output); // Очищаем буферы
     if (output->surface) {
         // Уничтожение surface вызывает layer_surface_handle_closed,
         // если он еще не был вызван. Но мы уже уничтожили layer_surface.
         wl_surface_destroy(output->surface);
         output->surface = NULL;
         printf("  -> Surface destroyed.\n");
     }
     if (output->wl_output) {
         // wl_output_release вместо destroy, так как мы его получили через bind
         wl_output_release(output->wl_output);
         output->wl_output = NULL;
         printf("  -> wl_output released.\n");
     }
}

void remove_output(struct client_output *output) {
    if (!output) return;

    uint32_t name = output->wl_name;
    printf("Removing Output %u...\n", name);

    destroy_output_resources(output);

    // Удаляем из связного списка
    if (output->prev) {
        output->prev->next = output->next;
    } else {
        outputs_list_head = output->next; // Был головой
    }
    if (output->next) {
        output->next->prev = output->prev;
    }

    free(output);
    n_outputs--;
    printf("Output %u removed. (Total: %d)\n", name, n_outputs);
}

void cleanup_all_outputs(void) {
    printf("Cleaning up all outputs...\n");
    while (outputs_list_head) {
        remove_output(outputs_list_head); // remove_output изменяет outputs_list_head
    }
    n_outputs = 0; // На всякий случай
     printf("All outputs cleaned up.\n");
}


// --- Создание Wayland объектов для выхода ---
static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_handle_configure,
    .closed = layer_surface_handle_closed
};

static void create_wayland_output_objects(struct client_output *output) {
    if (!output || !output->wl_output || !g_compositor || !g_layer_shell) {
         fprintf(stderr, "Error: Missing necessary globals or output object to create surfaces for O:%u\n", output ? output->wl_name : 0);
        return;
    }
    if (output->surface) {
        printf("Warning: Surface already exists for O:%u\n", output->wl_name);
        return; // Уже создано
    }

    output->surface = wl_compositor_create_surface(g_compositor);
    if (!output->surface) {
        fprintf(stderr, "Error: wl_compositor_create_surface failed for O:%u\n", output->wl_name);
        return;
    }
    printf("O:%u Surface created.\n", output->wl_name);

    // Создание layer surface
    // ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND - для обоев
    // "wallpaper-app" - namespace приложения
    output->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        g_layer_shell, output->surface, output->wl_output,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "wallpaper-app");

    if (!output->layer_surface) {
        fprintf(stderr, "Error: zwlr_layer_shell_v1_get_layer_surface failed for O:%u\n", output->wl_name);
        wl_surface_destroy(output->surface);
        output->surface = NULL;
        return;
    }
     printf("O:%u Layer surface created.\n", output->wl_name);

    zwlr_layer_surface_v1_add_listener(output->layer_surface, &layer_surface_listener, output);

    // Настройки layer surface для обоев:
    zwlr_layer_surface_v1_set_size(output->layer_surface, 0, 0); // Размер определяется композитором
    // Якорь по всем краям (растянуть на весь экран)
    zwlr_layer_surface_v1_set_anchor(output->layer_surface,
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(output->layer_surface, -1); // Не резервировать место
    zwlr_layer_surface_v1_set_keyboard_interactivity(output->layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE); // Не принимать ввод

    // Важно сделать commit после настройки layer surface, чтобы композитор получил конфигурацию
    wl_surface_commit(output->surface);
     printf("O:%u Initial surface commit done.\n", output->wl_name);
}


// --- Обработчики событий ---

void output_handle_scale(void *data, struct wl_output *o, int32_t factor) {
    struct client_output *out = data;
    UNUSED(o); // wl_output есть в out->wl_output
    if (!out) return;

    printf("Output %u: Received integer scale factor: %d\n", out->wl_name, factor);
    if (factor <= 0) {
        fprintf(stderr, "Warning: Received non-positive scale factor (%d) for O:%u. Using 1.\n", factor, out->wl_name);
        factor = 1;
    }

    if (out->scale_factor != factor) {
        out->scale_factor = factor;
        out->scale_received = true; // Масштаб получен
        // Переинициализируем рендерер, если размеры уже известны
        check_and_reinit_renderer(out);
    }
}

void output_handle_name(void *data, struct wl_output *o, const char *name) {
     struct client_output *out = data; UNUSED(o);
     if (!out) return;
     printf("Output %u Name: %s\n", out->wl_name, name ? name : "<null>");
     // Можно сохранить имя в client_output, если нужно
}

void output_handle_description(void *data, struct wl_output *o, const char *desc) {
     struct client_output *out = data; UNUSED(o);
     if (!out) return;
     printf("Output %u Description: %s\n", out->wl_name, desc ? desc : "<null>");
     // Можно сохранить описание
}

// --- Fractional Scaling Listener ---
static const struct wp_fractional_scale_v1_listener fractional_scale_listener = {
    .preferred_scale = fractional_scale_handle_preferred_scale,
};

void fractional_scale_handle_preferred_scale(void *data,
                                                   struct wp_fractional_scale_v1 *fract_scale,
                                                   uint32_t scale_numerator) {
    struct client_output *output = data;
    if (!output) return;
    UNUSED(fract_scale); // объект есть в output->fractional_scale_obj

    // Спецификация говорит, что scale = scale_numerator / 120.0
    double new_fractional_scale = (double)scale_numerator / 120.0;

    printf("Output %u: Received fractional scale numerator: %u => scale: %.2f\n",
           output->wl_name, scale_numerator, new_fractional_scale);

     if (new_fractional_scale <= 0) {
        fprintf(stderr, "Warning: Received non-positive fractional scale (%.2f) for O:%u. Ignoring.\n", new_fractional_scale, output->wl_name);
        // Возможно, стоит использовать целочисленный масштаб как fallback?
        // new_fractional_scale = output->scale_factor > 0 ? (double)output->scale_factor : 1.0;
        return; // Игнорируем некорректное значение
    }


    if (output->fractional_scale != new_fractional_scale) {
         output->fractional_scale = new_fractional_scale;
         output->scale_received = true; // Масштаб получен
        // Переинициализируем рендерер
         check_and_reinit_renderer(output);
    }
}


// --- Layer Surface Listeners ---

void layer_surface_handle_configure(void *data, struct zwlr_layer_surface_v1 *ls, uint32_t serial, uint32_t width, uint32_t height) {
    struct client_output *output = data;
    UNUSED(ls); // Объект есть в output->layer_surface
    if (!output) return;

    printf("Layer surface configure O:%u Serial:%u Logical Size: %ux%u\n",
           output->wl_name, serial, width, height);

     if (width == 0 || height == 0) {
         fprintf(stderr, "Warning: Received configure event with zero dimensions for O:%u. Waiting for valid size.\n", output->wl_name);
         // Не подтверждаем конфигурацию с нулевыми размерами, ожидаем следующей
         // Хотя, возможно, стоит подтвердить и ничего не рендерить? Зависит от композитора.
         // Пока подтверждаем, но не устанавливаем configured=true
         zwlr_layer_surface_v1_ack_configure(output->layer_surface, serial);
         return;
     }

    // Сохраняем логические размеры
    output->logical_width = (int)width;
    output->logical_height = (int)height;
    output->configured = true; // Теперь у нас есть размеры

    // Подтверждаем получение конфигурации
    zwlr_layer_surface_v1_ack_configure(output->layer_surface, serial);

    // Если есть менеджер дробного масштабирования и мы еще не получили объект для этой surface
    if (g_fractional_scale_manager && !output->fractional_scale_obj) {
        output->fractional_scale_obj = wp_fractional_scale_manager_v1_get_fractional_scale(
            g_fractional_scale_manager, output->surface);

        if (output->fractional_scale_obj) {
            wp_fractional_scale_v1_add_listener(output->fractional_scale_obj, &fractional_scale_listener, output);
            printf("O:%u Added fractional scale listener.\n", output->wl_name);
             // Может потребоваться roundtrip, чтобы получить начальное значение масштаба
             // wl_display_roundtrip(g_display); // Делать roundtrip здесь может быть опасно (рекурсия)
             // Лучше положиться на то, что событие preferred_scale придет позже.
        } else {
            fprintf(stderr, "O:%u Failed to get fractional_scale object for surface. Fractional scaling might not work.\n", output->wl_name);
            // Если не удалось получить объект, считаем, что дробного масштаба нет
            output->fractional_scale = 0.0;
        }
    } else if (!g_fractional_scale_manager) {
         // Если менеджера нет в принципе, дробного масштаба точно нет
         output->fractional_scale = 0.0;
         // Устанавливаем scale_received, если есть целочисленный масштаб
         if (output->scale_factor > 0) output->scale_received = true;
    }

    // Проверяем, нужно ли пересоздавать рендерер/буферы
    check_and_reinit_renderer(output);
}


void layer_surface_handle_closed(void *data, struct zwlr_layer_surface_v1 *ls) {
    struct client_output *output = data;
    UNUSED(ls);
    if (!output) return;

    fprintf(stderr, "Layer surface closed for O:%u. Cleaning up associated resources.\n", output->wl_name);
    // Композитор уничтожил layer_surface, нам нужно почистить все связанные с ним ресурсы этого выхода.
    // Вызов remove_output сделает всю грязную работу.
    // Важно: нельзя использовать output после вызова remove_output!
    remove_output(output);
}

// --- Очистка Буферов для Выхода ---
void cleanup_output_buffers(struct client_output *output) {
    if (!output) return;
    bool cleaned = false;
    for (int i = 0; i < 2; ++i) {
        if (output->buffers[i].fd >= 0 || output->buffers[i].wl_buffer) {
            cleanup_buffer_pool_entry(&output->buffers[i]);
            cleaned = true;
        }
    }
    output->current_buffer_index = -1;
    if (cleaned) printf("Output %u: Cleaned up buffer pool.\n", output->wl_name);
}

// --- Расчет Актуального Масштаба ---
static double calculate_current_scale(const struct client_output *output) {
    // Приоритет у дробного масштаба, если он валидный (> 0)
    if (output->fractional_scale > 0) {
        return output->fractional_scale;
    }
    // Иначе используем целочисленный масштаб, если он валидный (> 0)
    if (output->scale_factor > 0) {
        return (double)output->scale_factor;
    }
    // Fallback на 1.0, если ни один масштаб не получен или оба некорректны
    return 1.0;
}


// --- Функция Проверки и Переинициализации Рендерера и Буферов ---
// Эта функция вызывается при изменении логического размера (configure),
// изменении масштаба (wl_output.scale, fractional_scale.preferred_scale)
// или при первоначальной настройке.
void check_and_reinit_renderer(struct client_output *output) {
    if (!output) return;

    // Не можем ничего делать, пока не получена конфигурация (логические размеры)
    // и хотя бы какое-то значение масштаба (иначе физический размер не рассчитать)
    if (!output->configured || !output->scale_received || output->logical_width <= 0 || output->logical_height <= 0) {
        // printf("O:%u Skipping reinit: configured=%d, scale_received=%d, logical_w=%d, logical_h=%d\n",
        //        output->wl_name, output->configured, output->scale_received, output->logical_width, output->logical_height);
        return;
    }

    double scale = calculate_current_scale(output);
    // Округляем до ближайшего целого, как рекомендует протокол wayland
    int new_physical_width = (int)round(output->logical_width * scale);
    int new_physical_height = (int)round(output->logical_height * scale);

     if (new_physical_width <= 0 || new_physical_height <= 0) {
         fprintf(stderr, "Error: O:%u Calculated invalid physical size %dx%d (logical %dx%d, scale %.2f). Aborting reinit.\n",
                 output->wl_name, new_physical_width, new_physical_height,
                 output->logical_width, output->logical_height, scale);
         return;
     }

    bool size_changed = (output->physical_width != new_physical_width || output->physical_height != new_physical_height);
    bool need_renderer_reinit = size_changed || !output->renderer_state; // Пересоздаем рендерер при смене размера или если его нет

    // 1. Пересоздание буферов, если изменился физический размер
    if (size_changed) {
        printf("O:%u Physical size changed (%dx%d -> %dx%d). Recreating buffer pool...\n",
               output->wl_name, output->physical_width, output->physical_height, new_physical_width, new_physical_height);

        cleanup_output_buffers(output); // Очистить старый пул буферов

        cairo_format_t format = CAIRO_FORMAT_ARGB32; // Формат, с которым будет работать рендерер

        bool pool_ok = true;
        for (int i = 0; i < 2; ++i) { // Создать два новых буфера
            if (!create_buffer_pool_entry(&output->buffers[i], g_shm, new_physical_width, new_physical_height, format)) {
                 fprintf(stderr, "Error: O:%u Failed to create buffer pool entry %d.\n", output->wl_name, i);
                 pool_ok = false;
                 break;
            }
        }

        if (pool_ok) {
             printf("O:%u New buffer pool created (2x %dx%d, stride %d, size %zu bytes each).\n",
                   output->wl_name, output->buffers[0].width, output->buffers[0].height,
                   output->buffers[0].stride, output->buffers[0].size);
             // Обновляем физические размеры *после* успешного создания буферов
            output->physical_width = new_physical_width;
            output->physical_height = new_physical_height;
        } else {
            fprintf(stderr, "Error: O:%u Failed to recreate buffer pool. Cleaning up partially created buffers.\n", output->wl_name);
            cleanup_output_buffers(output); // Очищаем то, что успели создать
            // Сбрасываем физические размеры, так как буферов под них нет
            output->physical_width = 0;
            output->physical_height = 0;
            // Сбрасываем renderer_state, так как он не соответствует текущему состоянию
            if (output->renderer_state) {
                renderer_cleanup(output->renderer_state);
                output->renderer_state = NULL;
            }
            return; // Не можем продолжать без буферов
        }
    }

    // 2. Пересоздание рендерера, если нужно
    if (need_renderer_reinit) {
         printf("O:%u Initializing/Re-initializing renderer. Logical:%dx%d Scale:%.2f Physical:%dx%d\n",
               output->wl_name, output->logical_width, output->logical_height, scale,
               output->physical_width, output->physical_height);

        // Очищаем старый рендерер, если он был
        if (output->renderer_state) {
            renderer_cleanup(output->renderer_state);
            output->renderer_state = NULL;
        }

        // Получаем текущую команду из config_monitor для инициализации
        const char *cmd = config_monitor_get_current_command();
        output->renderer_state = renderer_init(output->physical_width, output->physical_height, cmd);

        if (!output->renderer_state) {
            fprintf(stderr, "Error: Failed to initialize renderer for O:%u with size %dx%d\n",
                    output->wl_name, output->physical_width, output->physical_height);
            // Если рендерер не создался, надо бы и буферы почистить, т.к. они бесполезны
            cleanup_output_buffers(output);
             output->physical_width = 0;
             output->physical_height = 0;
             output->configured = false; // Считаем выход неконфигурированным
            return;
        }
         printf("O:%u Renderer initialized successfully.\n", output->wl_name);
         // Запускаем первый рендер сразу после инициализации
         printf("O:%u Triggering initial render after reinit.\n", output->wl_name);
         struct timespec ts;
         clock_gettime(CLOCK_MONOTONIC, &ts);
         uint32_t ms = (uint32_t)(((uint64_t)ts.tv_sec * 1000) + ((uint64_t)ts.tv_nsec / 1000000));
         render_and_commit_output(output, ms);
    }
}


// --- Рендеринг Кадра с Пулом Буферов ---
bool render_and_commit_output(struct client_output *output, uint32_t time_ms) {
    // Проверки на валидность состояния
    if (!output || !output->configured || !output->surface || !output->renderer_state ||
        output->physical_width <= 0 || output->physical_height <= 0 ||
        !output->buffers[0].wl_buffer || !output->buffers[1].wl_buffer) // Проверяем наличие обоих буферов
    {
        // Не печатаем ошибку постоянно, это может быть нормальным состоянием во время инициализации/реконфигурации
        // fprintf(stderr, "Debug: Skipping render for O:%u (state invalid)\n", output ? output->wl_name : 0);
        return false;
    }

    // Выбор следующего свободного буфера (простая стратегия пинг-понг)
    int buffer_idx = -1;
    // Сначала пытаемся взять буфер, который НЕ был последним отправлен
    int next_idx = (output->current_buffer_index + 1) % 2;
    if (!output->buffers[next_idx].busy) {
        buffer_idx = next_idx;
    }
    // Если он занят, проверяем, освободился ли уже тот, что отправляли последним
    // (Это может случиться, если кадры пропускаются)
    else if (output->current_buffer_index != -1 && !output->buffers[output->current_buffer_index].busy) {
        buffer_idx = output->current_buffer_index;
         // printf("O:%u Reusing previous buffer index %d\n", output->wl_name, buffer_idx); // Отладка
    }

    // Если оба буфера заняты композитором, пропускаем кадр
    if (buffer_idx == -1) {
         // printf("O:%u Skipping frame: No free buffer available\n", output->wl_name); // Отладка (может быть шумной)
        return false;
    }

    struct buffer_pool_entry *current_buf = &output->buffers[buffer_idx];

    // Помечаем буфер как занятый НАШИМ кодом (перед передачей композитору)
    // Событие 'release' от композитора снимет этот флаг.
    current_buf->busy = true;

    // Отладочное сообщение о том, какой буфер используется
    // printf("O:%u Rendering into buffer %d (fd: %d)\n", output->wl_name, buffer_idx, current_buf->fd);

    // Рендерим кадр в выбранный буфер
    bool success = renderer_render_frame(output->renderer_state,
                                         current_buf->map,
                                         current_buf->width,
                                         current_buf->height,
                                         current_buf->stride,
                                         time_ms);

    if (!success) {
        fprintf(stderr, "Error: Renderer failed for O:%u\n", output->wl_name);
        // Если рендеринг не удался, освобождаем буфер, чтобы не заблокировать его навсегда
        current_buf->busy = false;
        return false;
    }

    // Настройка Viewport (если доступен viewporter)
    // Это нужно, чтобы композитор правильно масштабировал наш буфер (physical_size)
    // до логического размера surface (logical_size).
    if (g_viewporter) {
        if (!output->viewport) { // Создаем viewport, если его еще нет
            output->viewport = wp_viewporter_get_viewport(g_viewporter, output->surface);
            if (!output->viewport) {
                 fprintf(stderr, "Error: Failed to get viewport for O:%u surface. Scaling may be incorrect.\n", output->wl_name);
            } else {
                 printf("O:%u Viewport created.\n", output->wl_name);
            }
        }
        if (output->viewport) {
            // Устанавливаем размер назначения равным логическому размеру surface
            wp_viewport_set_destination(output->viewport, output->logical_width, output->logical_height);
            // Устанавливать source viewport не нужно, если мы рендерим в буфер точного физического размера
             // wp_viewport_set_source(output->viewport, wl_fixed_from_int(0), wl_fixed_from_int(0),
             //                       wl_fixed_from_int(output->physical_width), wl_fixed_from_int(output->physical_height));
        }
    } else {
        // Предупреждаем один раз, если viewporter недоступен
        static bool vp_warned = false;
        if (!vp_warned) {
            fprintf(stderr, "Warning: wp_viewporter protocol not available! Scaling might be blurry or incorrect.\n");
            vp_warned = true;
        }
    }

    // Прикрепляем отрендеренный буфер к surface
    wl_surface_attach(output->surface, current_buf->wl_buffer, 0, 0);

    // Указываем область повреждения (damage) - весь буфер
    // INT32_MAX используется как "бесконечность" для простоты
    wl_surface_damage_buffer(output->surface, 0, 0, INT32_MAX, INT32_MAX);

    // Отправляем изменения композитору
    wl_surface_commit(output->surface);

    // Запоминаем индекс буфера, который мы только что отправили
    output->current_buffer_index = buffer_idx;

    // На этом этапе буфер передан композитору. Флаг busy снимется,
    // когда придет событие buffer.release.

    return true;
}
