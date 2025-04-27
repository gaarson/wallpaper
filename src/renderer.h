#ifndef RENDERER_H
#define RENDERER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h> // Для uint32_t

// Объявляем структуру состояния рендерера как неполный тип (Opaque Pointer).
// Детали реализации будут скрыты в .c файле.
typedef struct RendererState RendererState;

/**
 * @brief Инициализирует рендерер для заданных размеров.
 *
 * @param width Ширина области рендеринга.
 * @param height Высота области рендеринга.
 * @param initial_arg Строка с начальным аргументом (может быть NULL, цвет типа "#RRGGBB", или путь к файлу в будущем).
 * @return Указатель на внутреннее состояние рендерера или NULL при ошибке.
 */
RendererState* renderer_init(int width, int height, const char* initial_arg);

/**
 * @brief Освобождает все ресурсы, связанные с рендерером.
 *
 * @param state Указатель на состояние рендерера, полученное от renderer_init.
 */
void renderer_cleanup(RendererState* state);

/**
 * @brief Обрабатывает внешнюю команду.
 *
 * @param state Указатель на состояние рендерера.
 * @param command Строка с командой (например, "faster", "slower").
 * @return true, если команда была распознана и обработана, иначе false.
 * (Возвращаемое значение пока не критично, т.к. перерисовка идет по таймеру).
 */
bool renderer_handle_command(RendererState* state, const char* command);

/**
 * @brief Рендерит один кадр анимации в предоставленный буфер.
 *
 * Обновляет внутреннее состояние анимации (фазу) на основе времени.
 *
 * @param state Указатель на состояние рендерера.
 * @param buffer Указатель на начало буфера памяти (mmap'нутый из SHM), куда нужно рисовать.
 * @param width Ширина буфера.
 * @param height Высота буфера.
 * @param stride Шаг (stride) буфера в байтах (количество байт на одну строку пикселей).
 * @param time_ms Текущее время в миллисекундах (например, от CLOCK_MONOTONIC),
 * используется для расчета дельты времени и обновления анимации.
 * @return true, если рендеринг прошел успешно, иначе false.
 */
bool renderer_render_frame(RendererState* state, void* buffer, int width, int height, int stride, uint32_t time_ms);

#endif // RENDERER_H
