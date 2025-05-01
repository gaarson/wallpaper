#ifndef BUFFER_H
#define BUFFER_H

#include "common.h"

// --- Структура для буфера в пуле ---
struct buffer_pool_entry {
    struct wl_buffer *wl_buffer; // Wayland буфер
    void *map;                   // Указатель на mmap'нутую память
    size_t size;                 // Размер буфера в байтах
    int fd;                      // Файловый дескриптор SHM
    bool busy;                   // true, если буфер используется композитором
    int width;                   // Ширина буфера
    int height;                  // Высота буфера
    int stride;                  // Stride буфера
};

// --- Прототипы ---
int create_shm_file(size_t size);
void cleanup_buffer_pool_entry(struct buffer_pool_entry *entry); // Очистка одного буфера
bool create_buffer_pool_entry(struct buffer_pool_entry *entry, struct wl_shm *shm, int width, int height, cairo_format_t format);
void buffer_handle_release(void *data, struct wl_buffer *wl_buffer);

extern const struct wl_buffer_listener buffer_listener; // Экспортируем листенер

#endif // BUFFER_H
