#ifndef COMMON_H
#define COMMON_H

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/poll.h>
#include <errno.h>
#include <limits.h>
#include <time.h>
#include <stdint.h>
#include <sys/timerfd.h>
#include <math.h> // Для round()


#ifdef ENABLE_DEBUG_LOG
  #define log_debug(format, ...) \
      fprintf(stdout, "[DEBUG] %s:%d: " format, \
      __FILE__, __LINE__, ##__VA_ARGS__)

#else
  #define log_debug(format, ...) do {} while(0)
#endif

#define log_error(format, ...) \
    fprintf(stderr, "[ERROR] %s:%d: " format, \
    __FILE__, __LINE__, ##__VA_ARGS__)

#include <cairo.h>
#include <wayland-client.h>
#include "wlr-layer-shell-unstable-v1-client-protocol.h"
#include "viewporter-client-protocol.h"
#include "fractional-scale-v1-client-protocol.h"

#include "renderer/core.h" // Предполагается существующим
#include "ipc.h"      // Предполагается существующим
#include "config_monitor.h" // Предполагается существующим

#define MAX_IPC_CLIENTS 5
#define MAX_POLL_FDS (3 + 1 + MAX_IPC_CLIENTS) // Wayland + Timer + Config + IPC Listener + IPC Clients
#define IPC_READ_BUFFER_SIZE (IPC_MAX_COMMAND_LEN + 1) // IPC_MAX_COMMAND_LEN должно быть определено в ipc.h

#define UNUSED(x) (void)(x)

// --- Структуры данных ---
// Определения struct buffer_pool_entry и struct client_output перенесены
// в buffer.h и output.h соответственно.

// --- Прототипы функций (для тех, что используются в нескольких модулях, но не являются частью интерфейса модуля) ---
// Обычно лучше избегать такого и делать явные интерфейсы через .h файлы модулей.

#endif // COMMON_H
