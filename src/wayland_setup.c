#include "wayland_setup.h"
#include "output.h" 

struct wl_display *g_display = NULL;
struct wl_registry *g_registry = NULL;
struct zwlr_layer_shell_v1 *g_layer_shell = NULL;
struct wl_compositor *g_compositor = NULL;
struct wl_shm *g_shm = NULL;
struct wp_viewporter *g_viewporter = NULL;
struct wp_fractional_scale_manager_v1 *g_fractional_scale_manager = NULL;

static void handle_global(void *data, struct wl_registry *reg, uint32_t name, const char *iface, uint32_t ver);
static void handle_global_remove(void *data, struct wl_registry *reg, uint32_t name);

static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove
};


bool init_wayland(void) {
    printf("Initializing Wayland connection...\n");
    g_display = wl_display_connect(NULL); 
    if (!g_display) {
        perror("wl_display_connect failed");
        fprintf(stderr, "Error: Failed to connect to Wayland display. Is WAYLAND_DISPLAY set?\n");
        return false;
    }
    printf("Connected to Wayland display (fd: %d).\n", wl_display_get_fd(g_display));

    g_registry = wl_display_get_registry(g_display);
    if (!g_registry) {
         perror("wl_display_get_registry failed");
         wl_display_disconnect(g_display); g_display = NULL;
         return false;
    }
     printf("Got Wayland registry.\n");

    
    wl_registry_add_listener(g_registry, &registry_listener, NULL);

    
    printf("Performing initial roundtrip to get globals...\n");
    if (wl_display_roundtrip(g_display) == -1) {
         perror("wl_display_roundtrip failed after getting registry");
         cleanup_wayland(); 
         return false;
    }
     printf("Initial roundtrip complete. Globals received:\n");
     printf("  Compositor: %p\n", (void*)g_compositor);
     printf("  SHM: %p\n", (void*)g_shm);
     printf("  Layer Shell: %p\n", (void*)g_layer_shell);
     printf("  Viewporter: %p%s\n", (void*)g_viewporter, g_viewporter ? "" : " (Not found)");
     printf("  Fractional Scale Mgr: %p%s\n", (void*)g_fractional_scale_manager, g_fractional_scale_manager ? "" : " (Not found)");


    
    if (!g_compositor || !g_shm || !g_layer_shell) {
        fprintf(stderr, "Error: Missing required Wayland globals (compositor, shm, or layer-shell).\n");
        cleanup_wayland();
        return false;
    }

    
    if (!g_viewporter) {
        fprintf(stderr, "Warning: wp_viewporter protocol not found. Scaling might be suboptimal.\n");
    }
    if (!g_fractional_scale_manager) {
        fprintf(stderr, "Warning: wp_fractional_scale_manager_v1 protocol not found. Fractional scaling will not be available.\n");
    }

    
    
    printf("Performing second roundtrip to process output listeners...\n");
    if (wl_display_roundtrip(g_display) == -1) {
         perror("wl_display_roundtrip failed after adding output listeners");
         cleanup_wayland();
         return false;
    }

    printf("Wayland initialization successful. Found %d output(s).\n", n_outputs);
    if (n_outputs == 0) {
         fprintf(stderr, "Warning: No outputs found during Wayland initialization.\n");
         
    }

    return true;
}


void cleanup_wayland(void) {
     printf("Cleaning up Wayland resources...\n");

     
     cleanup_all_outputs(); 

     
     if (g_layer_shell) {
         zwlr_layer_shell_v1_destroy(g_layer_shell);
         g_layer_shell = NULL;
         printf("  Layer shell destroyed.\n");
     }
     if (g_fractional_scale_manager) {
         wp_fractional_scale_manager_v1_destroy(g_fractional_scale_manager);
         g_fractional_scale_manager = NULL;
          printf("  Fractional scale manager destroyed.\n");
     }
    if (g_viewporter) {
         wp_viewporter_destroy(g_viewporter);
         g_viewporter = NULL;
          printf("  Viewporter destroyed.\n");
     }
     if (g_shm) {
         wl_shm_destroy(g_shm);
         g_shm = NULL;
          printf("  SHM destroyed.\n");
     }
     if (g_compositor) {
         wl_compositor_destroy(g_compositor);
         g_compositor = NULL;
          printf("  Compositor destroyed.\n");
     }
     if (g_registry) {
         wl_registry_destroy(g_registry);
         g_registry = NULL;
          printf("  Registry destroyed.\n");
     }

     
     if (g_display) {
         
         int flushed = wl_display_flush(g_display);
         if (flushed < 0 && errno != EPIPE) { 
             perror("Warning: wl_display_flush failed during cleanup");
         }
         wl_display_disconnect(g_display);
         g_display = NULL;
         printf("Disconnected from Wayland display.\n");
     }
      printf("Wayland cleanup finished.\n");
}





static void handle_global(void *data, struct wl_registry *reg, uint32_t name, const char *iface, uint32_t version) {
    UNUSED(data);
    

    
    if (strcmp(iface, wl_compositor_interface.name) == 0) {
        
        uint32_t bind_version = (version < 4) ? version : 4;
        g_compositor = wl_registry_bind(reg, name, &wl_compositor_interface, bind_version);
        printf("  -> Bound wl_compositor (version %u)\n", bind_version);
    } else if (strcmp(iface, wl_shm_interface.name) == 0) {
         
         g_shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
         printf("  -> Bound wl_shm (version 1)\n");
    } else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
        
         uint32_t bind_version = (version < 4) ? version : 4;
         g_layer_shell = wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface, bind_version);
         printf("  -> Bound zwlr_layer_shell_v1 (version %u)\n", bind_version);
    } else if (strcmp(iface, wp_viewporter_interface.name) == 0) {
         
         g_viewporter = wl_registry_bind(reg, name, &wp_viewporter_interface, 1);
         printf("  -> Bound wp_viewporter (version 1)\n");
    } else if (strcmp(iface, wp_fractional_scale_manager_v1_interface.name) == 0) {
        
         g_fractional_scale_manager = wl_registry_bind(reg, name, &wp_fractional_scale_manager_v1_interface, 1);
         printf("  -> Bound wp_fractional_scale_manager_v1 (version 1)\n");
    } else if (strcmp(iface, wl_output_interface.name) == 0) {
        
        
        uint32_t bind_version = (version < 4) ? version : 4;
        struct wl_output *wl_out = wl_registry_bind(reg, name, &wl_output_interface, bind_version);
         printf("  -> Found wl_output (name %u, version %u, bound %u)\n", name, version, bind_version);
        if (wl_out) {
            
            if (!add_output(wl_out, name)) {
                 fprintf(stderr, "Error: Failed to add output %u\n", name);
                 wl_output_release(wl_out); 
            }
        } else {
             fprintf(stderr, "Error: Failed to bind wl_output %u\n", name);
        }
    }
    
}


static void handle_global_remove(void *data, struct wl_registry *reg, uint32_t name) {
    UNUSED(data);
    UNUSED(reg);
    printf("Global Removed: Name: %u\n", name);

    
    struct client_output *output_to_remove = find_output_by_wl_name(name);
    if (output_to_remove) {
        printf("  -> Output %u is being removed.\n", name);
        
        remove_output(output_to_remove);
    }
    
    
}
