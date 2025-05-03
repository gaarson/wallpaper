#ifndef RENDERER_H
#define RENDERER_H

#include <stdbool.h>
#include <stdint.h>     // Для uint32_t, uint64_t
#include <wayland-client.h> // Нужны Wayland типы для init

// --- Режимы рендеринга (остаются прежними) ---
typedef enum {
    RENDER_DEFAULT,     // Фон по умолчанию (черный)
    RENDER_ANIMATION,   // Анимированный градиент
    RENDER_COLOR,       // Сплошной цвет
    RENDER_IMAGE        // Изображение из файла
} RenderMode;

// Объявляем структуру состояния рендерера OpenGL как неполный тип (Opaque Pointer).
// Детали реализации будут скрыты в renderer_gl.c.
typedef struct RendererStateGL RendererStateGL;

/**
 * @brief Инициализирует рендерер OpenGL/EGL для данной Wayland surface.
 *
 * @param display Глобальный объект wl_display.
 * @param surface Объект wl_surface, на который будет идти рендеринг.
 * @param initial_logical_width Начальная логическая ширина surface.
 * @param initial_logical_height Начальная логическая высота surface.
 * @param initial_scale Начальный масштаб surface.
 * @param initial_arg Строка с начальным аргументом (цвет, путь к файлу, %SETUP_ANIMATION%).
 * @return Указатель на внутреннее состояние рендерера или NULL при ошибке.
 */
RendererStateGL* renderer_gl_init(struct wl_display* display, struct wl_surface* surface,
                                  int initial_logical_width, int initial_logical_height,
                                  double initial_scale, const char* initial_arg);

/**
 * @brief Освобождает все EGL и OpenGL ресурсы, связанные с рендерером.
 *
 * @param state Указатель на состояние рендерера, полученное от renderer_gl_init.
 */
void renderer_gl_cleanup(RendererStateGL* state);

/**
 * @brief Обрабатывает внешнюю команду (смена режима, управление анимацией).
 *
 * @param state Указатель на состояние рендерера.
 * @param command Строка с командой (например, "#RRGGBB", "/path/to/image.png", "faster").
 * @return true, если команда была распознана и обработана, иначе false.
 */
bool renderer_gl_handle_command(RendererStateGL* state, const char* command);

/**
 * @brief Рендерит один кадр с помощью OpenGL в EGLSurface, связанную с wl_surface.
 *
 * Обновляет внутреннее состояние анимации (фазу) на основе времени.
 * Устанавливает glViewport в соответствии с переданными размерами/масштабом.
 * НЕ вызывает eglSwapBuffers.
 *
 * @param state Указатель на состояние рендерера.
 * @param logical_width Текущая логическая ширина области рендеринга.
 * @param logical_height Текущая логическая высота области рендеринга.
 * @param scale Текущий масштаб (дробный или целый).
 * @param time_ms Текущее время в миллисекундах.
 * @return true, если рендеринг прошел успешно (команды отправлены), иначе false (критическая ошибка OpenGL/EGL).
 */
bool renderer_gl_render_frame(RendererStateGL* state, int logical_width, int logical_height, double scale, uint32_t time_ms);

/**
 * @brief Выполняет eglSwapBuffers для surface, связанной с данным состоянием рендерера.
 *
 * Должна вызываться после renderer_gl_render_frame.
 * Внутри обрабатывает eglMakeCurrent перед вызовом eglSwapBuffers.
 *
 * @param state Указатель на состояние рендерера.
 * @return true, если обмен буферов прошел успешно, иначе false.
 */
bool renderer_gl_swap_buffers(RendererStateGL* state);

/**
 * @brief Изменяет размер связанного с рендерером EGL окна Wayland.
 *
 * @param state Указатель на состояние рендерера.
 * @param physical_width Новая физическая ширина в пикселях.
 * @param physical_height Новая физическая высота в пикселях.
 */
void renderer_gl_resize_egl_window(RendererStateGL* state, int physical_width, int physical_height);


#endif // RENDERER_H
