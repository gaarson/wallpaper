// mode_color.c
#include "../common.h"
#include "color.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h> // For strlen, sscanf
#include <GLES2/gl2.h>

// --- State ---
typedef struct {
    float r, g, b;
} ColorModeState;

// --- Helper to parse color ---
// (Copied and adapted from original renderer code)
static bool parse_color_internal(const char* color_str, float *r_out, float *g_out, float *b_out) {
     if (!color_str || color_str[0] != '#') return false;
     unsigned int r, g, b;
     size_t len = strlen(color_str);

     if (len == 7) { // #RRGGBB
         if (sscanf(color_str, "#%02x%02x%02x", &r, &g, &b) == 3) {
             *r_out = r / 255.0f;
             *g_out = g / 255.0f;
             *b_out = b / 255.0f;
             return true;
         }
     } else if (len == 4) { // #RGB
         if (sscanf(color_str, "#%1x%1x%1x", &r, &g, &b) == 3) {
             *r_out = (r << 4 | r) / 255.0f;
             *g_out = (g << 4 | g) / 255.0f;
             *b_out = (b << 4 | b) / 255.0f;
             return true;
         }
     }
     fprintf(stderr, "ModeColor Error: Unknown color format '%s'\n", color_str);
     return false;
 }


// --- Interface Functions ---

static void* color_init(const char* arg, GLuint common_vbo) {
    (void)common_vbo; // Unused
    log_debug("ModeColor: Initializing with arg: %s\n", arg ? arg : "(null)");
    ColorModeState* state = calloc(1, sizeof(ColorModeState));
    if (!state) {
        perror("ModeColor calloc state");
        return NULL;
    }

    if (!parse_color_internal(arg, &state->r, &state->g, &state->b)) {
        fprintf(stderr, "ModeColor Error: Failed to parse color. Defaulting to black.\n");
        // Keep state allocated, but set color to black
        state->r = state->g = state->b = 0.0f;
        // Or free state and return NULL to trigger fallback in core? Let's fallback.
        // free(state);
        // return NULL;
        // Let's keep it simple: initialize to black if parse fails, don't trigger core fallback
    }
     log_debug("ModeColor: Initialized (Color: %.2f, %.2f, %.2f).\n", state->r, state->g, state->b);
    return state;
}

static void color_cleanup(void* mode_state) {
    ColorModeState* state = (ColorModeState*)mode_state;
    if (!state) return;
    log_debug("ModeColor: Cleaning up.\n");
    free(state);
}

static bool color_render(void* mode_state, const RenderParams* params) {
    ColorModeState* state = (ColorModeState*)mode_state;
    if (!state) return false; // Should not happen if init succeeded
    (void)params; // Unused

    glClearColor(state->r, state->g, state->b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    return true;
}

// Implement other functions as stubs (like default mode)
static bool color_handle_command(void* mode_state, const char* command) { (void)mode_state; (void)command; return false; }
static void color_resize(void* mode_state, int w, int h) { (void)mode_state; (void)w; (void)h; }
static bool color_needs_redraw(void* mode_state) { (void)mode_state; return false; }

// --- Interface Instance ---
const RenderModeInterface color_mode_interface = {
    .init = color_init,
    .cleanup = color_cleanup,
    .render = color_render,
    .handle_command = color_handle_command,
    .resize = color_resize,
    .needs_redraw = color_needs_redraw,
};
