#ifndef OUTPUT_H
#define OUTPUT_H

#include "common.h"
#include "buffer.h" // Нужно определение buffer_pool_entry

// --- Структура для состояния одного выхода ---
struct client_output {
    struct wl_output *wl_output;
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    bool configured; // Флаг, что получены логические размеры и можно рендерить
    RendererState *renderer_state;
    uint32_t wl_name; // Глобальное имя wl_output

    // Поля для масштабирования
    int32_t scale_factor; // От wl_output.scale
    double fractional_scale; // От wp_fractional_scale_v1.preferred_scale (scale_num / 120.0)
    bool scale_received; // Флаг, что получено хотя бы одно значение масштаба

    // Размеры
    int logical_width;  // Заданные композитором (событие configure)
    int logical_height;
    int physical_width; // Рассчитанные для буфера (logical * scale)
    int physical_height;

    // Объекты протоколов
    struct wp_viewport *viewport; // Для корректного отображения буфера на surface
    struct wp_fractional_scale_v1 *fractional_scale_obj; // Для получения дробного масштаба

    // Пул буферов
    struct buffer_pool_entry buffers[2]; // Пул из двух буферов
    int current_buffer_index;            // Индекс буфера, который был последним отправлен (-1, 0 или 1)

    struct client_output *next; // Для связного списка (альтернатива динамическому массиву)
    struct client_output *prev;
};

// --- Глобальные переменные (доступ через функции) ---
extern struct client_output *outputs_list_head; // Голова списка выходов
extern int n_outputs; // Количество выходов

// --- Прототипы ---
struct client_output* find_output_by_wl_name(uint32_t name);
struct client_output* find_output_by_wl_output(struct wl_output *wl_out);
struct client_output* add_output(struct wl_output *wl_out, uint32_t name);
void remove_output(struct client_output *output);
void cleanup_all_outputs(void);

void check_and_reinit_renderer(struct client_output *output); // Перенесена сюда
void cleanup_output_buffers(struct client_output *output);
bool render_and_commit_output(struct client_output *output, uint32_t time_ms); // Перенесена сюда

// Листенеры, специфичные для output
void output_handle_scale(void *data, struct wl_output *o, int32_t factor);
void output_handle_name(void *data, struct wl_output *o, const char *n);
void output_handle_description(void *d, struct wl_output *o, const char *desc);
// Другие output_handle_* могут быть статическими в output.c, если не нужны извне

void layer_surface_handle_configure(void *d, struct zwlr_layer_surface_v1 *ls, uint32_t serial, uint32_t w, uint32_t h);
void layer_surface_handle_closed(void *d, struct zwlr_layer_surface_v1 *ls);

void fractional_scale_handle_preferred_scale(void *data, struct wp_fractional_scale_v1 *fract_scale, uint32_t scale_numerator);


#endif // OUTPUT_H
