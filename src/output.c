#include "output.h"
#include <EGL/egl.h> 

#include "src/renderer/core.h"
#include "wayland_setup.h" 

#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <wayland-egl.h> 
#include "timer.h" 
                  
extern void update_animation_timer(void);

struct client_output *outputs_list_head = NULL;
int n_outputs = 0;


static void initialize_output_struct(struct client_output *output, struct wl_output *wl_out, uint32_t name);
static void destroy_output_resources(struct client_output *output);
static double calculate_current_scale(const struct client_output *output);
static void create_wayland_output_objects(struct client_output *output);


static void output_handle_geometry_impl(void *data, struct wl_output *o, int32_t x, int32_t y, int32_t w, int32_t h, int32_t sub, const char *mk, const char *md, int32_t tr);
static void output_handle_mode_impl(void *data, struct wl_output *o, uint32_t fl, int32_t w, int32_t h, int32_t re);
static void output_handle_done_impl(void *data, struct wl_output *o);
bool draw_frame_and_commit(struct client_output *output, uint32_t time_ms);

static void surface_handle_enter(void *data, struct wl_surface *s, struct wl_output *wl_out);
static void surface_handle_leave(void *data, struct wl_surface *s, struct wl_output *wl_out);

static void surface_handle_enter(void *data, struct wl_surface *s, struct wl_output *wl_out) {
    UNUSED(s); UNUSED(wl_out);
    struct client_output *output = data;
    if (output && !output->is_visible) {
        printf("O:%u Surface ENTERED. Notifying main.\n", output->wl_name);
        output->is_visible = true;
    }
}

static void surface_handle_leave(void *data, struct wl_surface *s, struct wl_output *wl_out) {
    UNUSED(s); UNUSED(wl_out);
    struct client_output *output = data;
    if (output && output->is_visible) {
        printf("O:%u Surface LEFT. Notifying main.\n", output->wl_name);
        output->is_visible = false;
    }
}

static const struct wl_surface_listener surface_listener = {
    .enter = surface_handle_enter,
    .leave = surface_handle_leave
};

struct client_output* find_output_by_wl_name(uint32_t name) {
    for (struct client_output *o = outputs_list_head; o; o = o->next) {
        if (o->wl_name == name) {
            return o;
        }
    }
    return NULL;
}

struct client_output* find_output_by_wl_output(struct wl_output *wl_out) {
     for (struct client_output *o = outputs_list_head; o; o = o->next) {
        if (o->wl_output == wl_out) {
            return o;
        }
    }
    return NULL;
}


static void initialize_output_struct(struct client_output *output, struct wl_output *wl_out, uint32_t name) {
      memset(output, 0, sizeof(*output));
      output->wl_output = wl_out;
      output->is_visible = false;
      output->wl_name = name;
      output->scale_factor = 1; 
      output->fractional_scale = 0.0; 
      output->scale_received = false;
      output->configured = false;
      output->renderer_state_gl = NULL; 
}

struct client_output* add_output(struct wl_output *wl_out, uint32_t name) {
    if (!wl_out) return NULL;
    if (find_output_by_wl_output(wl_out)) {
        fprintf(stderr, "Warning: Attempted to add already existing output (wl_output: %p, name: %u)\n", (void*)wl_out, name);
        return find_output_by_wl_output(wl_out); 
    }

    struct client_output *new_output = malloc(sizeof(*new_output));
    if (!new_output) {
        perror("Failed to allocate memory for new output");
        return NULL;
    }

    initialize_output_struct(new_output, wl_out, name);

    
    new_output->next = outputs_list_head;
    new_output->prev = NULL;
    if (outputs_list_head) {
        outputs_list_head->prev = new_output;
    }
    outputs_list_head = new_output;
    n_outputs++;

    printf("Output added: %u (Total: %d)\n", name, n_outputs);

    
    
    static const struct wl_output_listener output_listener = {
        .geometry = output_handle_geometry_impl, 
        .mode = output_handle_mode_impl,       
        .done = output_handle_done_impl,         
        .scale = output_handle_scale,            
        .name = output_handle_name,
        .description = output_handle_description 
    };
    wl_output_add_listener(new_output->wl_output, &output_listener, new_output);

    
    create_wayland_output_objects(new_output);

    return new_output;
}

static void output_handle_geometry_impl(void *data, struct wl_output *o, int32_t x, int32_t y, int32_t w, int32_t h, int32_t sub, const char *mk, const char *md, int32_t tr) {
    UNUSED(data); UNUSED(o); UNUSED(x); UNUSED(y); UNUSED(w); UNUSED(h); UNUSED(sub); UNUSED(mk); UNUSED(md); UNUSED(tr);
    
}

static void output_handle_mode_impl(void *data, struct wl_output *o, uint32_t flags, int32_t width, int32_t height, int32_t refresh) {
    UNUSED(data); UNUSED(o); UNUSED(flags); UNUSED(width); UNUSED(height); UNUSED(refresh);
     
}

static void output_handle_done_impl(void *data, struct wl_output *o) {
    UNUSED(data); UNUSED(o);
     
}

static void destroy_output_resources(struct client_output *output) {
      printf("Destroying resources for Output %u\n", output->wl_name);
      output->configured = false; 

      if (output->is_visible) {
        output->is_visible = false;
      }

      
      if (output->renderer_state_gl) {
          renderer_core_cleanup(output->renderer_state_gl); 
          output->renderer_state_gl = NULL;
          update_animation_timer(); 
          printf("  -> Renderer GL cleaned up.\n");
      }
      

      if (output->layer_surface) {
          zwlr_layer_surface_v1_destroy(output->layer_surface);
          output->layer_surface = NULL;
           printf("  -> Layer surface destroyed.\n");
      }
      if (output->viewport) {
          wp_viewport_destroy(output->viewport);
          output->viewport = NULL;
           printf("  -> Viewport destroyed.\n");
      }
      if (output->fractional_scale_obj) {
          wp_fractional_scale_v1_destroy(output->fractional_scale_obj);
          output->fractional_scale_obj = NULL;
           printf("  -> Fractional scale object destroyed.\n");
      }
      
      if (output->surface) {
          wl_surface_destroy(output->surface);
          output->surface = NULL;
          printf("  -> Surface destroyed.\n");
      }
      if (output->wl_output) {
          
          wl_output_release(output->wl_output);
          output->wl_output = NULL;
          printf("  -> wl_output released.\n");
      }
}

void remove_output(struct client_output *output) {
    if (!output) return;

    uint32_t name = output->wl_name;
    printf("Removing Output %u...\n", name);

    destroy_output_resources(output);

    
    if (output->prev) {
        output->prev->next = output->next;
    } else {
        outputs_list_head = output->next; 
    }
    if (output->next) {
        output->next->prev = output->prev;
    }

    free(output);
    n_outputs--;
    printf("Output %u removed. (Total: %d)\n", name, n_outputs);
}

void cleanup_all_outputs(void) {
    printf("Cleaning up all outputs...\n");
    while (outputs_list_head) {
        remove_output(outputs_list_head); 
    }
    n_outputs = 0; 
     printf("All outputs cleaned up.\n");
}



static const struct zwlr_layer_surface_v1_listener layer_surface_listener = {
    .configure = layer_surface_handle_configure,
    .closed = layer_surface_handle_closed
};

static void create_wayland_output_objects(struct client_output *output) {
    
    if (!output || !output->wl_output || !g_compositor || !g_layer_shell) {
         fprintf(stderr, "Error: Missing necessary globals or output object to create surfaces for O:%u\n", output ? output->wl_name : 0);
        return;
    }
    if (output->surface) {
        printf("Warning: Surface already exists for O:%u\n", output->wl_name);
        return; 
    }


    output->surface = wl_compositor_create_surface(g_compositor);
    if (!output->surface) {
        fprintf(stderr, "Error: wl_compositor_create_surface failed for O:%u\n", output->wl_name);
        return;
    }

    wl_surface_add_listener(output->surface, &surface_listener, output);

    printf("O:%u Surface created.\n", output->wl_name);

    
    output->layer_surface = zwlr_layer_shell_v1_get_layer_surface(
        g_layer_shell, output->surface, output->wl_output,
        ZWLR_LAYER_SHELL_V1_LAYER_BACKGROUND, "wallpaper-app");

    if (!output->layer_surface) {
        fprintf(stderr, "Error: zwlr_layer_shell_v1_get_layer_surface failed for O:%u\n", output->wl_name);
        wl_surface_destroy(output->surface);
        output->surface = NULL;
        return;
    }
     printf("O:%u Layer surface created.\n", output->wl_name);

    zwlr_layer_surface_v1_add_listener(output->layer_surface, &layer_surface_listener, output);

    
    zwlr_layer_surface_v1_set_size(output->layer_surface, 0, 0);
    zwlr_layer_surface_v1_set_anchor(output->layer_surface,
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP |
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_BOTTOM |
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT |
                                     ZWLR_LAYER_SURFACE_V1_ANCHOR_RIGHT);
    zwlr_layer_surface_v1_set_exclusive_zone(output->layer_surface, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(output->layer_surface, ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);

    
    wl_surface_commit(output->surface);
     printf("O:%u Initial surface commit done.\n", output->wl_name);
}




void output_handle_scale(void *data, struct wl_output *o, int32_t factor) {
    struct client_output *out = data;
    UNUSED(o);
    if (!out) return;

    printf("Output %u: Received integer scale factor: %d\n", out->wl_name, factor);
    if (factor <= 0) {
        fprintf(stderr, "Warning: Received non-positive scale factor (%d) for O:%u. Using 1.\n", factor, out->wl_name);
        factor = 1;
    }

    
    if (out->scale_factor != factor) {
        out->scale_factor = factor;
        out->scale_received = true; 
        
        check_and_reinit_renderer(out); 
    }
}

void output_handle_name(void *data, struct wl_output *o, const char *name) {
     struct client_output *out = data; UNUSED(o);
     if (!out) return;
     printf("Output %u Name: %s\n", out->wl_name, name ? name : "<null>");
     
}

void output_handle_description(void *data, struct wl_output *o, const char *desc) {
     struct client_output *out = data; UNUSED(o);
     if (!out) return;
     printf("Output %u Description: %s\n", out->wl_name, desc ? desc : "<null>");
     
}


static const struct wp_fractional_scale_v1_listener fractional_scale_listener = {
    .preferred_scale = fractional_scale_handle_preferred_scale,
};

void fractional_scale_handle_preferred_scale(void *data,
                                             struct wp_fractional_scale_v1 *fract_scale,
                                             uint32_t scale_numerator) {
    struct client_output *output = data;
    if (!output) return;
    UNUSED(fract_scale);

    double new_fractional_scale = (double)scale_numerator / 120.0;

    printf("Output %u: Received fractional scale numerator: %u => scale: %.2f\n",
           output->wl_name, scale_numerator, new_fractional_scale);

     if (new_fractional_scale <= 0) {
        fprintf(stderr, "Warning: Received non-positive fractional scale (%.2f) for O:%u. Ignoring.\n", new_fractional_scale, output->wl_name);
        return; 
    }

    
    if (fabs(output->fractional_scale - new_fractional_scale) > 1e-6) {
         output->fractional_scale = new_fractional_scale;
         output->scale_received = true; 
         
         check_and_reinit_renderer(output); 
    }
}




void layer_surface_handle_configure(void *data, struct zwlr_layer_surface_v1 *ls, uint32_t serial, uint32_t width, uint32_t height) {
    struct client_output *output = data;
    UNUSED(ls);
    if (!output) return;

    printf("Layer surface configure O:%u Serial:%u Logical Size: %ux%u\n",
           output->wl_name, serial, width, height);

     if (width == 0 || height == 0) {
         fprintf(stderr, "Warning: Received configure event with zero dimensions for O:%u. Waiting for valid size.\n", output->wl_name);
         zwlr_layer_surface_v1_ack_configure(output->layer_surface, serial);
         output->configured = false;
         if (output->renderer_state_gl) {
              renderer_core_cleanup(output->renderer_state_gl);
              output->renderer_state_gl = NULL;
         }
         return;
     }

    
    
    output->logical_width = (int)width;
    output->logical_height = (int)height;
    output->configured = true;

    
    zwlr_layer_surface_v1_ack_configure(output->layer_surface, serial);

    
    if (g_fractional_scale_manager && !output->fractional_scale_obj) {
        output->fractional_scale_obj = wp_fractional_scale_manager_v1_get_fractional_scale(
            g_fractional_scale_manager, output->surface);
        if (output->fractional_scale_obj) {
            wp_fractional_scale_v1_add_listener(output->fractional_scale_obj, &fractional_scale_listener, output);
            printf("O:%u Added fractional scale listener.\n", output->wl_name);
        } else {
            fprintf(stderr, "O:%u Failed to get fractional_scale object. Fractional scaling might not work.\n", output->wl_name);
            output->fractional_scale = 0.0;
        }
    } else if (!g_fractional_scale_manager) {
        output->fractional_scale = 0.0;
        if (output->scale_factor > 0) output->scale_received = true;
    }

    
    
    
    check_and_reinit_renderer(output);
}


void layer_surface_handle_closed(void *data, struct zwlr_layer_surface_v1 *ls) {
    
    struct client_output *output = data;
    UNUSED(ls);
    if (!output) return;

    fprintf(stderr, "Layer surface closed for O:%u. Cleaning up associated resources.\n", output->wl_name);
    remove_output(output);
}

static double calculate_current_scale(const struct client_output *output) {
    
    if (output->fractional_scale > 0) {
        return output->fractional_scale;
    }
    if (output->scale_factor > 0) {
        return (double)output->scale_factor;
    }
    return 1.0;
}
void check_and_reinit_renderer(struct client_output *output) {
    if (!output || !output->surface) return; 

    if (!output->configured || !output->scale_received || output->logical_width <= 0 || output->logical_height <= 0) {
        return;
    }

    double scale = calculate_current_scale(output);
    int physical_width = (int)round(output->logical_width * scale);
    int physical_height = (int)round(output->logical_height * scale);

    
    bool need_full_reinit = !output->renderer_state_gl;

    if (need_full_reinit) {
         printf("O:%u Initializing GL renderer. Logical:%dx%d Scale:%.2f (Physical: %dx%d)\n",
               output->wl_name, output->logical_width, output->logical_height, scale, physical_width, physical_height);

        
        if (output->renderer_state_gl) {
            renderer_core_cleanup(output->renderer_state_gl);
            output->renderer_state_gl = NULL;
        }

        
        if (!g_display) {
            fprintf(stderr, "Error: O:%u Cannot initialize renderer, g_display is NULL!\n", output->wl_name);
            return;
        }
        const char *cmd = config_monitor_get_current_command();
        output->renderer_state_gl = renderer_core_init(g_display,
                                                    output->surface,
                                                    output->logical_width,
                                                    output->logical_height,
                                                    scale,
                                                    cmd);

        if (!output->renderer_state_gl) {
            fprintf(stderr, "Error: Failed to initialize GL renderer for O:%u\n", output->wl_name);
            output->configured = false;
            return;
        }
        printf("O:%u GL Renderer initialized successfully.\n", output->wl_name);

        update_animation_timer();

        if (output->renderer_state_gl) {
            printf("O:%u Resizing EGL window via renderer function to %dx%d\n",
                   output->wl_name, physical_width, physical_height);
            renderer_core_resize(output->renderer_state_gl, physical_width, physical_height, scale);
        }

        
        printf("O:%u Triggering initial render after init.\n", output->wl_name);
        struct timespec ts;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        uint32_t ms = (uint32_t)(((uint64_t)ts.tv_sec * 1000) + ((uint64_t)ts.tv_nsec / 1000000));
        present_output_frame(output, ms);


    } else {
        if (output->renderer_state_gl) {
            printf("O:%u Resizing EGL window (update) to %dx%d\n", output->wl_name, physical_width, physical_height);
            renderer_core_resize(output->renderer_state_gl, physical_width, physical_height, scale);

            
            
            
             printf("O:%u Triggering render after EGL window resize.\n", output->wl_name);
             struct timespec ts;
             clock_gettime(CLOCK_MONOTONIC, &ts);
             uint32_t ms = (uint32_t)(((uint64_t)ts.tv_sec * 1000) + ((uint64_t)ts.tv_nsec / 1000000));
             present_output_frame(output, ms);

        } else if (output->renderer_state_gl) {
            
             fprintf(stderr, "Warning: O:%u Renderer state exists, but EGL window is NULL during resize check.\n", output->wl_name);
        }
         
         
         
    }
}




bool draw_frame_and_commit(struct client_output *output, uint32_t time_ms) {
    if (!output || !output->configured || !output->surface || !output->renderer_state_gl ||
        output->logical_width <= 0 || output->logical_height <= 0)
    {
        return false;
    }

    RendererCoreState *gl_state = output->renderer_state_gl;

    
    bool success = renderer_core_render_frame(gl_state, time_ms);

    if (!success) {
        fprintf(stderr, "Error: OpenGL Renderer failed rendering frame for O:%u\n", output->wl_name);
        
    }

    
    if (g_viewporter) {
        if (!output->viewport) { 
             output->viewport = wp_viewporter_get_viewport(g_viewporter, output->surface);
             if (!output->viewport) {
                 fprintf(stderr, "Error: Failed to get viewport for O:%u surface. Scaling may be incorrect.\n", output->wl_name);
             } else {
                 printf("O:%u Viewport created.\n", output->wl_name);
             }
        }
        if (output->viewport) {
             
             wp_viewport_set_destination(output->viewport, output->logical_width, output->logical_height);
             
        }
     } else {
         
         static bool vp_warned = false;
         if (!vp_warned) {
             fprintf(stderr, "Warning: wp_viewporter protocol not available! Scaling might be blurry or incorrect.\n");
             vp_warned = true;
         }
     }

    bool swapped = renderer_core_swap_buffers(gl_state);

    if (!swapped) {
        fprintf(stderr, "Error: Swap buffers failed for O:%u. Attempting reinitialization.\n", output->wl_name);
        output->configured = false;
        if (output->renderer_state_gl) {
            renderer_core_cleanup(output->renderer_state_gl);
            output->renderer_state_gl = NULL;
        }
        return false;
    }

    wl_surface_commit(output->surface);

    return true;
}

bool present_output_frame(struct client_output *output, uint32_t time_ms) {
    
    if (!draw_frame_and_commit(output, time_ms)) {
        return false; 
    }

    // if (output->renderer_state_gl && renderer_core_needs_redraw(output->renderer_state_gl)) {
    //     struct wl_callback *callback = wl_surface_frame(output->surface);
    //     if (!callback) {
    //         fprintf(stderr, "O:%u Error: wl_surface_frame failed. Animation will stop.\n", output->wl_name);
    //     } else {
    //         wl_callback_add_listener(callback, &frame_listener, output);
    //     }
    // }

    return true;
}
