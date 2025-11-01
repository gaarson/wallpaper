// mode_default.c
#include "../common.h"
#include "default.h"
#include <stdlib.h> // For NULL
#include <stdio.h>  // For printf
#include <GLES2/gl2.h> // For glClearColor/glClear

// --- State (None needed for default) ---
// typedef struct { ... } DefaultModeState; // Not necessary

// --- Interface Functions ---

static void* default_init(const char* arg, GLuint common_vbo) {
    (void)arg; // Unused
    (void)common_vbo; // Unused
    log_debug("ModeDefault: Initializing (no state needed).\n");
    // No allocation needed, return a non-NULL dummy pointer if required
    // or just return (void*)1 to indicate success without state.
    // Let's return NULL and handle it in the core renderer check.
    // Actually, returning a consistent non-NULL indicates success even without state.
    return (void*)1; // Indicate success
}

static void default_cleanup(void* mode_state) {
    (void)mode_state; // Unused
    log_debug("ModeDefault: Cleaning up (no state to clean).\n");
    // No cleanup needed
}

static bool default_render(void* mode_state, const RenderParams* params) {
    (void)mode_state; // Unused
    (void)params;   // Unused
    // log_debug("ModeDefault: Rendering frame (glClear black).\n");
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f); // Black
    glClear(GL_COLOR_BUFFER_BIT);
    return true;
}

static bool default_handle_command(void* mode_state, const char* command) {
    (void)mode_state; // Unused
    (void)command;  // Unused
    return false; // Default mode doesn't handle commands
}

static void default_resize(void* mode_state, int physical_width, int physical_height) {
    (void)mode_state; // Unused
    (void)physical_width;
    (void)physical_height;
    // No action needed for resize
}

static bool default_needs_redraw(void* mode_state) {
    (void)mode_state;
    return false; // Static color doesn't need continuous redraw
}

// --- Interface Instance ---
const RenderModeInterface default_mode_interface = {
    .init = default_init,
    .cleanup = default_cleanup,
    .render = default_render,
    .handle_command = default_handle_command,
    .resize = default_resize,
    .needs_redraw = default_needs_redraw,
};
