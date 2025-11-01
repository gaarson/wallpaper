
#include "core.h"
#include "mode.h"
#include "./../hash_table.h"
#include "./../shader_utils.h" 


#include "default.h"
#include "color.h"
#include "gradient.h"
#include "texture.h"
#include "grid.h"
#include "starfield.h"


#include <stdlib.h>
#include <stdio.h>
#include <string.h> 
#include <math.h>   
#include <time.h>   


#define MODE_REGISTRY_SIZE 16 
                              

#define DEFAULT_MODE_KEY "default"

#define COLOR_MODE_KEY "color"
#define GRADIENT_MODE_KEY "gradient"
#define TEXTURE_MODE_KEY "texture"
#define GRID_MODE_KEY "grid"
#define STARFIELD_MODE_KEY "starfield"


static bool init_egl_core(RendererCoreState* state);
static void cleanup_egl_core(RendererCoreState* state);
static bool init_gl_core(RendererCoreState* state);
static void cleanup_gl_core(RendererCoreState* state);
static bool populate_mode_registry(RendererCoreState* state);



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
    state->last_update_time_ms = 0; 

    
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

    
    if (!init_egl_core(state)) {
        fprintf(stderr, "Core Error: EGL initialization failed.\n");
        ht_destroy(state->mode_registry);
        free(state); return NULL;
    }

    
    if (!init_gl_core(state)) {
        fprintf(stderr, "Core Error: Core OpenGL resource initialization failed.\n");
        cleanup_egl_core(state);
        ht_destroy(state->mode_registry);
        free(state); return NULL;
    }

    
    if (!renderer_core_set_mode(state, initial_arg)) {
         fprintf(stderr, "Core Warning: Failed to set initial mode '%s'. Default mode should be active.\n", initial_arg ? initial_arg : "default");
         
    }

    printf("Core: Initialization complete.\n");
    return state;
}

void renderer_core_cleanup(RendererCoreState* state) {
    if (!state) return;
    printf("Core: Cleaning up...\n");

    
    if (state->active_mode_impl && state->active_mode_impl->cleanup) {
        printf("Core: Cleaning up active mode...\n");
        state->active_mode_impl->cleanup(state->active_mode_state);
    }
    state->active_mode_impl = NULL;
    state->active_mode_state = NULL;

    
    cleanup_gl_core(state); 

    
    cleanup_egl_core(state); 

    
    ht_destroy(state->mode_registry);
    state->mode_registry = NULL;

    free(state);
    printf("Core: Cleanup complete.\n");
}


bool renderer_core_set_mode(RendererCoreState* state, const char* full_command_arg) {
    if (!state || !state->mode_registry) return false;
    printf("Core: Setting mode with full command: %s\n", full_command_arg ? full_command_arg : "(null)");

    char* mode_key = NULL;
    char* mode_arg = NULL;
    char* command_copy = NULL;

    const RenderModeInterface* requested_impl = NULL;

    if (full_command_arg && full_command_arg[0] != '\0') {
        
        command_copy = strdup(full_command_arg);
        if (!command_copy) {
            perror("Core set_mode strdup");
            goto fallback_to_default;
        }

        char* saveptr;
        
        mode_key = strtok_r(command_copy, " \t", &saveptr);

        if (mode_key) {
            
            requested_impl = ht_lookup(state->mode_registry, mode_key);
            
            if (requested_impl) {
                
                mode_arg = saveptr;
                
                if (mode_arg) {
                    while (*mode_arg == ' ' || *mode_arg == '\t') {
                        mode_arg++;
                    }
                    if (*mode_arg == '\0') {
                        mode_arg = NULL; 
                    }
                }
                printf("Core: Parsed mode key '%s', arg '%s'\n", mode_key, mode_arg ? mode_arg : "(null)");
            } else {
                 printf("Core Warning: Mode key '%s' not found in registry. Falling back.\n", mode_key);
            }
        }
    }

fallback_to_default:
    if (!requested_impl) {
        
        mode_key = DEFAULT_MODE_KEY;
        mode_arg = NULL;
        requested_impl = ht_lookup(state->mode_registry, DEFAULT_MODE_KEY);

        if (!requested_impl) {
             fprintf(stderr, "Core CRITICAL Error: Default mode implementation not found!\n");
             if (command_copy) free(command_copy);
             
             if (state->active_mode_impl && state->active_mode_impl->cleanup) {
                 state->active_mode_impl->cleanup(state->active_mode_state);
             }
             state->active_mode_impl = NULL;
             state->active_mode_state = NULL;
             return false;
        }
    }
    
    if (state->active_mode_impl && state->active_mode_impl->cleanup) {
        printf("Core: Cleaning up previous mode...\n");
        state->active_mode_impl->cleanup(state->active_mode_state);
    }
    state->active_mode_impl = NULL;
    state->active_mode_state = NULL;

    
    printf("Core: Initializing requested mode...\n");

    void* new_state = NULL;
    bool init_success = false;

    
    if (eglMakeCurrent(state->egl_display, state->egl_surface, state->egl_surface, state->egl_context) == EGL_FALSE) {
         fprintf(stderr, "Core Error: eglMakeCurrent failed before mode init (EGL error: 0x%x)\n", eglGetError());
         if (command_copy) free(command_copy);
         return false;
    }
    
    
    new_state = requested_impl->init(mode_arg, state->quad_vbo); 
    init_success = (new_state != NULL);

    if (command_copy) free(command_copy);

    if (init_success) {
        printf("Core: Mode initialized successfully.\n");
        state->active_mode_impl = requested_impl;
        state->active_mode_state = new_state;
    } else {
        fprintf(stderr, "Core Error: Failed to initialize requested mode. Falling back to default.\n");
        
        if(requested_impl->cleanup) requested_impl->cleanup(new_state); 

        const RenderModeInterface* default_impl = ht_lookup(state->mode_registry, DEFAULT_MODE_KEY);
        if (default_impl && default_impl->init) {
             printf("Core: Initializing default mode...\n");
             
             new_state = default_impl->init(NULL, state->quad_vbo);
             if (new_state) {
                 state->active_mode_impl = default_impl;
                 state->active_mode_state = new_state;
                 printf("Core: Default mode initialized successfully.\n");
             } else {
                 fprintf(stderr, "Core CRITICAL Error: Failed to initialize default mode!\n");
                 
                 return false; 
             }
        } else {
             fprintf(stderr, "Core CRITICAL Error: Default mode implementation or its init not found!\n");
             return false; 
        }
    }

    
    if (state->active_mode_impl && state->active_mode_impl->resize) {
         int physical_width = (int)round(state->current_logical_width * state->current_scale);
         int physical_height = (int)round(state->current_logical_height * state->current_scale);
          printf("Core: Notifying new mode of initial size %dx%d\n", physical_width, physical_height);
         state->active_mode_impl->resize(state->active_mode_state, physical_width, physical_height);
    }

    return true; 
}


bool renderer_core_handle_command(RendererCoreState* state, const char* command) {
    if (!state || !command) return false;
    printf("Core: Handling command: %s\n", command);

    if (state->active_mode_impl && state->active_mode_impl->handle_command) {
        if (state->active_mode_impl->handle_command(state->active_mode_state, command)) {
            printf("Core: Command handled by active mode.\n");
            return true; 
        }
    }

    printf("Core: Command not handled by active mode.\n");
    return false; 
}

bool renderer_core_render_frame(RendererCoreState* state, uint32_t time_ms) {
     if (!state || !state->egl_display || !state->egl_context || !state->egl_surface) {
         fprintf(stderr, "Core Error: Invalid EGL state in render_frame.\n");
         return false;
     }
     
     if (state->current_logical_width <= 0 || state->current_logical_height <= 0 || state->current_scale <= 0) {
          fprintf(stderr, "Core Warning: Invalid dimensions/scale in state: %dx%d @ %.2f\n",
                  state->current_logical_width, state->current_logical_height, state->current_scale);
          return true; 
     }


     if (eglMakeCurrent(state->egl_display, state->egl_surface, state->egl_surface, state->egl_context) == EGL_FALSE) {
         fprintf(stderr, "Core Error: eglMakeCurrent failed in render_frame (EGL error: 0x%x)\n", eglGetError());
         return false; 
     }

     
     uint64_t current_time_ms = time_ms; 
     uint32_t delta_time_ms_u32 = 0;
     if (state->last_update_time_ms != 0 && current_time_ms >= state->last_update_time_ms) {
          
         uint64_t diff = current_time_ms - state->last_update_time_ms;
         delta_time_ms_u32 = (diff > UINT32_MAX) ? UINT32_MAX : (uint32_t)diff;
     } else if (state->last_update_time_ms == 0) {
          delta_time_ms_u32 = 16; 
     }
     state->last_update_time_ms = current_time_ms;

     double time_delta_sec = delta_time_ms_u32 / 1000.0;
     
     const double MAX_DELTA_TIME_SEC = 0.1;
     if (time_delta_sec > MAX_DELTA_TIME_SEC) {
         time_delta_sec = MAX_DELTA_TIME_SEC;
     }

     
     int physical_width = (int)round(state->current_logical_width * state->current_scale);
     int physical_height = (int)round(state->current_logical_height * state->current_scale);
     glViewport(0, 0, physical_width, physical_height);

     
     RenderParams params = {
         .physical_width = physical_width,
         .physical_height = physical_height,
         .time_ms = time_ms, 
         .time_delta_sec = time_delta_sec,
         .common_vbo = state->quad_vbo
         
     };

     
     bool success = true;
     if (state->active_mode_impl && state->active_mode_impl->render) {
         success = state->active_mode_impl->render(state->active_mode_state, &params);
     } else {
         
         
         
         fprintf(stderr, "Core CRITICAL Error: No active mode or render function available!\n");
         glClearColor(1.0f, 0.0f, 0.0f, 1.0f);
         glClear(GL_COLOR_BUFFER_BIT);
         success = false; 
     }

     
     

     return success;
 }

bool renderer_core_swap_buffers(RendererCoreState* state) {
     if (!state || !state->egl_display || !state->egl_surface) {
          fprintf(stderr, "Core Error: Invalid EGL state in swap_buffers.\n");
          return false;
     }
     
     

     EGLBoolean swapped = eglSwapBuffers(state->egl_display, state->egl_surface);
     if (!swapped) {
         EGLint error = eglGetError();
         fprintf(stderr, "Core Error: eglSwapBuffers failed (EGL error: 0x%x)\n", error);
         
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

     
     state->current_logical_width = logical_width;
     state->current_logical_height = logical_height;
     state->current_scale = scale;

     int physical_width = (int)round(logical_width * scale);
     int physical_height = (int)round(logical_height * scale);

     printf("Core: Resizing to %dx%d logical, %dx%d physical (@%.2fx)\n",
            logical_width, logical_height, physical_width, physical_height, scale);


     
     if (state->egl_window) {
         wl_egl_window_resize(state->egl_window, physical_width, physical_height, 0, 0);
          printf("Core: Resized wl_egl_window.\n");
     } else {
          fprintf(stderr, "Core Warning: Cannot resize null EGL window.\n");
     }


     
     if (state->active_mode_impl && state->active_mode_impl->resize) {
          printf("Core: Notifying active mode of resize.\n");
         
         
         state->active_mode_impl->resize(state->active_mode_state, physical_width, physical_height);
     }
 }

bool renderer_core_needs_redraw(RendererCoreState* state) {
    if (state && state->active_mode_impl && state->active_mode_impl->needs_redraw) {
        return state->active_mode_impl->needs_redraw(state->active_mode_state);
    }
    
    return false;
}




static bool init_egl_core(RendererCoreState* state) {
     printf("Core EGL: Initializing...\n");
     
     state->egl_display = eglGetDisplay((EGLNativeDisplayType)state->wayland_display);
     if (state->egl_display == EGL_NO_DISPLAY) { /*...*/ return false; }

     EGLint major, minor;
     if (eglInitialize(state->egl_display, &major, &minor) == EGL_FALSE) { /*...*/ return false; }
     printf("Core EGL: Version %d.%d\n", major, minor);

     
     const EGLint config_attribs[] = {
         EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
         EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
         EGL_DEPTH_SIZE, 0, EGL_STENCIL_SIZE, 0, EGL_NONE
     };
     EGLint num_config;
     if (eglChooseConfig(state->egl_display, config_attribs, &state->egl_config, 1, &num_config) == EGL_FALSE || num_config == 0) {
         /*...*/ eglTerminate(state->egl_display); return false;
     }

     
     const EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
     state->egl_context = eglCreateContext(state->egl_display, state->egl_config, EGL_NO_CONTEXT, context_attribs);
     if (state->egl_context == EGL_NO_CONTEXT) { /*...*/ eglTerminate(state->egl_display); return false; }

     
     int physical_width = (int)round(state->current_logical_width * state->current_scale);
     int physical_height = (int)round(state->current_logical_height * state->current_scale);
     
     state->egl_window = wl_egl_window_create(state->wayland_surface, physical_width, physical_height);
     if (!state->egl_window) { /*...*/ eglDestroyContext(state->egl_display, state->egl_context); eglTerminate(state->egl_display); return false; }

     
     state->egl_surface = eglCreateWindowSurface(state->egl_display, state->egl_config, (EGLNativeWindowType)state->egl_window, NULL);
     if (state->egl_surface == EGL_NO_SURFACE) { /*...*/ wl_egl_window_destroy(state->egl_window); /*...*/ return false; }

     
     if (eglMakeCurrent(state->egl_display, state->egl_surface, state->egl_surface, state->egl_context) == EGL_FALSE) {
         fprintf(stderr, "Core EGL Error: eglMakeCurrent failed during init (error 0x%x)\n", eglGetError());
         /* Full cleanup */
         eglDestroySurface(state->egl_display, state->egl_surface);
         wl_egl_window_destroy(state->egl_window);
         eglDestroyContext(state->egl_display, state->egl_context);
         eglTerminate(state->egl_display);
         return false;
     }

     eglSwapInterval(state->egl_display, 1); 
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
     
     const GLfloat quad_vertices[] = {
         
         -1.0f, -1.0f, 0.0f, 0.0f, 
          1.0f, -1.0f, 1.0f, 0.0f, 
         -1.0f,  1.0f, 0.0f, 1.0f, 
         
          1.0f, -1.0f, 1.0f, 0.0f, 
          1.0f,  1.0f, 1.0f, 1.0f, 
         -1.0f,  1.0f, 0.0f, 1.0f  
     };

     glGenBuffers(1, &state->quad_vbo);
     if (state->quad_vbo == 0) {
          fprintf(stderr, "Core GL Error: glGenBuffers failed.\n");
          return false;
     }
     glBindBuffer(GL_ARRAY_BUFFER, state->quad_vbo);
     glBufferData(GL_ARRAY_BUFFER, sizeof(quad_vertices), quad_vertices, GL_STATIC_DRAW);
     glBindBuffer(GL_ARRAY_BUFFER, 0); 

     
     glDisable(GL_DEPTH_TEST);
     glDisable(GL_STENCIL_TEST);
     
     
     

     printf("Core GL: Common VBO created (ID: %u).\n", state->quad_vbo);
     return true;
}


static void cleanup_gl_core(RendererCoreState* state) {
     if (!state || state->quad_vbo == 0) return;

     
     if (state->egl_display != EGL_NO_DISPLAY && state->egl_context != EGL_NO_CONTEXT) {
         
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

     
     success &= ht_insert(state->mode_registry, DEFAULT_MODE_KEY, &default_mode_interface);
     success &= ht_insert(state->mode_registry, COLOR_MODE_KEY, &color_mode_interface);
     success &= ht_insert(state->mode_registry, GRADIENT_MODE_KEY, &gradient_mode_interface);
     success &= ht_insert(state->mode_registry, GRID_MODE_KEY, &grid_mode_interface);
     success &= ht_insert(state->mode_registry, TEXTURE_MODE_KEY, &texture_mode_interface);
     success &= ht_insert(state->mode_registry, STARFIELD_MODE_KEY, &starfield_mode_interface);
     

     if (!success) {
          fprintf(stderr, "Core Error: Failed to insert one or more modes into registry!\n");
     } else {
          printf("Core: Mode registry populated.\n");
     }
     return success;
}
