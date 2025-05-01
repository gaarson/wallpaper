#include "buffer.h"
#include <limits.h> // Для LLONG_MAX, OFF_T_MAX
#include <sys/syscall.h> // Для SYS_memfd_create (если будем использовать)

// --- Создание SHM файла ---
// Использует mkostemp для безопасности и автоматического unlink.
// Предпочитает XDG_RUNTIME_DIR, затем /dev/shm.
int create_shm_file(size_t size) {
    char template[] = "/wp-shm-XXXXXX";
    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    char shm_path[PATH_MAX];
    int fd = -1;
    int written = -1;

    if (size == 0) {
        fprintf(stderr, "Error: Attempted to create SHM file of zero size.\n");
        errno = EINVAL;
        return -1;
    }

    // Попытка в XDG_RUNTIME_DIR
    if (runtime_dir) {
        written = snprintf(shm_path, sizeof(shm_path), "%s%s", runtime_dir, template);
        if (written > 0 && (size_t)written < sizeof(shm_path)) {
            // O_CLOEXEC важен для безопасности
            fd = mkostemp(shm_path, O_CLOEXEC);
            if (fd >= 0) {
                // Файл уже создан, но имя нам больше не нужно,
                // композитор будет работать с fd. unlink удаляет имя,
                // но файл существует, пока fd открыт.
                unlink(shm_path);
                 printf("Created SHM file (fd: %d) via XDG_RUNTIME_DIR\n", fd);
            } else {
                perror("mkostemp in XDG_RUNTIME_DIR failed");
                // Продолжаем попытку в /dev/shm
            }
        } else {
             fprintf(stderr, "Warning: Could not construct path in XDG_RUNTIME_DIR.\n");
        }
    }

    // Попытка в /dev/shm, если не удалось в XDG_RUNTIME_DIR
    if (fd < 0) {
        written = snprintf(shm_path, sizeof(shm_path), "/dev/shm%s", template);
         if (written > 0 && (size_t)written < sizeof(shm_path)) {
            fd = mkostemp(shm_path, O_CLOEXEC);
             if (fd >= 0) {
                unlink(shm_path);
                 printf("Created SHM file (fd: %d) via /dev/shm\n", fd);
             } else {
                 perror("mkostemp in /dev/shm failed");
             }
        } else {
             fprintf(stderr, "Warning: Could not construct path in /dev/shm.\n");
        }
    }

    // Если все попытки провалились
    if (fd < 0) {
        fprintf(stderr, "Error: Failed to create temporary SHM file.\n");
        errno = EIO; // Или другой подходящий код ошибки
        return -1;
    }

    // Проверка переполнения размера файла
    #ifdef OFF_T_MAX
        // POSIX.1-2001
        if (size > (size_t)OFF_T_MAX) {
            fprintf(stderr,"Error: Requested SHM size (%zu) exceeds OFF_T_MAX (%lld)\n", size, (long long)OFF_T_MAX);
            close(fd);
            errno = EFBIG;
            return -1;
        }
    #else
        // Fallback, если OFF_T_MAX не определен (менее вероятно на современных системах)
        if ((unsigned long long)size > (unsigned long long)LLONG_MAX) {
             fprintf(stderr,"Error: Requested SHM size (%zu) exceeds LLONG_MAX (%lld)\n", size, LLONG_MAX);
            close(fd);
            errno = EFBIG;
            return -1;
        }
    #endif


    // Установка размера файла
    // Используем ftruncate64 если доступно (_GNU_SOURCE должен быть определен)
    #if defined(_GNU_SOURCE) && defined(__USE_LARGEFILE64)
        if (ftruncate64(fd, (off64_t)size) == -1) {
            perror("ftruncate64 failed");
            close(fd);
            return -1;
        }
    #else
        if (ftruncate(fd, (off_t)size) == -1) {
             perror("ftruncate failed");
            close(fd);
            return -1;
        }
    #endif


    return fd;
}


// --- Очистка одного буфера ---
void cleanup_buffer_pool_entry(struct buffer_pool_entry *entry) {
    if (!entry) return;

    if (entry->wl_buffer) {
        wl_buffer_destroy(entry->wl_buffer);
        entry->wl_buffer = NULL;
    }
    if (entry->map && entry->map != MAP_FAILED && entry->size > 0) {
        if (munmap(entry->map, entry->size) == -1) {
            perror("munmap failed during buffer cleanup");
        }
        entry->map = NULL; // Важно обнулить после munmap
        entry->size = 0;
    }
     // FD закрывается здесь, так как он принадлежит этому буферу
    if (entry->fd >= 0) {
        if (close(entry->fd) == -1) {
             perror("close failed during buffer cleanup");
        }
        entry->fd = -1;
    }
    entry->busy = false;
    entry->width = 0;
    entry->height = 0;
    entry->stride = 0;
}

// --- Создание одного буфера ---
bool create_buffer_pool_entry(struct buffer_pool_entry *entry, struct wl_shm *shm, int width, int height, cairo_format_t format) {
    if (!entry || !shm || width <= 0 || height <= 0) {
        fprintf(stderr, "Error: Invalid arguments for create_buffer_pool_entry\n");
        return false;
    }

    // Рассчитываем stride и size
    int stride = cairo_format_stride_for_width(format, width);
    if (stride <= 0) {
        fprintf(stderr, "Error: Invalid stride (%d) calculated for width %d and format %d\n", stride, width, format);
        return false;
    }
    size_t stride_sz = (size_t)stride;
    size_t size = 0;
    // Проверка на переполнение при умножении
    if ((size_t)height > SIZE_MAX / stride_sz) {
        fprintf(stderr, "Error: Buffer size calculation overflow (%d x %zu)\n", height, stride_sz);
        return false;
    }
    size = stride_sz * (size_t)height;
    if (size == 0) {
        fprintf(stderr, "Error: Calculated buffer size is zero.\n");
        return false;
    }

    // Создаем SHM файл
    entry->fd = create_shm_file(size);
    if (entry->fd < 0) {
        fprintf(stderr, "Error: Failed to create SHM file for buffer.\n");
        // cleanup_buffer_pool_entry(entry); // fd уже -1
        return false;
    }

    // mmap
    // MAP_SHARED важен, чтобы композитор видел изменения
    entry->map = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, entry->fd, 0);
    if (entry->map == MAP_FAILED) {
        perror("mmap failed for buffer");
        cleanup_buffer_pool_entry(entry); // Закроет fd
        return false;
    }

    // Создаем Wayland SHM Pool
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, entry->fd, (int64_t)size);
    if (!pool) {
        fprintf(stderr, "Error: wl_shm_create_pool failed.\n");
        cleanup_buffer_pool_entry(entry); // munmap + close fd
        return false;
    }

    // Создаем Wayland Buffer из пула
    // Используем ARGB8888, так как Cairo работает с ним по умолчанию и он хорошо поддерживается
    // Убедитесь, что cairo_format_t соответствует WL_SHM_FORMAT
    if (format != CAIRO_FORMAT_ARGB32) {
         fprintf(stderr, "Warning: Using CAIRO_FORMAT %d, but WL_SHM_FORMAT_ARGB8888 is assumed.\n", format);
    }
    entry->wl_buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_ARGB8888);

    // Пул больше не нужен после создания буфера
    wl_shm_pool_destroy(pool);

    if (!entry->wl_buffer) {
        fprintf(stderr, "Error: wl_shm_pool_create_buffer failed.\n");
        cleanup_buffer_pool_entry(entry); // munmap + close fd
        return false;
    }

    // Сохраняем параметры
    entry->size = size;
    entry->busy = false;
    entry->width = width;
    entry->height = height;
    entry->stride = stride;

    // Добавляем слушателя для события release
    // Передаем указатель на сам 'entry' в качестве data
    wl_buffer_add_listener(entry->wl_buffer, &buffer_listener, entry);

    // FD НЕ закрывается здесь, он нужен пока жив wl_buffer.
    // Он будет закрыт в cleanup_buffer_pool_entry.

    return true;
}


// --- Обработчик освобождения буфера ---
// Вызывается Wayland композитором, когда буфер больше не используется.
void buffer_handle_release(void *data, struct wl_buffer *wl_buffer) {
    struct buffer_pool_entry *entry = data; // data - это указатель на buffer_pool_entry
    UNUSED(wl_buffer); // wl_buffer совпадает с entry->wl_buffer

    if (!entry) {
        fprintf(stderr, "Error: buffer_handle_release called with NULL data!\n");
        return;
    }

    // Просто помечаем буфер как свободный. Ресурсы НЕ освобождаются здесь.
    entry->busy = false;
    // printf("Buffer released (fd: %d)\n", entry->fd); // Для отладки
}

// Определение листенера для buffer_handle_release
const struct wl_buffer_listener buffer_listener = {
    .release = buffer_handle_release
};
