#include "output.h"
#include <EGL/egl.h> // <-- ДОБАВЛЕНО

#include "src/renderer/core.h"
#include "wayland_setup.h" // Нужны глобальные Wayland объекты (display, compositor, etc.)
// #include "buffer.h"     // БОЛЬШЕ НЕ НУЖЕН
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wayland-egl.h> // Для объявления wl_egl_window_resize


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
      output->renderer_state_gl = NULL; // Рендерер еще не создан

      // Поля для SHM буферов УДАЛЕНЫ
      // output->current_buffer_index = -1;
      // output->buffers[0].fd = -1;
      // output->buffers[1].fd = -1;
      // ... и т.д.
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

    // Создаем surface и layer surface (рендерер создастся позже, при первом configure)
    create_wayland_output_objects(new_output);

    return new_output;
}

static void output_handle_geometry_impl(void *data, struct wl_output *o, int32_t x, int32_t y, int32_t w, int32_t h, int32_t sub, const char *mk, const char *md, int32_t tr) {
    UNUSED(data); UNUSED(o); UNUSED(x); UNUSED(y); UNUSED(w); UNUSED(h); UNUSED(sub); UNUSED(mk); UNUSED(md); UNUSED(tr);
    // Отладочный вывод можно оставить, если нужен
}

static void output_handle_mode_impl(void *data, struct wl_output *o, uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
    UNUSED(data); UNUSED(o); UNUSED(flags); UNUSED(width); UNUSED(height); UNUSED(refresh);
     // Отладочный вывод можно оставить, если нужен
}

static void output_handle_done_impl(void *data, struct wl_output *o) {
    UNUSED(data); UNUSED(o);
     // Отладочный вывод можно оставить, если нужен
}


// Освобождает ресурсы, связанные с одним выходом
static void destroy_output_resources(struct client_output *output) {
      printf("Destroying resources for Output %u\n", output->wl_name);
      output->configured = false; // Предотвращаем рендеринг во время очистки

      // --- Важно: Уничтожить рендерер ПЕРЕД уничтожением surface ---
      if (output->renderer_state_gl) {
          renderer_core_cleanup(output->renderer_state_gl); // Используем новую функцию очистки
          output->renderer_state_gl = NULL;
          printf("  -> Renderer GL cleaned up.\n");
      }
      // --- Убедитесь, что EGL ресурсы (особенно wl_egl_window) уничтожены до wl_surface ---

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

      // cleanup_output_buffers(output); // БОЛЬШЕ НЕ НУЖНО

      // Уничтожение surface должно происходить ПОСЛЕ очистки EGL/Renderer,
      // так как EGL Surface/wl_egl_window зависят от wl_surface.
      if (output->surface) {
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
    // Код создания wl_surface и zwlr_layer_surface_v1 остается ПРЕЖНИМ
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

    // Настройки layer surface (остаются прежними)
    zwlr_layer_surface_v1_set_size(output->layer_surface, 0, 0);
    zwlr_layer_surface_v1_set_anchor(output->layer_surface,
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(output->layer_surface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(output->layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

    // Важно сделать commit после настройки layer surface
    wl_surface_commit(output->surface);
     printf("O:%u Initial surface commit done.\n", output->wl_name);
}


// --- Обработчики событий ---

void output_handle_scale(void *data, struct wl_output *o, int32_t factor) {
    struct client_output *out = data;
    UNUSED(o);
    if (!out) return;

    printf("Output %u: Received integer scale factor: %d\n", out->wl_name, factor);
    if (factor <= 0) {
        fprintf(stderr, "Warning: Received non-positive scale factor (%d) for O:%u. Using 1.\n", factor, out->wl_name);
        factor = 1;
    }

    // Используем != для сравнения int32_t
    if (out->scale_factor != factor) {
        out->scale_factor = factor;
        out->scale_received = true; // Масштаб получен
        // Переинициализируем рендерер, если размеры уже известны
        check_and_reinit_renderer(out); // Вызов этой функции остается
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
    UNUSED(fract_scale);

    double new_fractional_scale = (double)scale_numerator / 120.0;

    printf("Output %u: Received fractional scale numerator: %u => scale: %.2f\n",
           output->wl_name, scale_numerator, new_fractional_scale);

     if (new_fractional_scale <= 0) {
        fprintf(stderr, "Warning: Received non-positive fractional scale (%.2f) for O:%u. Ignoring.\n", new_fractional_scale, output->wl_name);
        return; // Игнорируем некорректное значение
    }

    // Используем сравнение double с небольшой погрешностью или просто != если точность не критична
    if (fabs(output->fractional_scale - new_fractional_scale) > 1e-6) {
         output->fractional_scale = new_fractional_scale;
         output->scale_received = true; // Масштаб получен
         // Переинициализируем рендерер
         check_and_reinit_renderer(output); // Вызов этой функции остается
    }
}


// --- Layer Surface Listeners ---

void layer_surface_handle_configure(void *data, struct zwlr_layer_surface_v1 *ls, uint32_t serial, uint32_t width, uint32_t height) {
    struct client_output *output = data;
    UNUSED(ls);
    if (!output) return;

    printf("Layer surface configure O:%u Serial:%u Logical Size: %ux%u\n",
           output->wl_name, serial, width, height);

     if (width == 0 || height == 0) {
         fprintf(stderr, "Warning: Received configure event with zero dimensions for O:%u. Waiting for valid size.\n", output->wl_name);
         zwlr_layer_surface_v1_ack_configure(output->layer_surface, serial);
         output->configured = false;
         if (output->renderer_state_gl) {
              renderer_core_cleanup(output->renderer_state_gl);
              output->renderer_state_gl = NULL;
         }
         return;
     }

    // Сохраняем логические размеры
    // bool size_changed = ...; // <-- УДАЛЕНО (было неиспользуемым)
    output->logical_width = (int)width;
    output->logical_height = (int)height;
    output->configured = true;

    // Подтверждаем получение конфигурации
    zwlr_layer_surface_v1_ack_configure(output->layer_surface, serial);

    // Получение объекта fractional_scale (остается без изменений)
    if (g_fractional_scale_manager && !output->fractional_scale_obj) {
        output->fractional_scale_obj = wp_fractional_scale_manager_v1_get_fractional_scale(
            g_fractional_scale_manager, output->surface);
        if (output->fractional_scale_obj) {
            wp_fractional_scale_v1_add_listener(output->fractional_scale_obj, &fractional_scale_listener, output);
            printf("O:%u Added fractional scale listener.\n", output->wl_name);
        } else {
            fprintf(stderr, "O:%u Failed to get fractional_scale object. Fractional scaling might not work.\n", output->wl_name);
            output->fractional_scale = 0.0;
        }
    } else if (!g_fractional_scale_manager) {
        output->fractional_scale = 0.0;
        if (output->scale_factor > 0) output->scale_received = true;
    }

    // Проверяем, нужно ли пересоздавать рендерер/буферы
    // Вызываем check_and_reinit_renderer всегда при configure с валидными размерами,
    // он сам решит, нужно ли что-то делать
    check_and_reinit_renderer(output);
}


void layer_surface_handle_closed(void *data, struct zwlr_layer_surface_v1 *ls) {
    // Остается без изменений
    struct client_output *output = data;
    UNUSED(ls);
    if (!output) return;

    fprintf(stderr, "Layer surface closed for O:%u. Cleaning up associated resources.\n", output->wl_name);
    remove_output(output);
}

// --- Очистка Буферов для Выхода ---
// void cleanup_output_buffers(struct client_output *output) {
    // Эта функция БОЛЬШЕ НЕ НУЖНА
// }

// --- Расчет Актуального Масштаба ---
static double calculate_current_scale(const struct client_output *output) {
    // Остается без изменений
    if (output->fractional_scale > 0) {
        return output->fractional_scale;
    }
    if (output->scale_factor > 0) {
        return (double)output->scale_factor;
    }
    return 1.0;
}


// --- Функция Проверки и Переинициализации Рендерера ---
// Теперь эта функция НЕ управляет буферами, а только рендерером.
void check_and_reinit_renderer(struct client_output *output) {
    if (!output || !output->surface) return; // Нужна поверхность для EGL

    // Не можем инициализировать рендерер или изменять размер окна,
    // пока не получена конфигурация (логические размеры)
    // и хотя бы какое-то значение масштаба.
    if (!output->configured || !output->scale_received || output->logical_width <= 0 || output->logical_height <= 0) {
        return;
    }

    double scale = calculate_current_scale(output);
    int physical_width = (int)round(output->logical_width * scale);
    int physical_height = (int)round(output->logical_height * scale);

    // --- Проверяем, нужно ли инициализировать/переинициализировать рендерер ---
    // Пока оставляем простую логику: создаем, если его нет.
    // В будущем можно добавить проверку на значительное изменение размера/масштаба,
    // если полная переинициализация не всегда нужна.
    bool need_full_reinit = !output->renderer_state_gl;

    if (need_full_reinit) {
         printf("O:%u Initializing GL renderer. Logical:%dx%d Scale:%.2f (Physical: %dx%d)\n",
               output->wl_name, output->logical_width, output->logical_height, scale, physical_width, physical_height);

        // Очищаем старый рендерер, если он был (на всякий случай, хотя need_full_reinit=true значит, что его нет)
        if (output->renderer_state_gl) {
            renderer_core_cleanup(output->renderer_state_gl);
            output->renderer_state_gl = NULL;
        }

        // Убедитесь, что g_display инициализирован до этого момента!
        if (!g_display) {
            fprintf(stderr, "Error: O:%u Cannot initialize renderer, g_display is NULL!\n", output->wl_name);
            return;
        }
        const char *cmd = config_monitor_get_current_command();
        output->renderer_state_gl = renderer_core_init(g_display,
                                                    output->surface,
                                                    output->logical_width,
                                                    output->logical_height,
                                                    scale,
                                                    cmd);

        if (!output->renderer_state_gl) {
            fprintf(stderr, "Error: Failed to initialize GL renderer for O:%u\n", output->wl_name);
            output->configured = false;
            return;
        }
        printf("O:%u GL Renderer initialized successfully.\n", output->wl_name);

        if (output->renderer_state_gl) {
            printf("O:%u Resizing EGL window via renderer function to %dx%d\n",
                   output->wl_name, physical_width, physical_height);
            renderer_core_resize(output->renderer_state_gl, physical_width, physical_height, scale);
        }

        // Запускаем первый рендер
        printf("O:%u Triggering initial render after init.\n", output->wl_name);
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint32_t ms = (uint32_t)(((uint64_t)ts.tv_sec * 1000) + ((uint64_t)ts.tv_nsec / 1000000));
        present_output_frame(output, ms);

    } else {
        // --- Рендерер уже существует. Проверяем, нужно ли изменить размер EGL окна ---
        // Это произойдет, если изменились logical_width/height или scale, но полная
        // переинициализация рендерера не потребовалась (need_full_reinit == false).

        // TODO: Оптимизация: хранить предыдущие physical_width/height и вызывать resize только при их изменении.
        //       Для простоты пока вызываем всегда, когда рендерер есть и конфигурация валидна.
        if (output->renderer_state_gl) {
            printf("O:%u Resizing EGL window (update) to %dx%d\n", output->wl_name, physical_width, physical_height);
            renderer_core_resize(output->renderer_state_gl, physical_width, physical_height, scale);

            // После изменения размера окна, возможно, стоит запросить новый кадр,
            // чтобы обновить отрисовку с новыми размерами немедленно.
            // Это зависит от логики вашего основного цикла.
             printf("O:%u Triggering render after EGL window resize.\n", output->wl_name);
             struct timespec ts;
             clock_gettime(CLOCK_MONOTONIC, &ts);
             uint32_t ms = (uint32_t)(((uint64_t)ts.tv_sec * 1000) + ((uint64_t)ts.tv_nsec / 1000000));
             present_output_frame(output, ms);

        } else if (output->renderer_state_gl) {
            // Состояние рендерера есть, но egl_window почему-то нет? Это странно.
             fprintf(stderr, "Warning: O:%u Renderer state exists, but EGL window is NULL during resize check.\n", output->wl_name);
        }
         // Если рендерер уже есть и полная переинициализация не нужна,
         // просто выходим (или вызываем renderer_gl_resize, если бы он был).
         // printf("O:%u Renderer already initialized. Skipping full reinit.\n", output->wl_name);
    }
}


// --- Представление Кадра (OpenGL/EGL) ---
// Заменяет render_and_commit_output
bool present_output_frame(struct client_output *output, uint32_t time_ms) {
    if (!output || !output->configured || !output->surface || !output->renderer_state_gl ||
        output->logical_width <= 0 || output->logical_height <= 0)
    {
        return false;
    }

    RendererCoreState *gl_state = output->renderer_state_gl;

    // 1. Рендерим кадр с помощью OpenGL
    bool success = renderer_core_render_frame(gl_state, time_ms);

    if (!success) {
        fprintf(stderr, "Error: OpenGL Renderer failed rendering frame for O:%u\n", output->wl_name);
        // Не выходим сразу, попробуем сделать swap
    }

    // Настройка Viewport (остается актуальной для Wayland композитора!)
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
             // wp_viewport_set_source не нужен, если EGL surface соответствует физическому размеру
        }
     } else {
         // Предупреждение о недоступности viewporter (остается)
         static bool vp_warned = false;
         if (!vp_warned) {
             fprintf(stderr, "Warning: wp_viewporter protocol not available! Scaling might be blurry or incorrect.\n");
             vp_warned = true;
         }
     }

    // --- Главное изменение: Заменяем attach/damage/commit на eglSwapBuffers ---
    // Делаем eglMakeCurrent перед swap на всякий случай, если контекст мог измениться
    // if (eglMakeCurrent(gl_state->egl_display, gl_state->egl_surface, gl_state->egl_surface, gl_state->egl_context) == EGL_FALSE) {
    //      fprintf(stderr, "RendererGL Error: eglMakeCurrent failed before swap (EGL error: 0x%x)\n", eglGetError());
    //      return false; // Критическая ошибка
    // }

   // 3. Обмен буферов через функцию рендерера
    //    (Внутри renderer_gl_swap_buffers будет вызван eglMakeCurrent перед eglSwapBuffers)
    bool swapped = renderer_core_swap_buffers(gl_state);

    // --- ПРОВЕРЬТЕ, ЧТО СЛЕДУЮЩИЙ БЛОК `if (eglMakeCurrent...` УДАЛЕН ИЗ ВАШЕГО КОДА ---
    /*
    // НЕПРАВИЛЬНО: Этот вызов eglMakeCurrent здесь лишний и вызывает ошибку компиляции
    if (eglMakeCurrent(gl_state->egl_display, gl_state->egl_surface, gl_state->egl_surface, gl_state->egl_context) == EGL_FALSE) {
         fprintf(stderr, "RendererGL Error: eglMakeCurrent failed before swap (EGL error: 0x%x)\n", eglGetError());
         return false; // Критическая ошибка
    }
    */
   // --- КОНЕЦ УДАЛЯЕМОГО БЛОКА ---


    if (!swapped) {
        // Ошибка при обмене буферов
        fprintf(stderr, "Error: Swap buffers failed for O:%u. Attempting reinitialization.\n", output->wl_name);
        output->configured = false;
        if (output->renderer_state_gl) {
            renderer_core_cleanup(output->renderer_state_gl);
            output->renderer_state_gl = NULL;
        }
        return false;
    }

    return true;
}
