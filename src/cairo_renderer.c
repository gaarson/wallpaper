#include "renderer.h" // Включаем наш интерфейс

#include <cairo.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <time.h> // Для srand(time(NULL))

#include <math.h>    // Для sin, cos, M_PI, fmod, pow, fabs

// --- Константы ---
#define ANIMATION_COMMAND "%SETUP_ANIMATION%"
#define CYBERSPACE_COMMAND "/cyberspace" // Новая команда для киберпространства

// --- Режимы рендеринга ---
typedef enum {
    RENDER_DEFAULT,     // Фон по умолчанию (черный)
    RENDER_ANIMATION,   // Анимированный градиент
    RENDER_COLOR,       // Сплошной цвет
    RENDER_IMAGE,       // Изображение из файла
    RENDER_CYBERSPACE   // Новый режим: Полет в киберпространстве
} RenderMode;

typedef struct {
    double x;       // X координата (0.0 до 1.0 * width)
    double base_y;  // Базовая Y координата в виртуальном пространстве
    double speed;   // Множитель скорости (для параллакса)
    double radius;  // Размер звезды
} Star;

// --- Внутренняя структура состояния ---
struct RendererState {
    // Анимация
    double phase;
    double speed_factor;
    uint64_t last_update_time_ms;

    // Общее состояние
    char *current_arg;
    RenderMode render_mode;

    // Для цвета
    double color_r, color_g, color_b;

    // Для изображения
    cairo_surface_t *image_surface;
    char *last_loaded_image_path;

    // --- НОВОЕ: Для звезд ---
    Star *stars;                // Массив звезд
    int num_stars;              // Количество звезд
    double virtual_sky_height;  // Высота виртуального неба для зацикливания звезд
};
// --- Forward Declarations для статических функций ---
static void renderer_set_mode_from_arg(RendererState* state, const char* arg);
static bool parse_color(const char* color_str, double *r, double *g, double *b);
static bool draw_gradient(cairo_t *cr, int width, int height, double phase);
static bool draw_color(cairo_t *cr, double r, double g, double b);
static bool draw_image(cairo_t *cr, int width, int height, cairo_surface_t *image);
static bool draw_default(cairo_t *cr);
static bool draw_cyberspace(cairo_t *cr, int width, int height, double phase); // Новая функция отрисовки

static void init_stars(RendererState* state, int width, int height); // Новая функция
static void draw_stars(cairo_t *cr, RendererState* state, int width, int height); // Новая функция
// Переименуем draw_cyberspace в draw_retrowave_landscape для ясности
static bool draw_retrowave_landscape(cairo_t *cr, RendererState* state, int width, int height);


// --- Реализация функций интерфейса ---

RendererState* renderer_init(int width, int height, const char* initial_arg) {
    // Инициализация генератора случайных чисел
    srand(time(NULL));

    RendererState* state = calloc(1, sizeof(RendererState));
    if (!state) {
        perror("Failed to allocate RendererState");
        return NULL;
    }

    printf("CairoRenderer: Initializing state %p.\n", (void*)state);
    state->speed_factor = 1.0;
    state->render_mode = RENDER_DEFAULT; // Будет изменено ниже

    // --- НОВОЕ: Инициализация звезд ---
    // Используем width и height для инициализации, хотя они и помечены как неиспользуемые для state в целом
    init_stars(state, width, height);
    // --- Конец нового ---

    renderer_set_mode_from_arg(state, initial_arg);

    return state;
}
static void draw_stars(cairo_t *cr, RendererState* state, int width, int height) {
    if (!state || !state->stars || state->num_stars == 0) return;

    cairo_set_source_rgb(cr, 1.0, 1.0, 1.0); // Белые звезды

    double phase_offset = state->phase; // Используем общую фазу для движения

    for (int i = 0; i < state->num_stars; ++i) {
        Star *star = &state->stars[i];

        // Рассчитываем текущую Y позицию с зацикливанием
        double current_y = fmod(star->base_y - phase_offset * star->speed, state->virtual_sky_height);
        // Коррекция, если fmod вернул отрицательное значение
        if (current_y < 0) {
            current_y += state->virtual_sky_height;
        }

        // Рисуем только звезды, видимые на экране (в верхней части)
        // Горизонт примерно на state->render_mode == RENDER_CYBERSPACE ? 0.35 * height : 0.5 * height;
        // Пусть звезды будут выше ~40% экрана
        if (current_y < height * 0.45) { // Рисуем звезды выше 45% высоты
             // Используем cairo_rectangle для простоты (можно cairo_arc для круглых)
             //cairo_arc(cr, star->x, current_y, star->radius, 0, 2 * M_PI);
             //cairo_fill(cr);
             // Прямоугольник может рендериться быстрее для большого кол-ва звезд
             cairo_rectangle(cr, star->x - star->radius * 0.5, current_y - star->radius * 0.5, star->radius, star->radius);
             cairo_fill(cr);
        }
    }
}

static void init_stars(RendererState* state, int width, int height) {
    state->num_stars = 300; // Количество звезд (можно настроить)
    // Виртуальное небо сделаем в 2 раза выше экрана для плавного зацикливания
    state->virtual_sky_height = height * 2.0;
    state->stars = malloc(state->num_stars * sizeof(Star));
    if (!state->stars) {
        perror("Failed to allocate stars array");
        state->num_stars = 0;
        return;
    }

    for (int i = 0; i < state->num_stars; ++i) {
        state->stars[i].x = (double)rand() / RAND_MAX * width; // Случайный X
        // Случайный Y в пределах виртуальной высоты
        state->stars[i].base_y = (double)rand() / RAND_MAX * state->virtual_sky_height;
        // Случайная скорость для параллакса (медленные - дальше, быстрые - ближе)
        // Умножаем phase на speed, так что меньший speed = медленнее
        state->stars[i].speed = 0.05 + ((double)rand() / RAND_MAX) * 0.15; // от 0.05 до 0.2
        // Случайный радиус (размер)
        state->stars[i].radius = 0.5 + ((double)rand() / RAND_MAX) * 1.0; // от 0.5 до 1.5
    }
    printf("Initialized %d stars.\n", state->num_stars);
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
     // --- НОВОЕ: Очистка звезд ---
     free(state->stars);
     // --- Конец нового ---
     free(state);
}
bool renderer_handle_command(RendererState* state, const char* command) {
    if (!state) return false;

    printf("CairoRenderer: Handling command: %s\n", command ? command : "(null)");
    bool handled = false;

    // Проверяем, является ли команда командой смены режима
    bool is_mode_command = (command == NULL ||
                            command[0] == '\0' ||
                            command[0] == '#' ||
                            command[0] == '/' || // Общий префикс для команд режимов
                            strcmp(command, ANIMATION_COMMAND) == 0);

    if (is_mode_command) {
        // Эта функция сама проверит, изменился ли аргумент и обновит режим
        renderer_set_mode_from_arg(state, command);
        handled = true; // Команду смены режима считаем обработанной
    } else {
        // Если не смена режима, проверяем команды управления анимацией,
        // но только если ТЕКУЩИЙ режим - анимированный (градиент ИЛИ киберпространство).
        if (state->render_mode == RENDER_ANIMATION || state->render_mode == RENDER_CYBERSPACE) { // <-- Изменено: Добавлен RENDER_CYBERSPACE
            if (strcmp(command, "faster") == 0) {
                state->speed_factor *= 1.25; // Увеличим шаг для наглядности
                printf("  -> Speed factor increased to %.2f\n", state->speed_factor);
                handled = true;
            } else if (strcmp(command, "slower") == 0) {
                state->speed_factor /= 1.25;
                // Добавим нижний предел скорости, чтобы не остановиться совсем
                if (state->speed_factor < 0.05) state->speed_factor = 0.05;
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

      // Обновление фазы/позиции анимации, только если режим анимированный
      if (state->render_mode == RENDER_ANIMATION || state->render_mode == RENDER_CYBERSPACE) { // <-- Изменено: Добавлен RENDER_CYBERSPACE
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

          // Рассчитываем изменение фазы/позиции.
          // Для градиента - это угол, для киберпространства - смещение по "глубине".
          // Используем одну переменную phase, но интерпретируем её по-разному.
          // Скорость можно сделать разной для разных режимов, если нужно.
          // Базовая скорость (единиц/пикселей/градусов в секунду)
          double base_speed = 100.0; // Пиксели в секунду для киберпространства (подбирается экспериментально)
          if (state->render_mode == RENDER_ANIMATION) {
               base_speed = (2.0 * M_PI) / 10.0; // Градусы в секунду для градиента
          }

          state->phase += time_seconds * base_speed * state->speed_factor;

          // Ограничение фазы: для градиента - по модулю 2*PI, для киберпространства - можно не ограничивать или ограничивать большим значением
          if (state->render_mode == RENDER_ANIMATION) {
             state->phase = fmod(state->phase, 2.0 * M_PI); // Держим фазу в пределах [0, 2*PI)
          } else if (state->render_mode == RENDER_CYBERSPACE) {
             // Для киберпространства можно использовать fmod для зацикливания горизонтальных линий
             // Например, если шаг сетки по Z = 50:
             // state->phase = fmod(state->phase, 50.0);
             // Или просто позволить фазе расти (но это может привести к потере точности double со временем)
             // Оставим пока без ограничения fmod для киберпространства, его применит draw_cyberspace.
          }
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
          case RENDER_CYBERSPACE: // Или как вы назвали этот режим
              // Передаем state, чтобы иметь доступ к звездам и фазе
              success = draw_retrowave_landscape(cr, state, width, height);
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
        // Сбрасываем фазу и время при включении анимации для предсказуемости
        state->phase = 0.0;
        state->last_update_time_ms = 0;
    } else if (strcmp(arg, CYBERSPACE_COMMAND) == 0) { // <-- Обработка новой команды
        printf(" -> Mode: RENDER_CYBERSPACE\n");
        state->render_mode = RENDER_CYBERSPACE;
        // Сбрасываем фазу и время при включении анимации для предсказуемости
        state->phase = 0.0;
        state->last_update_time_ms = 0;
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

    // Цвета градиента, зависящие от фазы
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

    // Трансформируем систему координат
    cairo_translate(cr, dx, dy);
    cairo_scale(cr, scale, scale);

    // Рисуем картинку
    cairo_set_source_surface(cr, image, 0, 0);
    cairo_pattern_set_filter(cairo_get_source(cr), CAIRO_FILTER_BILINEAR);

    // Рисуем прямоугольник нужного размера, используя surface как pattern.
    cairo_rectangle(cr, 0, 0, img_width, img_height); // Прямоугольник в масштабированной системе
    cairo_fill(cr);

    cairo_restore(cr); // Восстанавливаем матрицу

    return cairo_status(cr) == CAIRO_STATUS_SUCCESS;
}

// Рисует фон по умолчанию (черный)
static bool draw_default(cairo_t *cr) {
    cairo_set_source_rgb(cr, 0.0, 0.0, 0.0); // Черный
    cairo_paint(cr);
    return cairo_status(cr) == CAIRO_STATUS_SUCCESS;
}


static bool draw_retrowave_landscape(cairo_t *cr, RendererState* state, int width, int height) {
    // --- Параметры (остаются прежними) ---
    // Небо
    const double sky_top_r = 0.05, sky_top_g = 0.0, sky_top_b = 0.15;
    const double sky_horizon_r = 0.6, sky_horizon_g = 0.1, sky_horizon_b = 0.4;
    const double horizon_y_ratio = 0.35;

    // Солнце
    const double sun_r = 1.0, sun_g = 0.3, sun_b = 0.2;
    const double sun_outer_r = 0.8, sun_outer_g = 0.1, sun_outer_b = 0.3;
    const double sun_radius_ratio = 0.18;
    const double sun_y_offset = -0.04;

    // Сетка
    const int num_horizontal_lines = 35;
    const int num_vertical_lines = 30; // Это значение теперь используется для определения *плотности* линий
    const double perspective_strength = 0.8;
    const double grid_spacing_z = 40.0;
    const double line_width = 1.5;
    const double fade_exponent = 0.6;
    const double grid_line_r = 0.4, grid_line_g = 0.4, grid_line_b = 1.0;
    const double grid_alt_line_r = 0.1, grid_alt_line_g = 0.9, grid_alt_line_b = 1.0;

    // --- Расчеты (остаются прежними) ---
    double horizon_y = height * horizon_y_ratio;
    double vanish_x = width / 2.0;
    double sun_radius = width * sun_radius_ratio;
    double sun_cx = vanish_x;
    double sun_cy = horizon_y + height * sun_y_offset;


    // --- Рисование Слоями ---

    // 1. Небо (Градиент) - без изменений
    cairo_rectangle(cr, 0, 0, width, height);
    cairo_pattern_t *sky_pat = cairo_pattern_create_linear(0, 0, 0, horizon_y * 1.5);
    cairo_pattern_add_color_stop_rgb(sky_pat, 0.0, sky_top_r, sky_top_g, sky_top_b);
    cairo_pattern_add_color_stop_rgb(sky_pat, 1.0, sky_horizon_r, sky_horizon_g, sky_horizon_b);
    cairo_set_source(cr, sky_pat);
    cairo_fill(cr);
    cairo_pattern_destroy(sky_pat);

    // 2. Звезды (Движущиеся) - без изменений
    // Убедитесь, что функция draw_stars существует и вызывается правильно
    // void draw_stars(cairo_t *cr, RendererState* state, int width, int height); // Объявление
    if (state) { // Добавим проверку на NULL state для безопасности
         draw_stars(cr, state, width, height);
    }


    // 3. Солнце (Статичное, с градиентом) - без изменений
    cairo_pattern_t *sun_pat = cairo_pattern_create_radial(sun_cx, sun_cy, sun_radius * 0.1,
                                                          sun_cx, sun_cy, sun_radius);
    cairo_pattern_add_color_stop_rgb(sun_pat, 0.0, sun_r, sun_g, sun_b);
    cairo_pattern_add_color_stop_rgba(sun_pat, 1.0, sun_outer_r, sun_outer_g, sun_outer_b, 0.5);
    cairo_set_source(cr, sun_pat);
    cairo_arc(cr, sun_cx, sun_cy, sun_radius, 0, 2 * M_PI);
    cairo_fill(cr);
    cairo_pattern_destroy(sun_pat);

    // 4. Сетка
    cairo_set_line_width(cr, line_width);
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_BUTT);

    // --- Горизонтальные линии сетки (ИЗМЕНЕНО: рисуем по всей ширине) ---
    for (int i = 0; i < num_horizontal_lines; ++i) {
         // ИЗМЕНЕНО: Используем state->phase напрямую, предполагая что он обновляется корректно
         // для RENDER_CYBERSPACE в renderer_render_frame.
         double phase_offset = (state && state->render_mode == RENDER_CYBERSPACE) ? state->phase : 0.0;
         double effective_z = i * grid_spacing_z - fmod(phase_offset, grid_spacing_z);
         if (effective_z < 0) continue; // Линии "за нами" не рисуем

         // Рассчитываем y на экране
         double max_z = num_horizontal_lines * grid_spacing_z;
         if (max_z <= 0) max_z = 1.0; // Избегаем деления на ноль
         double z_ratio = effective_z / max_z;

         // Ограничиваем z_ratio, чтобы избежать проблем с pow и выходом за пределы
         const double epsilon = 1e-6; // Малое значение для сравнения с горизонтом
         if (z_ratio < epsilon) z_ratio = epsilon;
         // z_ratio может стать > 1 из-за fmod(state->phase...), ограничиваем сверху для y_norm
         // if (z_ratio > 1.0) z_ratio = 1.0; // Убрал это, т.к. линия может быть ниже y=height

         // Используем pow(z_ratio, 0.5) для нелинейного сгущения к горизонту
         double line_y = horizon_y + (height - horizon_y) * pow(z_ratio, 0.5);

         // Рисуем только если линия ниже горизонта и не слишком далеко внизу
         // Увеличим допуск, чтобы линии плавно уходили за нижний край
         if (line_y >= horizon_y && line_y < height * 1.5) { // Допуск 50% ниже экрана
             // y_norm нужен для расчета alpha (затухания)
             double y_norm = (line_y - horizon_y) / (height - horizon_y);
             // Ограничиваем y_norm в диапазоне [0, 1] для корректного alpha
             if (y_norm < 0.0) y_norm = 0.0;
             if (y_norm > 1.0) y_norm = 1.0;

             double alpha = pow(y_norm, fade_exponent); // Затухание как и раньше
             if (alpha > 1.0) alpha = 1.0;

             // Выбираем цвет линии
             if (i % 5 == 0) { // Альтернативный цвет для каждой 5-й линии
                 cairo_set_source_rgba(cr, grid_alt_line_r, grid_alt_line_g, grid_alt_line_b, alpha);
             } else {
                 cairo_set_source_rgba(cr, grid_line_r, grid_line_g, grid_line_b, alpha);
             }

             // ИЗМЕНЕНО: Рисуем линию по всей ширине
             cairo_move_to(cr, 0.0, line_y);
             cairo_line_to(cr, (double)width, line_y);
             cairo_stroke(cr);
         }
    }


    // --- Вертикальные линии сетки (ИЗМЕНЕНО: рисуем до тех пор, пока они не уйдут далеко за экран) ---
    double y_start = height;
    double x_end = vanish_x;
    double y_end = horizon_y;
    int k = 0; // Счетчик линий от центра
    // Безопасный предел, чтобы избежать бесконечного цикла, если что-то пойдет не так
    const int max_lines_to_draw = num_vertical_lines * 20; // Увеличим запас

    if (num_vertical_lines <= 0) {
         fprintf(stderr, "Warning: num_vertical_lines is zero or negative in draw_retrowave_landscape. Skipping vertical lines.\n");
    } else {
        while(k < max_lines_to_draw) {
            k++; // Начинаем с первой линии от центра (k=1)

            // Нормализованная координата 'i' для формулы pow
            // Это определяет, насколько "далеко" эта линия находится от центра в перспективе
            double k_norm = (double)k / num_vertical_lines;

            // Рассчитываем x-смещение внизу экрана (y=height), используя ту же логику перспективы
            // vanish_x здесь выступает как масштабный коэффициент для ширины
            double x_ratio = pow(k_norm, perspective_strength);
            double bottom_x_offset = x_ratio * vanish_x;

            // Координаты старта линий внизу экрана (y=height)
            double x_start_r = vanish_x + bottom_x_offset;
            double x_start_l = vanish_x - bottom_x_offset;

            // ИЗМЕНЕНО: Условие выхода из цикла
            // Прекращаем, если линии начинаются слишком далеко за пределами экрана.
            // Достаточно проверить левую линию: если ее начальная точка левее, чем, скажем, -ширина экрана,
            // то рисовать дальше нет смысла (правая будет симметрично правее 2*ширины).
            if (x_start_l < - (double)width) {
                 // printf("Stopping vertical lines at k=%d, x_start_l=%.2f\n", k, x_start_l); // Для отладки
                 break;
            }

            // Рисуем правую линию с градиентом затухания
            cairo_pattern_t *pat_r = cairo_pattern_create_linear(x_start_r, y_start, x_end, y_end);
            if (pat_r) { // Проверка на случай ошибки создания паттерна
                cairo_pattern_add_color_stop_rgba(pat_r, 0.0, grid_line_r, grid_line_g, grid_line_b, 1.0); // Внизу (непрозрачно)
                cairo_pattern_add_color_stop_rgba(pat_r, 1.0, grid_line_r, grid_line_g, grid_line_b, 0.0); // У горизонта (прозрачно)
                cairo_set_source(cr, pat_r);
                cairo_move_to(cr, x_start_r, y_start);
                cairo_line_to(cr, x_end, y_end);
                cairo_stroke(cr);
                cairo_pattern_destroy(pat_r);
            } else {
                 fprintf(stderr, "Failed to create right vertical line pattern (k=%d)\n", k);
            }

            // Рисуем левую линию с градиентом затухания
            cairo_pattern_t *pat_l = cairo_pattern_create_linear(x_start_l, y_start, x_end, y_end);
             if (pat_l) {
                cairo_pattern_add_color_stop_rgba(pat_l, 0.0, grid_line_r, grid_line_g, grid_line_b, 1.0); // Внизу
                cairo_pattern_add_color_stop_rgba(pat_l, 1.0, grid_line_r, grid_line_g, grid_line_b, 0.0); // У горизонта
                cairo_set_source(cr, pat_l);
                cairo_move_to(cr, x_start_l, y_start);
                cairo_line_to(cr, x_end, y_end);
                cairo_stroke(cr);
                cairo_pattern_destroy(pat_l);
            } else {
                 fprintf(stderr, "Failed to create left vertical line pattern (k=%d)\n", k);
            }
        } // конец while

        // Предупреждение, если вышли по пределу итераций, а не по условию видимости
        if (k >= max_lines_to_draw) {
             fprintf(stderr, "Warning: Vertical line loop hit safety limit (%d lines drawn).\n", k);
        }
    } // конец if (num_vertical_lines > 0)

    // Проверяем статус Cairo перед возвратом
    cairo_status_t status = cairo_status(cr);
    if (status != CAIRO_STATUS_SUCCESS) {
        fprintf(stderr, "Cairo drawing error in draw_retrowave_landscape: %s\n", cairo_status_to_string(status));
        return false;
    }

    return true;
} 
