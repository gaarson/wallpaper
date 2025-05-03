#define EGL_EGLEXT_PROTOTYPES // Для прототипов EGL расширений
#define GL_GLEXT_PROTOTYPES   // Для прототипов GL расширений (если нужны)

#include "renderer_gl.h"       // Наш новый интерфейс
#include "shader_utils.h"   // Утилиты для шейдеров

#include <EGL/egl.h>
#include <EGL/eglext.h>     // Для EGL Wayland расширений
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>   // Для доп. функций GLES (если нужны)

#include <wayland-client.h>
#include <wayland-egl.h>    // Для wl_egl_window

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>           // Для sin, cos, M_PI, fmod, round, fmax
#include <time.h>           // Для clock_gettime

// Определяем STB_IMAGE_IMPLEMENTATION только в одном .c файле
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"      // Подключаем реализацию stb_image

// --- Константы ---
#define ANIMATION_COMMAND "%SETUP_ANIMATION%"

// --- GLSL Шейдеры (встроены как строки) ---
// - Добавлена передача координат устройства (vDeviceCoord)
// - Явно указана точность highp
const char* vertex_shader_source =
    "#version 100\n"
    "precision highp float;\n" // Явная точность для float
    "precision highp int;\n"   // Явная точность для int
    "\n"
    "attribute vec4 aPosition; // Координаты вершин квада (-1..1)\n"
    "attribute vec2 aTexCoord; // Базовые текстурные координаты (0..1)\n"
    "\n"
    "uniform vec2 uResolution;        // Физическое разрешение экрана (viewport)\n"
    "uniform vec2 uTextureResolution; // Разрешение текстуры\n"
    "\n"
    "varying vec2 vTexCoord;     // Итоговые текстурные координаты для фрагментного шейдера\n"
    "varying vec2 vDeviceCoord;  // Координаты устройства (-1..1) для других шейдеров\n"
    "\n"
    "void main() {\n"
    "    gl_Position = aPosition;\n" // Позиция вершин остается прежней (-1..1)
    "    vDeviceCoord = aPosition.xy; // Передаем координаты устройства\n"
    "\n"
    // --- Расчет текстурных координат для режима 'cover' --- \n"
    // Этот код нужен только когда активен текстурный шейдер, но компилируется всегда. \n"
    // Рассчитываем соотношения сторон \n"
    "    float screenAspect = uResolution.x / uResolution.y;\n"
    "    float textureAspect = uTextureResolution.x / uTextureResolution.y;\n"
    "\n"
    // Вычисляем масштабы для cover-режима \n"
    "    float scaleX = 1.0, scaleY = 1.0;\n"
    "    // Избегаем деления на ноль, если разрешение некорректно \n"
    "    if (uResolution.y > 0.0 && uTextureResolution.y > 0.0 && textureAspect > 0.0) {\n"
    "        if (textureAspect > screenAspect) {\n"
    // Текстура шире экрана -> масштабируем по высоте, обрезаем по ширине \n"
    "            scaleY = 1.0;\n"
    "            scaleX = screenAspect / textureAspect;\n"
    "        } else {\n"
    // Текстура выше экрана (или такое же соотношение) -> масштабируем по ширине, обрезаем по высоте \n"
    "            scaleX = 1.0;\n"
    "            scaleY = textureAspect / screenAspect;\n"
    "        }\n"
    "    }\n"
    "\n"
    // Центрируем текстурные координаты и применяем масштаб \n"
    "    vTexCoord.x = (aTexCoord.x - 0.5) * scaleX + 0.5;\n"
    "    vTexCoord.y = (aTexCoord.y - 0.5) * scaleY + 0.5;\n"
    "}\n";

// Фрагментный шейдер для сплошного цвета (добавлена точность)
const char* fragment_shader_color_source =
    "#version 100\n"
    "precision mediump float; // mediump обычно достаточно для цвета\n"
    "uniform vec3 uColor;     // Цвет RGB (0..1)\n"
    "void main() {\n"
    "    gl_FragColor = vec4(uColor, 1.0);\n"
    "}\n";

// Модифицированный фрагментный шейдер для анимированного градиента:
// - Принимает vDeviceCoord вместо vPos
// - Удалено объявление uResolution
// - Явно указана точность mediump
const char* fragment_shader_gradient_source =
    "#version 100\n"
    "precision mediump float;\n"
    "uniform float uPhase;          // Фаза анимации\n"
    // "uniform vec2 uResolution;" // УБРАНО - не нужно здесь и вызывало конфликт
    "varying vec2 vDeviceCoord;   // ПРИНИМАЕМ координаты устройства (-1..1)\n"
    "void main() {\n"
    // Нормализуем Y координату к 0..1, используя vDeviceCoord
    "    float yNorm = (vDeviceCoord.y + 1.0) * 0.5;\n"
    // Простой градиент на основе фазы и координаты Y \n"
    "    float r1 = 0.5 + 0.5 * sin(uPhase);\n"
    "    float g1 = 0.1;\n"
    "    float b1 = 0.5 + 0.5 * cos(uPhase);\n"
    "    vec3 color1 = vec3(r1, g1, b1);\n"
    "\n"
    "    float phase2 = uPhase + 3.14159 * 0.8;\n"
    "    float r2 = 0.5 + 0.5 * sin(phase2);\n"
    "    float g2 = 0.5 + 0.5 * cos(phase2);\n"
    "    float b2 = 0.1;\n"
    "    vec3 color2 = vec3(r2, g2, b2);\n"
    "\n"
    // Линейная интерполяция между двумя цветами по Y \n"
    "    vec3 finalColor = mix(color1, color2, yNorm);\n"
    "    gl_FragColor = vec4(finalColor, 1.0);\n"
    "}\n";

// Фрагментный шейдер для текстуры (добавлена точность)
// Он правильно использует vTexCoord, передаваемый из вершинного шейдера
const char* fragment_shader_texture_source =
    "#version 100\n"
    "precision mediump float;\n"
    "uniform sampler2D uTexture; // Текстура\n"
    "varying vec2 vTexCoord;     // Текстурные координаты из вершинного шейдера\n"
    "void main() {\n"
    "    gl_FragColor = texture2D(uTexture, vTexCoord);\n"
    "}\n";

// --- Внутренняя структура состояния ---
struct RendererStateGL {
    // EGL Объекты
    EGLDisplay egl_display;
    EGLConfig egl_config;
    EGLContext egl_context;
    EGLSurface egl_surface;
    struct wl_egl_window *egl_window; // Wayland EGL окно

    // Шейдерные программы
    GLuint shader_program_color;
    GLuint shader_program_gradient;
    GLuint shader_program_texture;

    // Расположение Uniform-переменных
    GLint color_uniform_color;
    GLint gradient_uniform_phase;
    GLint gradient_uniform_resolution;
    GLint texture_uniform_texture; // Обычно 0

    // OpenGL Объекты
    GLuint vao; // Vertex Array Object (не обязателен в GLES 2.0, но полезен)
    GLuint vbo; // Vertex Buffer Object для квада
    GLuint texture_id; // ID загруженной текстуры

    // Состояние рендеринга
    RenderMode render_mode;
    char *current_arg; // Текущая команда/аргумент (strdup'нутая)
    int current_logical_width;
    int current_logical_height;
    double current_scale;

    // Анимация
    double phase;
    double speed_factor;
    uint64_t last_update_time_ms;

    // Для цвета
    double color_r, color_g, color_b;

    // Для изображения
    int tex_width, tex_height; // Размеры загруженной текстуры
    char *last_loaded_image_path; // Путь для оптимизации
};

// --- Forward Declarations для статических функций ---
static bool init_egl(RendererStateGL* state, struct wl_display* display, struct wl_surface* surface);
static bool init_gl_resources(RendererStateGL* state);
static void cleanup_gl_resources(RendererStateGL* state);
static void cleanup_egl(RendererStateGL* state);
static void set_render_mode_gl(RendererStateGL* state, const char* arg);
static bool load_texture_gl(RendererStateGL* state, const char* image_path);
static bool parse_color(const char* color_str, double *r, double *g, double *b); // Из старого рендерера
static void update_animation(RendererStateGL* state, uint32_t time_ms);

// --- Реализация функций интерфейса ---

RendererStateGL* renderer_gl_init(struct wl_display* display, struct wl_surface* surface,
                                  int initial_logical_width, int initial_logical_height,
                                  double initial_scale, const char* initial_arg)
{
    printf("RendererGL: Initializing...\n");
    RendererStateGL* state = calloc(1, sizeof(RendererStateGL));
    if (!state) {
        perror("RendererGL: Failed to allocate state");
        return NULL;
    }

    state->texture_id = 0; // Изначально текстуры нет
    state->speed_factor = 1.0;
    state->render_mode = RENDER_DEFAULT;
    state->current_logical_width = initial_logical_width;
    state->current_logical_height = initial_logical_height;
    state->current_scale = initial_scale > 0 ? initial_scale : 1.0;

    // 1. Инициализация EGL
    if (!init_egl(state, display, surface)) {
        fprintf(stderr, "RendererGL: EGL initialization failed.\n");
        free(state);
        return NULL;
    }
    printf("RendererGL: EGL Initialized.\n");

    // 2. Инициализация OpenGL ресурсов (шейдеры, буферы)
    if (!init_gl_resources(state)) {
        fprintf(stderr, "RendererGL: OpenGL resource initialization failed.\n");
        cleanup_egl(state);
        free(state);
        return NULL;
    }
    printf("RendererGL: OpenGL Resources Initialized.\n");

    // 3. Установка начального режима
    set_render_mode_gl(state, initial_arg);

    printf("RendererGL: Initialization complete (state: %p).\n", (void*)state);
    return state;
}

void renderer_gl_cleanup(RendererStateGL* state) {
    if (!state) return;
    printf("RendererGL: Cleaning up state %p (arg: %s)\n",
           (void*)state, state->current_arg ? state->current_arg : "null");

    // Важен порядок: сначала ресурсы OpenGL, потом EGL контекст/surface, потом display
    cleanup_gl_resources(state);
    cleanup_egl(state);

    free(state->current_arg);
    free(state->last_loaded_image_path);
    free(state);
    printf("RendererGL: Cleanup complete.\n");
}

bool renderer_gl_handle_command(RendererStateGL* state, const char* command) {
    if (!state) return false;
    printf("RendererGL: Handling command: %s\n", command ? command : "(null)");

    // Проверяем, является ли команда командой смены РЕЖИМА
    bool is_mode_command = (command == NULL ||
                            command[0] == '\0' ||
                            command[0] == '#' ||
                            command[0] == '/' || // Предполагаем, что пути начинаются с /
                            strcmp(command, ANIMATION_COMMAND) == 0);

    bool handled = false;

    if (is_mode_command) {
        // Это команда смены режима. Проверяем, изменился ли аргумент.
        if (!((state->current_arg == NULL && command == NULL) ||
              (state->current_arg != NULL && command != NULL && strcmp(state->current_arg, command) == 0)))
        {
            // Аргумент режима изменился, устанавливаем новый режим
            set_render_mode_gl(state, command);
            handled = true; // Команду смены режима считаем обработанной
        } else {
            // Аргумент режима не изменился, ничего не делаем
            printf("  -> Mode command ignored (argument unchanged).\n");
            handled = true; // Считаем обработанной, т.к. это команда режима
        }
    } else {
        // Это НЕ команда смены режима. Проверяем команды управления.
        // Команды управления анимацией работают только в режиме RENDER_ANIMATION.
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
            // Здесь можно добавить другие команды управления, не меняющие режим
        }
        // Если режим не RENDER_ANIMATION, команды faster/slower игнорируются
    }

    if (!handled) {
         printf("  -> Command ignored (either unknown or not applicable to current mode).\n");
    }
    return handled; // Возвращаем true, если команда была как-то обработана (даже если проигнорирована из-за режима)
}


bool renderer_gl_render_frame(RendererStateGL* state, int logical_width, int logical_height, double scale, uint32_t time_ms) {
    if (!state || !state->egl_display || !state->egl_context || !state->egl_surface) {
        fprintf(stderr, "RendererGL Error: Invalid EGL state in render_frame.\n");
        return false;
    }
    if (logical_width <= 0 || logical_height <= 0 || scale <= 0) {
         fprintf(stderr, "RendererGL Warning: Invalid dimensions/scale received: %dx%d @ %.2f\n", logical_width, logical_height, scale);
         // Можно пропустить рендеринг или попробовать использовать старые значения
         return true; // Не фатальная ошибка, просто пропускаем кадр
    }

    // Обновляем размеры и масштаб в состоянии, если они изменились
    state->current_logical_width = logical_width;
    state->current_logical_height = logical_height;
    state->current_scale = scale;

    // --- Важно: Сделать EGL контекст текущим ---
    if (eglMakeCurrent(state->egl_display, state->egl_surface, state->egl_surface, state->egl_context) == EGL_FALSE) {
        fprintf(stderr, "RendererGL Error: eglMakeCurrent failed (EGL error: 0x%x)\n", eglGetError());
        return false; // Это критическая ошибка
    }

    // --- Обновить анимацию ---
    update_animation(state, time_ms);

    // --- Установить Viewport ---
    // OpenGL работает с физическими пикселями
    int physical_width = (int)round(logical_width * scale);
    int physical_height = (int)round(logical_height * scale);
    glViewport(0, 0, physical_width, physical_height);

    // --- Очистка или установка фона ---
    // В режимах градиента и текстуры квад заполнит весь экран.
    // В режиме цвета или по умолчанию можно использовать glClear.
    if (state->render_mode == RENDER_COLOR || state->render_mode == RENDER_DEFAULT) {
        double r = state->color_r;
        double g = state->color_g;
        double b = state->color_b;
        if (state->render_mode == RENDER_DEFAULT) {
            r = g = b = 0.0; // Черный фон
        }
        glClearColor((GLfloat)r, (GLfloat)g, (GLfloat)b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        // Для этих режимов дальше ничего рисовать не нужно
        return true; // Успешно очистили буфер
    }

    // --- Выбор шейдера и настройка Uniforms ---
    GLuint current_program = 0;
    switch (state->render_mode) {
        case RENDER_ANIMATION:
            current_program = state->shader_program_gradient;
            glUseProgram(current_program);
            glUniform1f(state->gradient_uniform_phase, (GLfloat)state->phase);
            glUniform2f(state->gradient_uniform_resolution, (GLfloat)physical_width, (GLfloat)physical_height);
            break;
        case RENDER_IMAGE:
             if (state->texture_id != 0 && state->tex_width > 0 && state->tex_height > 0) { // Добавлена проверка размеров текстуры
                current_program = state->shader_program_texture;
                glUseProgram(current_program);
                glActiveTexture(GL_TEXTURE0);
                glBindTexture(GL_TEXTURE_2D, state->texture_id);
                glUniform1i(state->texture_uniform_texture, 0);

                // ---> ПЕРЕДАЕМ РАЗРЕШЕНИЯ <---
                GLint tex_res_loc = glGetUniformLocation(current_program, "uTextureResolution");
                GLint screen_res_loc = glGetUniformLocation(current_program, "uResolution");

                if (tex_res_loc != -1) {
                    glUniform2f(tex_res_loc, (GLfloat)state->tex_width, (GLfloat)state->tex_height);
                }
                if (screen_res_loc != -1) {
                    glUniform2f(screen_res_loc, (GLfloat)physical_width, (GLfloat)physical_height);
                }
                // ------------------------------

                glBindBuffer(GL_ARRAY_BUFFER, state->vbo);
                // ... (настройка атрибутов вершин) ...
                glDrawArrays(GL_TRIANGLES, 0, 6);
                // ... (отключение атрибутов, отвязка буфера) ...
             } else {
                 // Ошибка: режим картинки, но текстура не загружена. Рисуем черный фон.
                 glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                 glClear(GL_COLOR_BUFFER_BIT);
                 return true;
             }
             break;
        // RENDER_COLOR и RENDER_DEFAULT обработаны выше через glClear
        default:
            return true; // Ничего не делаем для неизвестных режимов
    }

    if (current_program == 0) {
        fprintf(stderr, "RendererGL Warning: No shader program selected for mode %d\n", state->render_mode);
        return true; // Не фатально, просто ничего не нарисовали
    }

    // --- Рисование Квада ---
    glBindBuffer(GL_ARRAY_BUFFER, state->vbo);

    // Настройка атрибутов вершин (должна соответствовать структуре данных в VBO и шейдеру)
    // Позиция вершины (vec4)
    GLint pos_loc = glGetAttribLocation(current_program, "aPosition");
    if (pos_loc >= 0) {
        glVertexAttribPointer(pos_loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)0);
        glEnableVertexAttribArray(pos_loc);
    }
    // Текстурные координаты (vec2)
    GLint tex_loc = glGetAttribLocation(current_program, "aTexCoord");
     if (tex_loc >= 0) {
        glVertexAttribPointer(tex_loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), (void*)(2 * sizeof(GLfloat)));
        glEnableVertexAttribArray(tex_loc);
    }

    // Рисуем квад (2 треугольника = 6 вершин, или 1 triangle fan = 4 вершины)
    // Используем способ с 6 вершинами (два треугольника)
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Отключаем атрибуты (хорошая практика)
     if (pos_loc >= 0) glDisableVertexAttribArray(pos_loc);
     if (tex_loc >= 0) glDisableVertexAttribArray(tex_loc);

    // Отвязываем VBO
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    // Проверка ошибок OpenGL (не обязательно каждый кадр, но полезно для отладки)
    // GLenum err;
    // while ((err = glGetError()) != GL_NO_ERROR) {
    //     fprintf(stderr, "RendererGL OpenGL Error: 0x%x\n", err);
    // }

    return true; // Команды рендеринга успешно отправлены
}


// --- Вспомогательные статические функции ---

static bool init_egl(RendererStateGL* state, struct wl_display* display, struct wl_surface* surface) {
    state->egl_display = eglGetDisplay((EGLNativeDisplayType)display);
    if (state->egl_display == EGL_NO_DISPLAY) {
        fprintf(stderr, "EGL Error: eglGetDisplay failed.\n");
        return false;
    }

    EGLint major, minor;
    if (eglInitialize(state->egl_display, &major, &minor) == EGL_FALSE) {
        fprintf(stderr, "EGL Error: eglInitialize failed (error 0x%x)\n", eglGetError());
        eglTerminate(state->egl_display); state->egl_display = EGL_NO_DISPLAY;
        return false;
    }
    printf("EGL Initialized: Version %d.%d\n", major, minor);

    // Выбираем конфигурацию EGL
    const EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT, // Требуем поддержку рендеринга в окно
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, // Требуем поддержку OpenGL ES 2.0
        EGL_RED_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_BLUE_SIZE, 8,
        EGL_ALPHA_SIZE, 8, // Запрашиваем альфа-канал
        EGL_DEPTH_SIZE, 0, // Глубина не нужна для 2D
        EGL_STENCIL_SIZE, 0, // Stencil не нужен
        EGL_NONE
    };
    EGLint num_config;
    if (eglChooseConfig(state->egl_display, config_attribs, &state->egl_config, 1, &num_config) == EGL_FALSE || num_config == 0) {
        fprintf(stderr, "EGL Error: eglChooseConfig failed or no suitable config found (error 0x%x)\n", eglGetError());
        eglTerminate(state->egl_display); state->egl_display = EGL_NO_DISPLAY;
        return false;
    }

    // Создаем EGL контекст
    const EGLint context_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, 2, // OpenGL ES 2.0
        EGL_NONE
    };
    state->egl_context = eglCreateContext(state->egl_display, state->egl_config, EGL_NO_CONTEXT, context_attribs);
    if (state->egl_context == EGL_NO_CONTEXT) {
        fprintf(stderr, "EGL Error: eglCreateContext failed (error 0x%x)\n", eglGetError());
        eglTerminate(state->egl_display); state->egl_display = EGL_NO_DISPLAY;
        return false;
    }

    // Создаем Wayland EGL окно
    // Размеры окна здесь могут быть начальными, EGL обычно адаптируется
    int physical_width = (int)round(state->current_logical_width * state->current_scale);
    int physical_height = (int)round(state->current_logical_height * state->current_scale);
    state->egl_window = wl_egl_window_create(surface, physical_width, physical_height);
    if (!state->egl_window) {
         fprintf(stderr, "EGL Error: wl_egl_window_create failed.\n");
         eglDestroyContext(state->egl_display, state->egl_context); state->egl_context = EGL_NO_CONTEXT;
         eglTerminate(state->egl_display); state->egl_display = EGL_NO_DISPLAY;
         return false;
    }

    // Создаем EGL surface
    state->egl_surface = eglCreateWindowSurface(state->egl_display, state->egl_config, (EGLNativeWindowType)state->egl_window, NULL);
    if (state->egl_surface == EGL_NO_SURFACE) {
        fprintf(stderr, "EGL Error: eglCreateWindowSurface failed (error 0x%x)\n", eglGetError());
        wl_egl_window_destroy(state->egl_window); state->egl_window = NULL;
        eglDestroyContext(state->egl_display, state->egl_context); state->egl_context = EGL_NO_CONTEXT;
        eglTerminate(state->egl_display); state->egl_display = EGL_NO_DISPLAY;
        return false;
    }

    // Делаем контекст текущим для инициализации GL
    if (eglMakeCurrent(state->egl_display, state->egl_surface, state->egl_surface, state->egl_context) == EGL_FALSE) {
         fprintf(stderr, "EGL Error: eglMakeCurrent failed during init (error 0x%x)\n", eglGetError());
         // Очистка...
         eglDestroySurface(state->egl_display, state->egl_surface); state->egl_surface = EGL_NO_SURFACE;
         wl_egl_window_destroy(state->egl_window); state->egl_window = NULL;
         eglDestroyContext(state->egl_display, state->egl_context); state->egl_context = EGL_NO_CONTEXT;
         eglTerminate(state->egl_display); state->egl_display = EGL_NO_DISPLAY;
         return false;
    }

    // Устанавливаем интервал для vsync (1 = включить vsync)
    eglSwapInterval(state->egl_display, 1);

    return true;
}

static void cleanup_egl(RendererStateGL* state) {
    if (state->egl_display != EGL_NO_DISPLAY) {
        eglMakeCurrent(state->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT); // Отвязать контекст
        if (state->egl_context != EGL_NO_CONTEXT) {
            eglDestroyContext(state->egl_display, state->egl_context);
            state->egl_context = EGL_NO_CONTEXT;
        }
        if (state->egl_surface != EGL_NO_SURFACE) {
            eglDestroySurface(state->egl_display, state->egl_surface);
            state->egl_surface = EGL_NO_SURFACE;
        }
        eglTerminate(state->egl_display);
        state->egl_display = EGL_NO_DISPLAY;
    }
    if (state->egl_window) {
        wl_egl_window_destroy(state->egl_window);
        state->egl_window = NULL;
    }
     printf("RendererGL: EGL Cleaned up.\n");
}

static bool init_gl_resources(RendererStateGL* state) {
    // 1. Создание шейдерных программ
    state->shader_program_color = create_program(vertex_shader_source, fragment_shader_color_source);
    state->shader_program_gradient = create_program(vertex_shader_source, fragment_shader_gradient_source);
    state->shader_program_texture = create_program(vertex_shader_source, fragment_shader_texture_source);

    if (!state->shader_program_color || !state->shader_program_gradient || !state->shader_program_texture) {
        fprintf(stderr, "OpenGL Error: Failed to create shader programs.\n");
        return false;
    }

    // 2. Получение расположений uniform-переменных
    state->color_uniform_color = glGetUniformLocation(state->shader_program_color, "uColor");
    state->gradient_uniform_phase = glGetUniformLocation(state->shader_program_gradient, "uPhase");
    state->gradient_uniform_resolution = glGetUniformLocation(state->shader_program_gradient, "uResolution");
    state->texture_uniform_texture = glGetUniformLocation(state->shader_program_texture, "uTexture");


    // 3. Создание VBO для квада (прямоугольника на весь экран)
    // Координаты вершин (-1..1 для позиции, 0..1 для текстуры)
    // Формат: X, Y, U, V
    const GLfloat quad_vertices[] = {
        // Треугольник 1
        -1.0f, -1.0f, 0.0f, 0.0f, // Нижний левый
         1.0f, -1.0f, 1.0f, 0.0f, // Нижний правый
        -1.0f,  1.0f, 0.0f, 1.0f, // Верхний левый
        // Треугольник 2
         1.0f, -1.0f, 1.0f, 0.0f, // Нижний правый
         1.0f,  1.0f, 1.0f, 1.0f, // Верхний правый
        -1.0f,  1.0f, 0.0f, 1.0f  // Верхний левый
    };

    glGenBuffers(1, &state->vbo);
    if (!state->vbo) {
         fprintf(stderr, "OpenGL Error: glGenBuffers failed.\n");
         return false;
    }
    glBindBuffer(GL_ARRAY_BUFFER, state->vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0); // Отвязать

    // VAO не обязателен в GLES 2.0, но если бы он был, создали бы и его здесь.
    // glGenVertexArrays(1, &state->vao); glBindVertexArray(state->vao); ... glBindVertexArray(0);

    // Начальные установки OpenGL
    glDisable(GL_DEPTH_TEST); // Глубина не нужна
    glDisable(GL_STENCIL_TEST);
    // Можно включить смешивание, если текстуры с альфа-каналом
    // glEnable(GL_BLEND);
    // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    printf("OpenGL Resources: Shaders and VBO created.\n");
    return true;
}


static void cleanup_gl_resources(RendererStateGL* state) {
     printf("RendererGL: Cleaning up GL resources...\n");
     // Удаляем шейдерные программы
     if (state->shader_program_color) glDeleteProgram(state->shader_program_color);
     if (state->shader_program_gradient) glDeleteProgram(state->shader_program_gradient);
     if (state->shader_program_texture) glDeleteProgram(state->shader_program_texture);
     state->shader_program_color = 0;
     state->shader_program_gradient = 0;
     state->shader_program_texture = 0;

     // Удаляем текстуру
     if (state->texture_id) {
         glDeleteTextures(1, &state->texture_id);
         state->texture_id = 0;
         printf(" -> Texture deleted.\n");
     }

     // Удаляем VBO
     if (state->vbo) {
         glDeleteBuffers(1, &state->vbo);
         state->vbo = 0;
          printf(" -> VBO deleted.\n");
     }

     // Удаляем VAO (если бы он был)
     // if (state->vao) glDeleteVertexArrays(1, &state->vao); state->vao = 0;
     printf("RendererGL: GL Resources Cleaned up.\n");
}


// Устанавливает режим рендеринга и связанные OpenGL ресурсы
static void set_render_mode_gl(RendererStateGL* state, const char* arg) {
    printf("RendererGL: Setting mode from argument: %s\n", arg ? arg : "(null)");

    // Освобождаем старые ресурсы, связанные с предыдущим режимом
    free(state->current_arg); state->current_arg = NULL;
    free(state->last_loaded_image_path); state->last_loaded_image_path = NULL;

    // Удаляем старую текстуру, если она была
    if (state->texture_id != 0) {
        printf(" -> Deleting previous texture (ID: %u)\n", state->texture_id);
        if (state->egl_display && state->egl_context) { // Убедимся, что контекст есть
             eglMakeCurrent(state->egl_display, state->egl_surface, state->egl_surface, state->egl_context);
             glDeleteTextures(1, &state->texture_id);
        } else {
            fprintf(stderr, "Warning: Cannot delete texture, EGL context not current.\n");
        }
        state->texture_id = 0;
        state->tex_width = 0;
        state->tex_height = 0;
    }
    // Сбрасываем цвет на дефолтный (черный)
    state->color_r = state->color_g = state->color_b = 0.0;

    // Копируем новый аргумент
    state->current_arg = arg ? strdup(arg) : NULL;
    if (arg && !state->current_arg) {
        perror("RendererGL: strdup failed for new argument");
        state->render_mode = RENDER_DEFAULT; // Откат к дефолту
        return;
    }

    // Определяем новый режим и загружаем ресурсы
    if (!arg || arg[0] == '\0') {
        printf(" -> Mode: RENDER_DEFAULT (null/empty arg)\n");
        state->render_mode = RENDER_DEFAULT;
    } else if (strcmp(arg, ANIMATION_COMMAND) == 0) {
        printf(" -> Mode: RENDER_ANIMATION\n");
        state->render_mode = RENDER_ANIMATION;
        state->phase = 0.0; // Сброс фазы
        state->last_update_time_ms = 0; // Сброс времени
    } else if (arg[0] == '#') {
        if (parse_color(arg, &state->color_r, &state->color_g, &state->color_b)) {
             printf(" -> Mode: RENDER_COLOR (%.2f, %.2f, %.2f)\n", state->color_r, state->color_g, state->color_b);
             state->render_mode = RENDER_COLOR;
        } else {
             fprintf(stderr, "RendererGL: Invalid color format '%s'. Falling back to default.\n", arg);
             state->render_mode = RENDER_DEFAULT;
             free(state->current_arg); state->current_arg = NULL;
        }
    } else {
        // Пытаемся загрузить как изображение
        printf(" -> Attempting to load image: %s\n", arg);
        if (load_texture_gl(state, arg)) {
            printf(" -> Mode: RENDER_IMAGE (Loaded successfully, ID: %u, Size: %dx%d)\n", state->texture_id, state->tex_width, state->tex_height);
            state->render_mode = RENDER_IMAGE;
            state->last_loaded_image_path = strdup(arg); // Запоминаем путь
             if (!state->last_loaded_image_path) perror("strdup failed for image path");
        } else {
            fprintf(stderr, "RendererGL: Failed to load image '%s'. Falling back to default.\n", arg);
            state->render_mode = RENDER_DEFAULT;
            free(state->current_arg); state->current_arg = NULL;
            // load_texture_gl сама должна была почистить state->texture_id при ошибке
        }
    }
     printf(" -> New mode set: %d\n", state->render_mode);
}

// Загружает изображение и создает OpenGL текстуру
static bool load_texture_gl(RendererStateGL* state, const char* image_path) {
    if (state->texture_id != 0) { // Удаляем предыдущую текстуру, если была
        glDeleteTextures(1, &state->texture_id);
        state->texture_id = 0;
    }
    state->tex_width = 0;
    state->tex_height = 0;

    int width, height, channels;
    // stbi_load загружает изображение с верхним левым углом в начале данных
    // OpenGL ожидает нижний левый угол, поэтому можно либо инвертировать UV в шейдере,
    // либо перевернуть изображение после загрузки (stbi_set_flip_vertically_on_load(1)).
    stbi_set_flip_vertically_on_load(1); // Переворачиваем для OpenGL
    unsigned char *img_data = stbi_load(image_path, &width, &height, &channels, 0);
    stbi_set_flip_vertically_on_load(0); // Возвращаем обратно

    if (!img_data) {
        fprintf(stderr, "OpenGL Texture Error: Failed to load image '%s': %s\n", image_path, stbi_failure_reason());
        return false;
    }

    GLenum format = GL_RGB;
    if (channels == 4) {
        format = GL_RGBA;
    } else if (channels == 3) {
        format = GL_RGB;
    } else if (channels == 1) {
         format = GL_LUMINANCE; // Или GL_RED в более новых версиях GLES
    } else {
         fprintf(stderr, "OpenGL Texture Error: Unsupported number of channels (%d) in image '%s'\n", channels, image_path);
         stbi_image_free(img_data);
         return false;
    }

    glGenTextures(1, &state->texture_id);
    if (state->texture_id == 0) {
         fprintf(stderr, "OpenGL Texture Error: glGenTextures failed.\n");
         stbi_image_free(img_data);
         return false;
    }

    glBindTexture(GL_TEXTURE_2D, state->texture_id);

    // Загружаем данные текстуры
    // Устанавливаем выравнивание пикселей (важно для форматов типа RGB)
    glPixelStorei(GL_UNPACK_ALIGNMENT, (channels == 3 || channels == 1) ? 1 : 4);
    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, img_data);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4); // Вернуть по умолчанию

    // Освобождаем память изображения на CPU
    stbi_image_free(img_data);

    // Устанавливаем параметры текстуры
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); // Или GL_REPEAT
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    // Фильтрация (билинейная подходит для большинства случаев)
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    // Можно сгенерировать мипмапы для лучшего качества при уменьшении,
    // но это требует GL_TEXTURE_MIN_FILTER типа *_MIPMAP_*
    // glGenerateMipmap(GL_TEXTURE_2D);

    glBindTexture(GL_TEXTURE_2D, 0); // Отвязываем текстуру

    // Проверяем ошибки OpenGL
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
         fprintf(stderr, "OpenGL Texture Error: GL error after texture loading: 0x%x\n", err);
         glDeleteTextures(1, &state->texture_id); state->texture_id = 0;
         return false;
    }

    state->tex_width = width;
    state->tex_height = height;
    return true;
}


// Парсит цвет #RGB или #RRGGBB (из старого рендерера, почти без изменений)
static bool parse_color(const char* color_str, double *r_out, double *g_out, double *b_out) {
    if (!color_str || color_str[0] != '#') return false;
    unsigned int r, g, b;
    size_t len = strlen(color_str); // Используем size_t для длины

    if (len == 7) { // #RRGGBB
        if (sscanf(color_str, "#%02x%02x%02x", &r, &g, &b) == 3) {
            *r_out = r / 255.0;
            *g_out = g / 255.0;
            *b_out = b / 255.0;
            return true;
        }
    } else if (len == 4) { // #RGB
        if (sscanf(color_str, "#%1x%1x%1x", &r, &g, &b) == 3) {
            *r_out = (r << 4 | r) / 255.0; // Эквивалентно r*17 / 255.0
            *g_out = (g << 4 | g) / 255.0;
            *b_out = (b << 4 | b) / 255.0;
            return true;
        }
    }
    fprintf(stderr, "Color parse error: Unknown format '%s'\n", color_str);
    return false;
}

static void update_animation(RendererStateGL* state, uint32_t time_ms) {
     // Обновление фазы анимации, только если активна
    if (state->render_mode == RENDER_ANIMATION) {
        uint32_t delta_time = 0;
        // Используем uint64_t для времени, чтобы избежать проблем при переполнении uint32_t
        uint64_t current_time_ms = time_ms; // Просто используем переданное время

        if (state->last_update_time_ms != 0 && current_time_ms >= state->last_update_time_ms) {
            delta_time = (uint32_t)(current_time_ms - state->last_update_time_ms);
        } else if (state->last_update_time_ms == 0) {
            delta_time = 16; // Примерно 1/60 сек для первого кадра
        } // else: Время пошло назад? delta_time = 0

        state->last_update_time_ms = current_time_ms; // Обновляем время *после* расчета дельты

        double time_seconds = delta_time / 1000.0;
        // Ограничиваем максимальный шаг по времени, чтобы избежать рывков после лагов
        if (time_seconds > 0.1) time_seconds = 0.1;

        double base_phase_change_per_second = (2.0 * M_PI) / 10.0; // Полный цикл градиента за 10 секунд
        state->phase += time_seconds * base_phase_change_per_second * state->speed_factor;
        state->phase = fmod(state->phase, 2.0 * M_PI); // Держим фазу в пределах [0, 2*PI)
    }
}
bool renderer_gl_swap_buffers(RendererStateGL* state) {
    if (!state || !state->egl_display || !state->egl_surface || !state->egl_context) {
        fprintf(stderr, "RendererGL Error: Invalid EGL state in swap_buffers.\n");
        return false;
    }

    // Делаем контекст текущим ПЕРЕД вызовом swap
    if (eglMakeCurrent(state->egl_display, state->egl_surface, state->egl_surface, state->egl_context) == EGL_FALSE) {
         fprintf(stderr, "RendererGL Error: eglMakeCurrent failed before swap (EGL error: 0x%x)\n", eglGetError());
         // Это может быть критической ошибкой, указывающей на потерю контекста/surface
         // Возвращаем false, чтобы вызывающий код мог среагировать (например, переинициализировать)
         return false;
    }

    EGLBoolean swapped = eglSwapBuffers(state->egl_display, state->egl_surface);
    if (!swapped) {
        EGLint error = eglGetError();
        fprintf(stderr, "RendererGL Error: eglSwapBuffers failed (EGL error: 0x%x)\n", error);
        // Проверяем на потерю контекста/поверхности
        if (error == EGL_BAD_SURFACE || error == EGL_CONTEXT_LOST) {
             fprintf(stderr, " -> EGL Surface or Context lost during swap.\n");
             // Вызывающий код (present_output_frame) должен будет обработать это
             // и инициировать пересоздание рендерера.
        }
        return false;
    }

    return true;
}

void renderer_gl_resize_egl_window(RendererStateGL* state, int physical_width, int physical_height) {
    // Проверяем, что состояние и окно существуют
    if (state && state->egl_window && physical_width > 0 && physical_height > 0) {
        // Вызываем функцию изменения размера
        wl_egl_window_resize(state->egl_window, physical_width, physical_height, 0, 0);
        // Возможно, здесь нужно что-то еще? Например, обновить viewport по умолчанию?
        // Зависит от вашей логики рендеринга.
    } else if (state) {
         fprintf(stderr, "Warning: Attempt to resize null EGL window or invalid dimensions (%dx%d) for renderer state %p\n",
             physical_width, physical_height, (void*)state);
    }
}
