#ifndef WAYLAND_SETUP_H
#define WAYLAND_SETUP_H

#include "common.h"

// --- Глобальные переменные Wayland (Объявления extern) ---
// Определены в wayland_setup.c
extern struct wl_display *g_display;
extern struct wl_registry *g_registry;
extern struct zwlr_layer_shell_v1 *g_layer_shell;
extern struct wl_compositor *g_compositor;
extern struct wl_shm *g_shm;
extern struct wp_viewporter *g_viewporter; // Может быть NULL
extern struct wp_fractional_scale_manager_v1 *g_fractional_scale_manager; // Может быть NULL

// --- Прототипы ---
bool init_wayland(void); // Основная функция инициализации
void cleanup_wayland(void); // Функция очистки Wayland ресурсов

// Обработчики registry listener (могут быть статическими, если не нужны извне)
// void handle_global(void *data, struct wl_registry *reg, uint32_t name, const char *iface, uint32_t ver);
// void handle_global_remove(void *data, struct wl_registry *reg, uint32_t name);

#endif // WAYLAND_SETUP_H
