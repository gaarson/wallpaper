// renderer_core.c
#include "core.h"
#include "mode.h"
#include "./../hash_table.h"
#include "./../shader_utils.h" // May not be directly needed, but good to include

// Include headers for all known modes to access their interface instances
#include "default.h"
#include "color.h"
#include "gradient.h"
#include "texture.h"
#include "grid.h"
#include "starfield.h"
// #include "mode_grid.h" // Include others as needed

#include <stdlib.h>
#include <stdio.h>
#include <string.h> // For strcmp
#include <math.h>   // For round
#include <time.h>   // For clock_gettime if calculating time internally

// --- Constants ---
#define MODE_REGISTRY_SIZE 16 // Initial size for the hash table
                              //
// #define DEFAULT_MODE_KEY "grid"
#define DEFAULT_MODE_KEY "default"

#define COLOR_MODE_KEY "color"
#define GRADIENT_MODE_KEY "gradient"
#define TEXTURE_MODE_KEY "texture"
#define GRID_MODE_KEY "grid"
#define STARFIELD_MODE_KEY "starfield"

// --- Forward Declarations for Static Helpers ---
static bool init_egl_core(RendererCoreState* state);
static void cleanup_egl_core(RendererCoreState* state);
static bool init_gl_core(RendererCoreState* state);
static void cleanup_gl_core(RendererCoreState* state);
static bool populate_mode_registry(RendererCoreState* state);
static const RenderModeInterface* find_mode_implementation(HashTable* registry, const char* arg);

// --- Public API Implementation ---

RendererCoreState* renderer_core_init(struct wl_display* display, struct wl_surface* surface,
                                      int initial_logical_width, int initial_logical_height,
                                      double initial_scale, const char* initial_arg)
{
    printf("Core: Initializing...\n");
    RendererCoreState* state = calloc(1, sizeof(RendererCoreState));
    if (!state) { perror("Core calloc state"); return NULL; }

    state->wayland_display = display;
    state->wayland_surface = surface;
    state->current_logical_width = initial_logical_width > 0 ? initial_logical_width : 1;
    state->current_logical_height = initial_logical_height > 0 ? initial_logical_height : 1;
    state->current_scale = initial_scale > 0 ? initial_scale : 1.0;
    state->active_mode_impl = NULL;
    state->active_mode_state = NULL;
    state->mode_registry = NULL;
    state->last_update_time_ms = 0; // Initialize time

    // 1. Create Mode Registry (Hash Table)
    state->mode_registry = ht_create(MODE_REGISTRY_SIZE);
    if (!state->mode_registry) {
        fprintf(stderr, "Core Error: Failed to create mode registry.\n");
        free(state); return NULL;
    }
    if (!populate_mode_registry(state)) {
         fprintf(stderr, "Core Error: Failed to populate mode registry.\n");
         ht_destroy(state->mode_registry);
         free(state); return NULL;
    }

    // 2. Initialize EGL
    if (!init_egl_core(state)) {
        fprintf(stderr, "Core Error: EGL initialization failed.\n");
        ht_destroy(state->mode_registry);
        free(state); return NULL;
    }

    // 3. Initialize Core OpenGL resources (needs EGL context active)
    if (!init_gl_core(state)) {
        fprintf(stderr, "Core Error: Core OpenGL resource initialization failed.\n");
        cleanup_egl_core(state);
        ht_destroy(state->mode_registry);
        free(state); return NULL;
    }

    // 4. Set initial render mode (use the public function)
    if (!renderer_core_set_mode(state, initial_arg)) {
         fprintf(stderr, "Core Warning: Failed to set initial mode '%s'. Default mode should be active.\n", initial_arg ? initial_arg : "default");
         // set_mode should have fallen back to default already
    }

    printf("Core: Initialization complete.\n");
    return state;
}

void renderer_core_cleanup(RendererCoreState* state) {
    if (!state) return;
    printf("Core: Cleaning up...\n");

    // 1. Clean up the active mode
    if (state->active_mode_impl && state->active_mode_impl->cleanup) {
        printf("Core: Cleaning up active mode...\n");
        state->active_mode_impl->cleanup(state->active_mode_state);
    }
    state->active_mode_impl = NULL;
    state->active_mode_state = NULL;

    // 2. Clean up core GL resources (VBO)
    cleanup_gl_core(state); // Needs context

    // 3. Clean up EGL
    cleanup_egl_core(state); // Detaches context

    // 4. Clean up mode registry
    ht_destroy(state->mode_registry);
    state->mode_registry = NULL;

    free(state);
    printf("Core: Cleanup complete.\n");
}

// Tries to set a mode, falls back to default on failure.
bool renderer_core_set_mode(RendererCoreState* state, const char* arg) {
    if (!state || !state->mode_registry) return false;
    printf("Core: Setting mode with arg: %s\n", arg ? arg : "(null)");

    const RenderModeInterface* requested_impl = find_mode_implementation(state->mode_registry, arg);
    const char* requested_arg_for_init = arg;

    if (!requested_impl) {
        fprintf(stderr, "Core Warning: No implementation found for arg '%s'. Falling back to default.\n", arg ? arg : "(null)");
        requested_impl = ht_lookup(state->mode_registry, DEFAULT_MODE_KEY);
        requested_arg_for_init = NULL; // Default mode doesn't need an arg usually
        if (!requested_impl) {
             fprintf(stderr, "Core CRITICAL Error: Default mode implementation not found in registry!\n");
             // Cannot proceed without a default mode. Clean up old mode if any, but leave state inconsistent.
             if (state->active_mode_impl && state->active_mode_impl->cleanup) {
                 state->active_mode_impl->cleanup(state->active_mode_state);
             }
             state->active_mode_impl = NULL;
             state->active_mode_state = NULL;
             return false; // Indicate critical failure
        }
    }

    // --- If requested mode is the same as current, do nothing (optimization) ---
    // Note: This assumes init() is expensive. If init is cheap, we might skip this check.
    // This check is tricky if the *argument* changes but maps to the *same* mode interface (e.g., different color hex).
    // Let's simplify: Always try to set the mode. The init function should handle args.
    // if (state->active_mode_impl == requested_impl) {
    //     printf("Core: Requested mode is already active.\n");
    //     // TODO: Potentially check if argument changed and call an 'update' method if needed?
    //     return true;
    // }

    // --- Cleanup previous mode ---
    if (state->active_mode_impl && state->active_mode_impl->cleanup) {
        printf("Core: Cleaning up previous mode...\n");
        state->active_mode_impl->cleanup(state->active_mode_state);
    }
    state->active_mode_impl = NULL;
    state->active_mode_state = NULL;

    // --- Initialize the new mode ---
    printf("Core: Initializing requested mode...\n");
    void* new_state = NULL;
    bool init_success = false;
    if (requested_impl->init) {
         // Ensure EGL context is current for GL calls within init()
        if (eglMakeCurrent(state->egl_display, state->egl_surface, state->egl_surface, state->egl_context) == EGL_FALSE) {
             fprintf(stderr, "Core Error: eglMakeCurrent failed before mode init (EGL error: 0x%x)\n", eglGetError());
             // Try falling back to default *without* calling its init (since context failed)
             state->active_mode_impl = ht_lookup(state->mode_registry, DEFAULT_MODE_KEY); // Just set pointer
             state->active_mode_state = (void*)1; // Dummy state
             return false; // Indicate error, but default *might* work if it doesn't use GL in render
        }
        new_state = requested_impl->init(requested_arg_for_init, state->quad_vbo);
        init_success = (new_state != NULL);
    } else {
         fprintf(stderr, "Core Error: Requested mode implementation has no init function!\n");
         init_success = false;
    }


    if (init_success) {
        printf("Core: Mode initialized successfully.\n");
        state->active_mode_impl = requested_impl;
        state->active_mode_state = new_state;
    } else {
        fprintf(stderr, "Core Error: Failed to initialize requested mode. Falling back to default.\n");
        // Cleanup might have already happened if init partially failed, but call again just in case
        if(requested_impl->cleanup) requested_impl->cleanup(new_state); // new_state might be NULL or partially init'd

        const RenderModeInterface* default_impl = ht_lookup(state->mode_registry, DEFAULT_MODE_KEY);
        if (default_impl && default_impl->init) {
             printf("Core: Initializing default mode...\n");
             // Context should still be current from previous attempt
             new_state = default_impl->init(NULL, state->quad_vbo);
             if (new_state) {
                 state->active_mode_impl = default_impl;
                 state->active_mode_state = new_state;
                 printf("Core: Default mode initialized successfully.\n");
             } else {
                 fprintf(stderr, "Core CRITICAL Error: Failed to initialize default mode!\n");
                 // No mode active, rendering will likely just clear screen.
                 return false; // Indicate failure to set even default
             }
        } else {
             fprintf(stderr, "Core CRITICAL Error: Default mode implementation or its init not found!\n");
             return false; // Indicate critical failure
        }
    }

    // Notify the newly set mode (whether requested or default) of the current size
    if (state->active_mode_impl && state->active_mode_impl->resize) {
         int physical_width = (int)round(state->current_logical_width * state->current_scale);
         int physical_height = (int)round(state->current_logical_height * state->current_scale);
          printf("Core: Notifying new mode of initial size %dx%d\n", physical_width, physical_height);
         state->active_mode_impl->resize(state->active_mode_state, physical_width, physical_height);
    }

    return true; // Mode was set (either requested or default)
}


bool renderer_core_handle_command(RendererCoreState* state, const char* command) {
     if (!state || !command) return false;
     printf("Core: Handling command: %s\n", command);

     // Determine if it's potentially a mode-setting command based on common patterns
     // A more robust way might be needed if commands overlap with paths etc.
     bool potential_mode_command = (command[0] == '\0' || command[0] == '#' || command[0] == '/' || strstr(command, "%SETUP") != NULL);

     if (potential_mode_command) {
         // Let set_mode handle the logic, including fallback and checking if mode actually changed
         // We return true because we attempted to handle it as a mode command.
         // The return value of set_mode indicates success/failure of setting *that specific mode*.
         renderer_core_set_mode(state, command);
         return true; // Indicate command was processed (as a mode set attempt)
     }

     // If not a mode command, delegate to the active mode
     if (state->active_mode_impl && state->active_mode_impl->handle_command) {
         if (state->active_mode_impl->handle_command(state->active_mode_state, command)) {
             printf("Core: Command handled by active mode.\n");
             return true; // Handled by mode
         }
     }

     printf("Core: Command not handled.\n");
     return false; // Not handled
 }


bool renderer_core_render_frame(RendererCoreState* state, uint32_t time_ms) {
     if (!state || !state->egl_display || !state->egl_context || !state->egl_surface) {
         fprintf(stderr, "Core Error: Invalid EGL state in render_frame.\n");
         return false;
     }
     // Use current dimensions/scale from state
     if (state->current_logical_width <= 0 || state->current_logical_height <= 0 || state->current_scale <= 0) {
          fprintf(stderr, "Core Warning: Invalid dimensions/scale in state: %dx%d @ %.2f\n",
                  state->current_logical_width, state->current_logical_height, state->current_scale);
          return true; // Skip frame, not fatal
     }


     if (eglMakeCurrent(state->egl_display, state->egl_surface, state->egl_surface, state->egl_context) == EGL_FALSE) {
         fprintf(stderr, "Core Error: eglMakeCurrent failed in render_frame (EGL error: 0x%x)\n", eglGetError());
         return false; // Critical error
     }

     // Calculate delta time
     uint64_t current_time_ms = time_ms; // Use provided time directly
     uint32_t delta_time_ms_u32 = 0;
     if (state->last_update_time_ms != 0 && current_time_ms >= state->last_update_time_ms) {
          // Ensure difference fits in uint32_t, though unlikely to exceed 49 days...
         uint64_t diff = current_time_ms - state->last_update_time_ms;
         delta_time_ms_u32 = (diff > UINT32_MAX) ? UINT32_MAX : (uint32_t)diff;
     } else if (state->last_update_time_ms == 0) {
          delta_time_ms_u32 = 16; // Assume ~60fps for first frame delta
     }
     state->last_update_time_ms = current_time_ms;

     double time_delta_sec = delta_time_ms_u32 / 1000.0;
     // Clamp delta time to avoid huge jumps after lag
     const double MAX_DELTA_TIME_SEC = 0.1;
     if (time_delta_sec > MAX_DELTA_TIME_SEC) {
         time_delta_sec = MAX_DELTA_TIME_SEC;
     }

     // Set viewport using state values
     int physical_width = (int)round(state->current_logical_width * state->current_scale);
     int physical_height = (int)round(state->current_logical_height * state->current_scale);
     glViewport(0, 0, physical_width, physical_height);

     // Prepare render parameters
     RenderParams params = {
         .physical_width = physical_width,
         .physical_height = physical_height,
         .time_ms = time_ms, // Pass original time_ms from compositor
         .time_delta_sec = time_delta_sec,
         .common_vbo = state->quad_vbo
         // .core_state = state // Pass core state if modes need it
     };

     // Call the active mode's render function
     bool success = true;
     if (state->active_mode_impl && state->active_mode_impl->render) {
         success = state->active_mode_impl->render(state->active_mode_state, &params);
     } else {
         // Should have fallen back to default mode which has a render function.
         // If we reach here, something is wrong (e.g., default mode failed init critically).
         // Clear to an error color (e.g., bright red).
         fprintf(stderr, "Core CRITICAL Error: No active mode or render function available!\n");
         glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
         glClear(GL_COLOR_BUFFER_BIT);
         success = false; // Indicate an error occurred
     }

     // Check for GL errors (optional, for debugging)
     // GLenum err; while ((err = glGetError()) != GL_NO_ERROR) fprintf(stderr, "Core OpenGL Error: 0x%x\n", err);

     return success;
 }

bool renderer_core_swap_buffers(RendererCoreState* state) {
     if (!state || !state->egl_display || !state->egl_surface) {
          fprintf(stderr, "Core Error: Invalid EGL state in swap_buffers.\n");
          return false;
     }
     // Don't strictly need MakeCurrent here, but doesn't hurt usually.
     // if (eglMakeCurrent(...) == EGL_FALSE) { return false; }

     EGLBoolean swapped = eglSwapBuffers(state->egl_display, state->egl_surface);
     if (!swapped) {
         EGLint error = eglGetError();
         fprintf(stderr, "Core Error: eglSwapBuffers failed (EGL error: 0x%x)\n", error);
         // Check for EGL_BAD_SURFACE or EGL_CONTEXT_LOST which might require re-init
         return false;
     }
     return true;
 }

void renderer_core_resize(RendererCoreState* state, int logical_width, int logical_height, double scale) {
     if (!state) return;
     if (logical_width <= 0 || logical_height <= 0 || scale <= 0) {
         fprintf(stderr, "Core Warning: Ignoring invalid resize parameters: %dx%d @ %.2f\n", logical_width, logical_height, scale);
         return;
     }

     // Update core state
     state->current_logical_width = logical_width;
     state->current_logical_height = logical_height;
     state->current_scale = scale;

     int physical_width = (int)round(logical_width * scale);
     int physical_height = (int)round(logical_height * scale);

     printf("Core: Resizing to %dx%d logical, %dx%d physical (@%.2fx)\n",
            logical_width, logical_height, physical_width, physical_height, scale);


     // Resize EGL window surface
     if (state->egl_window) {
         wl_egl_window_resize(state->egl_window, physical_width, physical_height, 0, 0);
          printf("Core: Resized wl_egl_window.\n");
     } else {
          fprintf(stderr, "Core Warning: Cannot resize null EGL window.\n");
     }


     // Notify the active mode
     if (state->active_mode_impl && state->active_mode_impl->resize) {
          printf("Core: Notifying active mode of resize.\n");
         // Ensure context is current if mode's resize needs GL calls
         // if (eglMakeCurrent(...) == EGL_FALSE) { /* handle error */ }
         state->active_mode_impl->resize(state->active_mode_state, physical_width, physical_height);
     }
 }

bool renderer_core_needs_redraw(RendererCoreState* state) {
    if (state && state->active_mode_impl && state->active_mode_impl->needs_redraw) {
        return state->active_mode_impl->needs_redraw(state->active_mode_state);
    }
    // If no mode active or function missing, assume static (no redraw needed)
    return false;
}


// --- Static Helper Implementations ---

static bool init_egl_core(RendererCoreState* state) {
     printf("Core EGL: Initializing...\n");
     // Use state->wayland_display
     state->egl_display = eglGetDisplay((EGLNativeDisplayType)state->wayland_display);
     if (state->egl_display == EGL_NO_DISPLAY) { /*...*/ return false; }

     EGLint major, minor;
     if (eglInitialize(state->egl_display, &major, &minor) == EGL_FALSE) { /*...*/ return false; }
     printf("Core EGL: Version %d.%d\n", major, minor);

     // Config attributes (same as before)
     const EGLint config_attribs[] = {
         EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
         EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
         EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0, EGL_NONE
     };
     EGLint num_config;
     if (eglChooseConfig(state->egl_display, config_attribs, &state->egl_config, 1, &num_config) == EGL_FALSE || num_config == 0) {
         /*...*/ eglTerminate(state->egl_display); return false;
     }

     // Context attributes (ES 2.0)
     const EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
     state->egl_context = eglCreateContext(state->egl_display, state->egl_config, EGL_NO_CONTEXT, context_attribs);
     if (state->egl_context == EGL_NO_CONTEXT) { /*...*/ eglTerminate(state->egl_display); return false; }

     // Create Wayland EGL window
     int physical_width = (int)round(state->current_logical_width * state->current_scale);
     int physical_height = (int)round(state->current_logical_height * state->current_scale);
     // Use state->wayland_surface
     state->egl_window = wl_egl_window_create(state->wayland_surface, physical_width, physical_height);
     if (!state->egl_window) { /*...*/ eglDestroyContext(state->egl_display, state->egl_context); eglTerminate(state->egl_display); return false; }

     // Create EGL surface
     state->egl_surface = eglCreateWindowSurface(state->egl_display, state->egl_config, (EGLNativeWindowType)state->egl_window, NULL);
     if (state->egl_surface == EGL_NO_SURFACE) { /*...*/ wl_egl_window_destroy(state->egl_window); /*...*/ return false; }

     // Make context current for init_gl_core
     if (eglMakeCurrent(state->egl_display, state->egl_surface, state->egl_surface, state->egl_context) == EGL_FALSE) {
         fprintf(stderr, "Core EGL Error: eglMakeCurrent failed during init (error 0x%x)\n", eglGetError());
         /* Full cleanup */
         eglDestroySurface(state->egl_display, state->egl_surface);
         wl_egl_window_destroy(state->egl_window);
         eglDestroyContext(state->egl_display, state->egl_context);
         eglTerminate(state->egl_display);
         return false;
     }

     eglSwapInterval(state->egl_display, 1); // Enable VSync
     printf("Core EGL: Initialized successfully.\n");
     return true;
 }

static void cleanup_egl_core(RendererCoreState* state) {
     if (!state || state->egl_display == EGL_NO_DISPLAY) return;
     printf("Core EGL: Cleaning up...\n");
     eglMakeCurrent(state->egl_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
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

     if (state->egl_window) {
         wl_egl_window_destroy(state->egl_window);
         state->egl_window = NULL;
     }
      printf("Core EGL: Cleaned up.\n");
 }


static bool init_gl_core(RendererCoreState* state) {
     printf("Core GL: Initializing common resources...\n");
     // Create VBO for quad (X, Y, U, V)
     const GLfloat quad_vertices[] = {
         // Tri 1
         -1.0f, -1.0f, 0.0f, 0.0f, // BL
          1.0f, -1.0f, 1.0f, 0.0f, // BR
         -1.0f,  1.0f, 0.0f, 1.0f, // TL
         // Tri 2
          1.0f, -1.0f, 1.0f, 0.0f, // BR
          1.0f,  1.0f, 1.0f, 1.0f, // TR
         -1.0f,  1.0f, 0.0f, 1.0f  // TL
     };

     glGenBuffers(1, &state->quad_vbo);
     if (state->quad_vbo == 0) {
          fprintf(stderr, "Core GL Error: glGenBuffers failed.\n");
          return false;
     }
     glBindBuffer(GL_ARRAY_BUFFER, state->quad_vbo);
     glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);
     glBindBuffer(GL_ARRAY_BUFFER, 0); // Unbind

     // Set common GL states
     glDisable(GL_DEPTH_TEST);
     glDisable(GL_STENCIL_TEST);
     // Blending might be enabled by specific modes if needed
     // glEnable(GL_BLEND);
     // glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

     printf("Core GL: Common VBO created (ID: %u).\n", state->quad_vbo);
     return true;
}


static void cleanup_gl_core(RendererCoreState* state) {
     if (!state || state->quad_vbo == 0) return;

     // Ensure context is current before deleting GL resources
     if (state->egl_display != EGL_NO_DISPLAY && state->egl_context != EGL_NO_CONTEXT) {
         // It might be better practice to ensure MakeCurrent succeeds before delete
         if (eglGetCurrentContext() == state->egl_context || eglMakeCurrent(state->egl_display, state->egl_surface, state->egl_surface, state->egl_context))
         {
             printf("Core GL: Deleting common VBO (ID: %u)...\n", state->quad_vbo);
             glDeleteBuffers(1, &state->quad_vbo);
             state->quad_vbo = 0;
         } else {
              fprintf(stderr, "Core GL Warning: Could not make context current to delete VBO (Error: 0x%x).\n", eglGetError());
         }
     } else {
         fprintf(stderr, "Core GL Warning: Cannot delete VBO, EGL context/display invalid.\n");
     }
}


static bool populate_mode_registry(RendererCoreState* state) {
     if (!state || !state->mode_registry) return false;
     printf("Core: Populating mode registry...\n");
     bool success = true;

     // Insert each known mode implementation with its designated key
     success &= ht_insert(state->mode_registry, DEFAULT_MODE_KEY, &default_mode_interface);
     success &= ht_insert(state->mode_registry, COLOR_MODE_KEY, &color_mode_interface);
     success &= ht_insert(state->mode_registry, GRADIENT_MODE_KEY, &gradient_mode_interface);
     success &= ht_insert(state->mode_registry, GRID_MODE_KEY, &grid_mode_interface);
     success &= ht_insert(state->mode_registry, TEXTURE_MODE_KEY, &texture_mode_interface);
     success &= ht_insert(state->mode_registry, STARFIELD_MODE_KEY, &starfield_mode_interface);
     // success &= ht_insert(state->mode_registry, GRID_MODE_KEY, &grid_mode_interface);

     if (!success) {
          fprintf(stderr, "Core Error: Failed to insert one or more modes into registry!\n");
     } else {
          printf("Core: Mode registry populated.\n");
     }
     return success;
}

// Determines the mode 'type' (key) based on argument format
static const RenderModeInterface* find_mode_implementation(HashTable* registry, const char* arg) {
     const char* mode_key = DEFAULT_MODE_KEY; // Start assuming default

     if (arg) {
         if (arg[0] == '#') {
             mode_key = COLOR_MODE_KEY;
         } else if (strcmp(arg, "%SETUP_ANIMATION%") == 0) { // Specific command for gradient
             // mode_key = DEFAULT_MODE_KEY;
             mode_key = GRADIENT_MODE_KEY;
         } else if (strcmp(arg, "%SETUP_STARFIELD%") == 0) { // <-- ДОБАВЬ ЭТОТ БЛОК
            mode_key = STARFIELD_MODE_KEY;
         } else if (strcmp(arg, "%SETUP_GRID%") == 0) { // Example for grid
             mode_key = GRID_MODE_KEY;
         } else if (arg[0] != '\0') {
             // Assume anything else non-empty is a texture path for now
             // More robust checking (file exists, extension?) could be added here
             mode_key = TEXTURE_MODE_KEY;
         }
         // else: arg is empty string -> keep default
     } // else: arg is NULL -> keep default

     printf("Core: Determined mode key '%s' for argument '%s'\n", mode_key, arg ? arg : "(null)");
     const RenderModeInterface* impl = ht_lookup(registry, mode_key);
     if (!impl) {
          fprintf(stderr, "Core Warning: Mode key '%s' not found in registry!\n", mode_key);
          // Fallback to default explicitly if lookup failed
          impl = ht_lookup(registry, DEFAULT_MODE_KEY);
     }
     return impl;
}
