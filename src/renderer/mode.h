// render_mode_interface.h
#ifndef RENDER_MODE_INTERFACE_H
#define RENDER_MODE_INTERFACE_H

#include <stdbool.h>
#include <stdint.h>
#include <GLES3/gl3.h>

// Forward declaration for core state if needed by modes (try to avoid)
// struct RendererCoreState;

typedef struct {
    int physical_width;
    int physical_height;
    uint32_t time_ms;
    double time_delta_sec;
    GLuint common_vbo; // Shared VBO for the full-screen quad
    // Add other shared parameters if necessary (e.g., core state pointer)
    // const struct RendererCoreState* core_state; // Example
} RenderParams;

typedef struct RenderModeInterface {
    // Initializes the mode. Returns a pointer to mode-specific state, or NULL on error.
    void* (*init)(const char* arg, GLuint common_vbo);

    // Cleans up mode-specific resources.
    void (*cleanup)(void* mode_state);

    // Renders one frame. Returns true on success, false on critical error.
    bool (*render)(void* mode_state, const RenderParams* params);

    // Handles mode-specific commands. Returns true if handled.
    bool (*handle_command)(void* mode_state, const char* command);

    // Notifies the mode of a resize event.
    void (*resize)(void* mode_state, int physical_width, int physical_height);

    // Indicates if the mode requires continuous redrawing (e.g., animation).
    bool (*needs_redraw)(void* mode_state);

} RenderModeInterface;

#endif // RENDER_MODE_INTERFACE_H
