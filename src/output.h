#ifndef OUTPUT_H
#define OUTPUT_H

#include <wayland-client.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h> // Для timespec в check_and_reinit_renderer
#include <math.h> // Для round

// Подключаем протоколы, которые используются в output.c
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "xdg-output-unstable-v1-client-protocol.h" // Если используется zxdg_output_v1
#include "fractional-scale-v1-client-protocol.h"
#include "viewporter-client-protocol.h"

#include "renderer/core.h" // Наш обновленный интерфейс рендерера
#include "config_monitor.h" // Предполагаем, что этот файл есть

#define UNUSED(x) (void)(x)

// Структура для хранения информации о каждом выходе (мониторе)
struct client_output {
    struct client_output *next, *prev; // Связный список

    uint32_t wl_name;           // Глобальное имя (ID) wl_output
    struct wl_output *wl_output; // Sam Wayland объект

    // Wayland объекты, связанные с этим выходом
    struct wl_surface *surface;
    struct zwlr_layer_surface_v1 *layer_surface;
    struct wp_viewport *viewport;           // Для масштабирования буфера композитором
    struct wp_fractional_scale_v1 *fractional_scale_obj; // Для дробного масштабирования

    // Размеры и масштаб
    int logical_width;          // Ширина в логических пикселях (от layer_surface.configure)
    int logical_height;         // Высота в логических пикселях
    // Физические размеры больше не храним здесь, они внутри рендерера или вычисляются на лету
    int32_t scale_factor;       // Целочисленный масштаб от wl_output.scale
    double fractional_scale;    // Дробный масштаб (scale_numerator / 120.0)
    bool scale_received;        // Флаг, что мы получили хотя бы один масштаб

    bool configured;            // Флаг, что мы получили первую конфигурацию (размеры)

    // Состояние рендерера OpenGL/EGL
    RendererCoreState *renderer_state_gl; // Указатель на состояние рендерера

    // Поля для SHM буферов УДАЛЕНЫ:
    // struct buffer_pool_entry buffers[2];
    // int current_buffer_index;
};

// --- Глобальные переменные ---
extern struct client_output *outputs_list_head; // Голова списка выходов
extern int n_outputs;                         // Количество активных выходов

// --- Функции управления выходами ---
struct client_output* add_output(struct wl_output *wl_out, uint32_t name);
void remove_output(struct client_output *output);
void cleanup_all_outputs(void);
struct client_output* find_output_by_wl_name(uint32_t name);
struct client_output* find_output_by_wl_output(struct wl_output *wl_out);

// --- Обработчики событий Wayland (прототипы) ---
// wl_output listener
void output_handle_scale(void *data, struct wl_output *o, int32_t factor);
void output_handle_name(void *data, struct wl_output *o, const char *name);
void output_handle_description(void *data, struct wl_output *o, const char *desc);
// Остальные (geometry, mode, done) могут остаться статическими в .c

// zwlr_layer_surface_v1 listener
void layer_surface_handle_configure(void *data, struct zwlr_layer_surface_v1 *ls, uint32_t serial, uint32_t width, uint32_t height);
void layer_surface_handle_closed(void *data, struct zwlr_layer_surface_v1 *ls);

// wp_fractional_scale_v1 listener
void fractional_scale_handle_preferred_scale(void *data, struct wp_fractional_scale_v1 *fs, uint32_t scale_numerator);

// --- Функция проверки и переинициализации ---
// Вызывается при изменении конфигурации или масштаба
void check_and_reinit_renderer(struct client_output *output);

// --- Функция рендеринга и представления кадра ---
// Заменяет render_and_commit_output
bool present_output_frame(struct client_output *output, uint32_t time_ms);

#endif // OUTPUT_H
