// renderer.c (Полная исправленная версия)

#include "renderer.h" // Включаем наш интерфейс

#include <cairo.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>    // Для sin, cos, M_PI, fmod

// --- Константы ---
#define ANIMATION_COMMAND "%SETUP_ANIMATION%"

// --- Режимы рендеринга ---
typedef enum {
    RENDER_DEFAULT,   // Фон по умолчанию (черный)
    RENDER_ANIMATION, // Анимированный градиент
    RENDER_COLOR,     // Сплошной цвет
    RENDER_IMAGE      // Изображение из файла
} RenderMode;

// --- Внутренняя структура состояния ---
struct RendererState {
    // Анимация
    double phase;
    double speed_factor;
    uint64_t last_update_time_ms;

    // Общее состояние
    char *current_arg;       // Текущая команда/аргумент (strdup'нутая)
    RenderMode render_mode; // Текущий режим рендеринга

    // Для цвета
    double color_r, color_g, color_b;

    // Для изображения
    cairo_surface_t *image_surface; // Загруженная картинка (если есть)
    char *last_loaded_image_path; // Путь к загруженной картинке для оптимизации
};

// --- Forward Declarations для статических функций ---
static void renderer_set_mode_from_arg(RendererState* state, const char* arg);
static bool parse_color(const char* color_str, double *r, double *g, double *b);
static bool draw_gradient(cairo_t *cr, int width, int height, double phase);
static bool draw_color(cairo_t *cr, double r, double g, double b);
static bool draw_image(cairo_t *cr, int width, int height, cairo_surface_t *image);
static bool draw_default(cairo_t *cr);


// --- Реализация функций интерфейса ---

RendererState* renderer_init(int width, int height, const char* initial_arg) {
    (void)width; (void)height; // Помечаем неиспользуемые параметры

    RendererState* state = calloc(1, sizeof(RendererState));
    if (!state) {
        perror("Failed to allocate RendererState");
        return NULL;
    }

    printf("CairoRenderer: Initializing state %p.\n", (void*)state);
    // Инициализация полей (calloc уже обнулил)
    state->speed_factor = 1.0;
    state->render_mode = RENDER_DEFAULT;

    // Устанавливаем начальный режим
    renderer_set_mode_from_arg(state, initial_arg);

    return state;
}

void renderer_cleanup(RendererState* state) {
     if (!state) return;
     printf("CairoRenderer: Cleaning up state %p (arg: %s)\n",
            (void*)state, state->current_arg ? state->current_arg : "null");

     free(state->current_arg);
     free(state->last_loaded_image_path);
     if (state->image_surface) {
         cairo_surface_destroy(state->image_surface);
     }
     free(state);
}

bool renderer_handle_command(RendererState* state, const char* command) {
    if (!state) return false;

    printf("CairoRenderer: Handling command: %s\n", command ? command : "(null)");
    bool handled = false;

    // Проверяем, является ли команда командой смены режима
    // (Упрощенная проверка, может дать ложное срабатывание на имя файла '#file' или '/anim')
    bool is_mode_command = (command == NULL ||
                           command[0] == '\0' ||
                           command[0] == '#' ||
                           command[0] == '/' ||
                           strcmp(command, ANIMATION_COMMAND) == 0);

    if (is_mode_command) {
        // Эта функция сама проверит, изменился ли аргумент и обновит режим
        renderer_set_mode_from_arg(state, command);
        handled = true; // Команду смены режима считаем обработанной
    } else {
        // Если не смена режима, проверяем команды управления анимацией,
        // но только если ТЕКУЩИЙ режим - RENDER_ANIMATION.
        if (state->render_mode == RENDER_ANIMATION) {
             if (strcmp(command, "faster") == 0) {
                 state->speed_factor *= 1.2;
                 printf("  -> Speed factor increased to %.2f\n", state->speed_factor);
                 handled = true;
             } else if (strcmp(command, "slower") == 0) {
                 state->speed_factor /= 1.2;
                 if (state->speed_factor < 0.1) state->speed_factor = 0.1;
                 printf("  -> Speed factor decreased to %.2f\n", state->speed_factor);
                 handled = true;
             } else if (strcmp(command, "reset_speed") == 0) {
                 state->speed_factor = 1.0;
                 printf("  -> Speed factor reset to %.2f\n", state->speed_factor);
                 handled = true;
             }
        } // В других режимах команды faster/slower игнорируются
    }

    if (!handled) {
        printf("  -> Unknown command or command ignored in current mode.\n");
    }
    return handled;
}

bool renderer_render_frame(RendererState* state, void* buffer, int width, int height, int stride, uint32_t time_ms) {
     if (!state || !buffer || width <= 0 || height <= 0) {
         fprintf(stderr, "renderer_render_frame: Invalid arguments.\n");
         return false;
     }

     // Обновление фазы анимации, только если активна
     if (state->render_mode == RENDER_ANIMATION) {
         uint32_t delta_time = 0;
         if (state->last_update_time_ms != 0 && time_ms >= state->last_update_time_ms) {
             delta_time = (uint32_t)(time_ms - state->last_update_time_ms);
         } else if (state->last_update_time_ms == 0) {
             delta_time = 16; // Примерно 1/60 сек для первого кадра
         } // else: Время пошло назад, delta_time = 0

         state->last_update_time_ms = time_ms; // Обновляем время *после* расчета дельты

         double time_seconds = delta_time / 1000.0;
         // Ограничиваем максимальный шаг по времени, чтобы избежать рывков после лагов
         if (time_seconds > 0.1) time_seconds = 0.1;

         double base_phase_change_per_second = (2.0 * M_PI) / 10.0; // Полный цикл градиента за 10 секунд
         state->phase += time_seconds * base_phase_change_per_second * state->speed_factor;
         state->phase = fmod(state->phase, 2.0 * M_PI); // Держим фазу в пределах [0, 2*PI)
     }
     // Для статических режимов время не используется

     // Подготовка Cairo
     cairo_surface_t *surf = cairo_image_surface_create_for_data(buffer, CAIRO_FORMAT_ARGB32, width, height, stride);
     if (!surf || cairo_surface_status(surf) != CAIRO_STATUS_SUCCESS) {
         fprintf(stderr, "renderer_render_frame: cairo_image_surface_create_for_data failed: %s\n",
                 cairo_status_to_string(cairo_surface_status(surf)));
         return false;
     }
     cairo_t *cr = cairo_create(surf);
      if (!cr || cairo_status(cr) != CAIRO_STATUS_SUCCESS) {
         fprintf(stderr, "renderer_render_frame: cairo_create failed: %s\n",
                 cairo_status_to_string(cairo_status(cr)));
         cairo_surface_destroy(surf);
         return false;
     }

     // Рисование в зависимости от режима
     bool success = false;
     switch (state->render_mode) {
         case RENDER_ANIMATION:
             success = draw_gradient(cr, width, height, state->phase);
             break;
         case RENDER_COLOR:
             success = draw_color(cr, state->color_r, state->color_g, state->color_b);
             break;
         case RENDER_IMAGE:
             // Рисуем картинку, если она загружена, иначе - фон по умолчанию
             success = state->image_surface ? draw_image(cr, width, height, state->image_surface) : draw_default(cr);
             if (!state->image_surface) fprintf(stderr, "Renderer Warning: Mode is RENDER_IMAGE, but image_surface is NULL!\n");
             break;
         case RENDER_DEFAULT:
         default:
             success = draw_default(cr);
             break;
     }

     // Очистка Cairo
     cairo_destroy(cr);
     // Flush не обязателен перед destroy surface для image surface обертки
     // cairo_surface_flush(surf);
     cairo_surface_destroy(surf); // Уничтожаем обертку Cairo над буфером
     return success;
}


// --- Внутренние вспомогательные функции ---

// Устанавливает режим рендеринга и связанные ресурсы
static void renderer_set_mode_from_arg(RendererState* state, const char* arg) {
    // Проверяем, изменился ли аргумент
    if ((state->current_arg == NULL && arg == NULL) ||
        (state->current_arg != NULL && arg != NULL && strcmp(state->current_arg, arg) == 0)) {
        return; // Аргумент не изменился
    }

    printf("Renderer: Setting mode from argument: %s\n", arg ? arg : "(null)");

    // Аргумент изменился, очищаем старые ресурсы, связанные с предыдущим режимом
    free(state->current_arg); state->current_arg = NULL; // Освобождаем старую строку команды
    free(state->last_loaded_image_path); state->last_loaded_image_path = NULL;
    if (state->image_surface) {
        printf(" -> Destroying previous image surface.\n");
        cairo_surface_destroy(state->image_surface);
        state->image_surface = NULL;
    }
    // Сбрасываем цвет на дефолтный (черный) на всякий случай
    state->color_r = state->color_g = state->color_b = 0.0;

    // Копируем новый аргумент (если он не NULL)
    state->current_arg = arg ? strdup(arg) : NULL;
    if (arg && !state->current_arg) {
        perror("Renderer: strdup failed for new argument");
        state->render_mode = RENDER_DEFAULT; // Откатываемся к дефолту при ошибке памяти
        return;
    }

    // Определяем новый режим и загружаем ресурсы, если нужно
    if (!arg || arg[0] == '\0') {
        printf(" -> Mode: RENDER_DEFAULT (null/empty arg)\n");
        state->render_mode = RENDER_DEFAULT;
    } else if (strcmp(arg, ANIMATION_COMMAND) == 0) {
        printf(" -> Mode: RENDER_ANIMATION\n");
        state->render_mode = RENDER_ANIMATION;
        // Сбрасываем фазу при включении анимации для предсказуемости
        state->phase = 0.0;
        state->last_update_time_ms = 0; // Сброс времени для корректного delta_t первого кадра
    } else if (arg[0] == '#') {
        if (parse_color(arg, &state->color_r, &state->color_g, &state->color_b)) {
             printf(" -> Mode: RENDER_COLOR (%.2f, %.2f, %.2f)\n", state->color_r, state->color_g, state->color_b);
             state->render_mode = RENDER_COLOR;
        } else {
             fprintf(stderr, "Renderer: Invalid color format '%s'. Falling back to default.\n", arg);
             state->render_mode = RENDER_DEFAULT;
             free(state->current_arg); // Освобождаем невалидный аргумент
             state->current_arg = NULL;
        }
    } else {
        // Пытаемся загрузить как изображение (пока только PNG)
        printf(" -> Attempting to load image: %s\n", arg);
        cairo_surface_t *loaded_image = cairo_image_surface_create_from_png(arg);
        cairo_status_t status = cairo_surface_status(loaded_image);

        if (status == CAIRO_STATUS_SUCCESS) {
            printf(" -> Mode: RENDER_IMAGE (Loaded successfully)\n");
            state->render_mode = RENDER_IMAGE;
            state->image_surface = loaded_image; // Сохраняем
            state->last_loaded_image_path = strdup(arg); // Запоминаем путь (ошибка не критична)
            if (!state->last_loaded_image_path) perror("strdup failed for image path");
        } else {
             fprintf(stderr, "Renderer: Failed to load PNG '%s' (status: %s). Falling back to default.\n",
                     arg, cairo_status_to_string(status));
             if (loaded_image) cairo_surface_destroy(loaded_image); // Освобождаем, если создалась с ошибкой
             state->render_mode = RENDER_DEFAULT;
             free(state->current_arg); // Освобождаем невалидный путь
             state->current_arg = NULL;
        }
    }
     printf(" -> New mode set: %d\n", state->render_mode);
}

// Парсит цвет вида #RGB или #RRGGBB
static bool parse_color(const char* color_str, double *r_out, double *g_out, double *b_out) {
    if (!color_str || color_str[0] != '#') return false;
    unsigned int r, g, b;
    size_t len = strlen(color_str); // Используем size_t для длины

    if (len == 7) { // #RRGGBB
        if (sscanf(color_str, "#%02x%02x%02x", &r, &g, &b) == 3) {
            // Проверка диапазона не нужна, %02x читает байт
            *r_out = r / 255.0;
            *g_out = g / 255.0;
            *b_out = b / 255.0;
            return true;
        }
    } else if (len == 4) { // #RGB
        if (sscanf(color_str, "#%1x%1x%1x", &r, &g, &b) == 3) {
            // r,g,b будут в диапазоне 0-15
            *r_out = (r << 4 | r) / 255.0; // Эквивалентно r*17 / 255.0
            *g_out = (g << 4 | g) / 255.0;
            *b_out = (b << 4 | b) / 255.0;
            return true;
        }
    }
    fprintf(stderr, "Color parse error: Unknown format '%s'\n", color_str);
    return false;
}

// Рисует градиент
static bool draw_gradient(cairo_t *cr, int width, int height, double phase) {
    cairo_pattern_t *pat = cairo_pattern_create_linear(0.0, 0.0, 0.0, (double)height);
    if (!pat || cairo_pattern_status(pat) != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "draw_gradient: cairo_pattern_create_linear failed\n");
        return false;
    }

    double r1 = 0.5 + 0.5 * sin(phase);
    double g1 = 0.1;
    double b1 = 0.5 + 0.5 * cos(phase);
    cairo_pattern_add_color_stop_rgba(pat, 0.0, r1, g1, b1, 1.0);

    double phase2 = phase + M_PI * 0.8;
    double r2 = 0.5 + 0.5 * sin(phase2);
    double g2 = 0.5 + 0.5 * cos(phase2);
    double b2 = 0.1;
    cairo_pattern_add_color_stop_rgba(pat, 1.0, r2, g2, b2, 1.0);

    cairo_rectangle(cr, 0, 0, width, height); // Указываем область явно
    cairo_set_source(cr, pat);
    cairo_fill(cr); // Заполняем прямоугольник
    cairo_pattern_destroy(pat);

    return cairo_status(cr) == CAIRO_STATUS_SUCCESS;
}

// Рисует сплошной цвет
static bool draw_color(cairo_t *cr, double r, double g, double b) {
    cairo_set_source_rgb(cr, r, g, b);
    cairo_paint(cr); // paint заливает всю clip region (по умолчанию всю поверхность)
    return cairo_status(cr) == CAIRO_STATUS_SUCCESS;
}

// Рисует изображение с масштабированием для заполнения
static bool draw_image(cairo_t *cr, int width, int height, cairo_surface_t *image) {
    if (!image || cairo_surface_status(image) != CAIRO_STATUS_SUCCESS || width <= 0 || height <= 0) {
        fprintf(stderr, "draw_image: Invalid arguments or image surface.\n");
        return false;
    }

    int img_width = cairo_image_surface_get_width(image);
    int img_height = cairo_image_surface_get_height(image);
    if (img_width <= 0 || img_height <= 0) {
        fprintf(stderr, "draw_image: Image surface has invalid dimensions (%dx%d).\n", img_width, img_height);
        return false;
    }

    // Рассчитываем масштаб для заполнения (cover)
    double scale_x = (double)width / img_width;
    double scale_y = (double)height / img_height;
    double scale = fmax(scale_x, scale_y); // Чтобы покрыть всю область

    // Рассчитываем смещение для центрирования
    double dx = (width - img_width * scale) / 2.0;
    double dy = (height - img_height * scale) / 2.0;

    cairo_save(cr); // Сохраняем матрицу

    // Заливаем фон на случай прозрачности PNG (можно выбрать другой цвет)
    // cairo_set_source_rgb(cr, 0, 0, 0);
    // cairo_paint(cr);

    // Трансформируем систему координат
    cairo_translate(cr, dx, dy);
    cairo_scale(cr, scale, scale);

    // Рисуем картинку
    cairo_set_source_surface(cr, image, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR); // Или CAIRO_FILTER_GOOD

    // Важно: Заливаем всю поверхность (width x height в исходной системе координат),
    // используя отмасштабированную картинку как текстуру.
    // Перед этим нужно сбросить трансформацию, чтобы paint работал на всю область.
    // Поэтому используем cairo_mask вместо cairo_paint.
    // Или проще: установить clip region перед трансформацией.
    // Самый простой способ: Рисуем прямоугольник нужного размера, используя surface как pattern.
    cairo_rectangle(cr, 0, 0, img_width, img_height); // Прямоугольник в масштабированной системе
    cairo_fill(cr);

    // -- Альтернатива с cairo_paint (менее точное позиционирование при cover): --
    // cairo_paint(cr); // Это зальет всю ПОВЕРХНОСТЬ, используя трансформированный источник

    cairo_restore(cr); // Восстанавливаем матрицу

    return cairo_status(cr) == CAIRO_STATUS_SUCCESS;
}

// Рисует фон по умолчанию (черный)
static bool draw_default(cairo_t *cr) {
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0); // Черный
    cairo_paint(cr);
    return cairo_status(cr) == CAIRO_STATUS_SUCCESS;
}
