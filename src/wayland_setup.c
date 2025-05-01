#include "wayland_setup.h"
#include "output.h" // Для add_output, remove_output, find_output_by_wl_name

// --- Глобальные переменные Wayland (Определения) ---
struct wl_display *g_display = NULL;
struct wl_registry *g_registry = NULL;
struct zwlr_layer_shell_v1 *g_layer_shell = NULL;
struct wl_compositor *g_compositor = NULL;
struct wl_shm *g_shm = NULL;
struct wp_viewporter *g_viewporter = NULL;
struct wp_fractional_scale_manager_v1 *g_fractional_scale_manager = NULL;

// --- Статические прототипы (для обработчиков листенеров) ---
static void handle_global(void *data, struct wl_registry *reg, uint32_t name, const char *iface, uint32_t ver);
static void handle_global_remove(void *data, struct wl_registry *reg, uint32_t name);

// --- Registry Listener ---
static const struct wl_registry_listener registry_listener = {
    .global = handle_global,
    .global_remove = handle_global_remove
};

// --- Инициализация Wayland ---
bool init_wayland(void) {
    printf("Initializing Wayland connection...\n");
    g_display = wl_display_connect(NULL); // Подключаемся к Wayland серверу (обычно через WAYLAND_DISPLAY)
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

    // Добавляем слушателя для получения глобальных объектов
    wl_registry_add_listener(g_registry, &registry_listener, NULL);

    // Первый roundtrip для получения списка глобальных объектов
    printf("Performing initial roundtrip to get globals...\n");
    if (wl_display_roundtrip(g_display) == -1) {
         perror("wl_display_roundtrip failed after getting registry");
         cleanup_wayland(); // Используем новую функцию очистки
         return false;
    }
     printf("Initial roundtrip complete. Globals received:\n");
     printf("  Compositor: %p\n", (void*)g_compositor);
     printf("  SHM: %p\n", (void*)g_shm);
     printf("  Layer Shell: %p\n", (void*)g_layer_shell);
     printf("  Viewporter: %p%s\n", (void*)g_viewporter, g_viewporter ? "" : " (Not found)");
     printf("  Fractional Scale Mgr: %p%s\n", (void*)g_fractional_scale_manager, g_fractional_scale_manager ? "" : " (Not found)");


    // Проверяем наличие обязательных интерфейсов
    if (!g_compositor || !g_shm || !g_layer_shell) {
        fprintf(stderr, "Error: Missing required Wayland globals (compositor, shm, or layer-shell).\n");
        cleanup_wayland();
        return false;
    }

    // Предупреждения о необязательных интерфейсах
    if (!g_viewporter) {
        fprintf(stderr, "Warning: wp_viewporter protocol not found. Scaling might be suboptimal.\n");
    }
    if (!g_fractional_scale_manager) {
        fprintf(stderr, "Warning: wp_fractional_scale_manager_v1 protocol not found. Fractional scaling will not be available.\n");
    }

    // Второй roundtrip, чтобы убедиться, что все добавленные слушатели output успели отработать
    // и мы получили начальные состояния (имена, масштабы и т.д.)
    printf("Performing second roundtrip to process output listeners...\n");
    if (wl_display_roundtrip(g_display) == -1) {
         perror("wl_display_roundtrip failed after adding output listeners");
         cleanup_wayland();
         return false;
    }

    printf("Wayland initialization successful. Found %d output(s).\n", n_outputs);
    if (n_outputs == 0) {
         fprintf(stderr, "Warning: No outputs found during Wayland initialization.\n");
         // Не выходим, может появиться позже
    }

    return true;
}

// --- Очистка Wayland ---
void cleanup_wayland(void) {
     printf("Cleaning up Wayland resources...\n");

     // 1. Очищаем ресурсы для каждого выхода (делегируем output модулю)
     cleanup_all_outputs(); // Эта функция должна уничтожить все wl_surface, layer_surface и т.д.

     // 2. Уничтожаем глобальные объекты Wayland (в обратном порядке получения)
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

     // 3. Отключаемся от дисплея
     if (g_display) {
         // Перед отключением стоит сделать финальный flush, чтобы отправить все destroy запросы
         int flushed = wl_display_flush(g_display);
         if (flushed < 0 && errno != EPIPE) { // EPIPE - сервер уже отвалился, нормально
             perror("Warning: wl_display_flush failed during cleanup");
         }
         wl_display_disconnect(g_display);
         g_display = NULL;
         printf("Disconnected from Wayland display.\n");
     }
      printf("Wayland cleanup finished.\n");
}


// --- Обработчики Registry Listener ---

// Вызывается для каждого глобального объекта, объявленного композитором
static void handle_global(void *data, struct wl_registry *reg, uint32_t name, const char *iface, uint32_t version) {
    UNUSED(data);
    // printf("Global Found: Interface: '%s', Version: %u, Name: %u\n", iface, version, name); // Отладка

    // Используем wl_compositor_interface.name и т.д. для сравнения
    if (strcmp(iface, wl_compositor_interface.name) == 0) {
        // Биндим интерфейс с максимально поддерживаемой версией (до 4)
        uint32_t bind_version = (version < 4) ? version : 4;
        g_compositor = wl_registry_bind(reg, name, &wl_compositor_interface, bind_version);
        printf("  -> Bound wl_compositor (version %u)\n", bind_version);
    } else if (strcmp(iface, wl_shm_interface.name) == 0) {
         // wl_shm имеет только версию 1
         g_shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
         printf("  -> Bound wl_shm (version 1)\n");
    } else if (strcmp(iface, zwlr_layer_shell_v1_interface.name) == 0) {
        // Запрашиваем версию 4, если доступно (обычно используется 2 или 3)
         uint32_t bind_version = (version < 4) ? version : 4;
         g_layer_shell = wl_registry_bind(reg, name, &zwlr_layer_shell_v1_interface, bind_version);
         printf("  -> Bound zwlr_layer_shell_v1 (version %u)\n", bind_version);
    } else if (strcmp(iface, wp_viewporter_interface.name) == 0) {
         // Viewporter имеет только версию 1
         g_viewporter = wl_registry_bind(reg, name, &wp_viewporter_interface, 1);
         printf("  -> Bound wp_viewporter (version 1)\n");
    } else if (strcmp(iface, wp_fractional_scale_manager_v1_interface.name) == 0) {
        // Fractional scale manager имеет только версию 1
         g_fractional_scale_manager = wl_registry_bind(reg, name, &wp_fractional_scale_manager_v1_interface, 1);
         printf("  -> Bound wp_fractional_scale_manager_v1 (version 1)\n");
    } else if (strcmp(iface, wl_output_interface.name) == 0) {
        // Нашли новый выход (монитор)
        // Биндим с версией 4 (для name/description событий)
        uint32_t bind_version = (version < 4) ? version : 4;
        struct wl_output *wl_out = wl_registry_bind(reg, name, &wl_output_interface, bind_version);
         printf("  -> Found wl_output (name %u, version %u, bound %u)\n", name, version, bind_version);
        if (wl_out) {
            // Делегируем добавление выхода модулю output
            if (!add_output(wl_out, name)) {
                 fprintf(stderr, "Error: Failed to add output %u\n", name);
                 wl_output_release(wl_out); // Освобождаем, если не смогли добавить
            }
        } else {
             fprintf(stderr, "Error: Failed to bind wl_output %u\n", name);
        }
    }
    // Другие интерфейсы (wl_seat, xdg_wm_base и т.д.) нам здесь не нужны
}

// Вызывается, когда глобальный объект удаляется (например, отключили монитор)
static void handle_global_remove(void *data, struct wl_registry *reg, uint32_t name) {
    UNUSED(data);
    UNUSED(reg);
    printf("Global Removed: Name: %u\n", name);

    // Ищем, не удалили ли один из наших выходов
    struct client_output *output_to_remove = find_output_by_wl_name(name);
    if (output_to_remove) {
        printf("  -> Output %u is being removed.\n", name);
        // Делегируем удаление модулю output
        remove_output(output_to_remove);
    }
    // Можно также проверять удаление других глобальных объектов,
    // но для compositor, shm, layer_shell это обычно означает конец сессии.
}
