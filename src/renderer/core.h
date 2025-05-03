// renderer_core.h
#ifndef RENDERER_CORE_H
#define RENDERER_CORE_H

#include "mode.h" // Needs interface definition
#include "./../hash_table.h" // Needs hash table definition
#include <EGL/egl.h>
#include <wayland-client.h>
#include <wayland-egl.h>
#include <GLES3/gl3.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct RendererCoreState {
    // EGL Objects
    EGLDisplay egl_display;
    EGLConfig egl_config;
    EGLContext egl_context;
    EGLSurface egl_surface;
    struct wl_egl_window *egl_window;

    // Common OpenGL Resources
    GLuint quad_vbo;

    // Mode Management
    HashTable* mode_registry; // Hash table for modes
    const RenderModeInterface* active_mode_impl; // Pointer to active interface
    void* active_mode_state;                     // Pointer to active mode's state data

    // Current Output State
    int current_logical_width;
    int current_logical_height;
    double current_scale;
    uint64_t last_update_time_ms; // For calculating frame delta time

    // Wayland handles needed for EGL init/resize
    struct wl_display* wayland_display;
    struct wl_surface* wayland_surface;

} RendererCoreState;

// --- Public API ---

RendererCoreState* renderer_core_init(struct wl_display* display, struct wl_surface* surface,
                                      int initial_logical_width, int initial_logical_height,
                                      double initial_scale, const char* initial_arg);

void renderer_core_cleanup(RendererCoreState* state);

// Tries to set the rendering mode based on the argument string.
// Returns true if the mode was changed or successfully set (even to default).
// Returns false if a specific mode was requested but failed to initialize.
bool renderer_core_set_mode(RendererCoreState* state, const char* arg);

// Handles a command string. May change mode or delegate to active mode.
// Returns true if the command was recognized and handled (by core or mode).
bool renderer_core_handle_command(RendererCoreState* state, const char* command);

// Renders one frame using the active mode.
// Returns true on success, false on critical rendering error.
bool renderer_core_render_frame(RendererCoreState* state, uint32_t time_ms);

// Swaps the EGL buffers. Returns true on success.
bool renderer_core_swap_buffers(RendererCoreState* state);

// Updates core state and notifies active mode of resize.
void renderer_core_resize(RendererCoreState* state, int logical_width, int logical_height, double scale);

// Returns true if the current mode requires continuous redrawing.
bool renderer_core_needs_redraw(RendererCoreState* state);


#endif // RENDERER_CORE_H
