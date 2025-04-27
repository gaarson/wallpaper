// ipc.c
#define _GNU_SOURCE // Для accept4 и strchrnul (хотя strchrnul здесь не используется)
#include "ipc.h" // Предполагается, что ipc.h тоже будет обновлен (см. комментарии ниже)

#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <errno.h>
#include <limits.h>
#include <poll.h>     // Был упомянут в комментарии оригинала, но не используется
#include <stdbool.h> // Для bool

// --- Новые определения ---

// Максимальная допустимая длина одной команды (без NULL-терминатора)
// Выберите значение, подходящее для вашего протокола.
#define MAX_COMMAND_LEN 1024

// Результаты операции чтения команды
// typedef enum {
//     IPC_READ_RESULT_SUCCESS,      // Успешно прочитана и передана в обработчик команда
//     IPC_READ_RESULT_WOULD_BLOCK,  // Нет данных для чтения сейчас (EAGAIN/EWOULDBLOCK)
//     IPC_READ_RESULT_EOF,          // Клиент корректно закрыл соединение (read вернул 0)
//     IPC_READ_RESULT_ERROR,        // Произошла ошибка чтения или другая проблема (сокет закрыт)
//     IPC_READ_RESULT_TOO_LONG,     // Получена команда, превышающая MAX_COMMAND_LEN (игнорирована)
//     IPC_READ_RESULT_NO_HANDLER,   // Команда получена, но обработчик не установлен
//     IPC_READ_RESULT_INVALID_FD,   // Передан неверный или неизвестный client_fd
//     IPC_READ_RESULT_BUFFER_TOO_SMALL // Предоставленный буфер слишком мал
// } ipc_read_result_t;

// Новый тип обработчика команд
// Принимает дескриптор клиента, указатель на команду (const) и ее длину.
// !!! Важно: Код, использующий эту библиотеку, должен предоставлять обработчик такого типа !!!
typedef void (*ipc_command_handler_t)(int client_fd, const char *command, size_t command_len);


// --- Внутренние константы и переменные ---
#define MAX_CLIENTS 5 // Максимальное количество одновременных клиентов
#define SOCKET_PATH_TEMPLATE "%s/%s" // Шаблон пути к сокету

static int listener_fd = -1;
static int client_fds[MAX_CLIENTS];
static int n_clients = 0;
static char socket_path[PATH_MAX] = {0};
// Используем новый тип для глобального обработчика
static ipc_command_handler_t global_command_handler = NULL;


// --- Вспомогательная функция для поиска слота клиента ---
// (без изменений)
static int find_client_slot(int client_fd) {
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (client_fds[i] == client_fd) {
            return i;
        }
    }
    return -1; // Не найден
}

// --- Реализации публичных функций ---

// Обновлена сигнатура: принимает ipc_command_handler_t
bool ipc_setup(const char *socket_name, ipc_command_handler_t handler) {
    if (listener_fd != -1) {
        fprintf(stderr, "IPC already set up.\n");
        return true; // Уже настроен
    }
    if (!socket_name || socket_name[0] == '\0') {
        fprintf(stderr, "Invalid socket name provided for IPC setup.\n");
        return false;
    }
    // Проверка обработчика
    if (!handler) {
         fprintf(stderr, "IPC setup requires a valid command handler.\n");
         return false;
    }

    const char *runtime_dir = getenv("XDG_RUNTIME_DIR");
    if (!runtime_dir) {
        // Можно попробовать /tmp как запасной вариант, но это менее безопасно
        // и может потребовать других прав доступа.
        // runtime_dir = "/tmp"; // Пример запасного варианта (не рекомендуется без доп. проверок)
        fprintf(stderr, "XDG_RUNTIME_DIR not set. Cannot create IPC socket securely.\n");
        return false;
    }

    int written = snprintf(socket_path, sizeof(socket_path), SOCKET_PATH_TEMPLATE, runtime_dir, socket_name);
    // Проверяем >= т.к. snprintf возвращает кол-во символов, которое *было бы* записано
    if (written < 0 || (size_t)written >= sizeof(socket_path)) {
        fprintf(stderr, "Failed to format socket path or path too long.\n");
        socket_path[0] = '\0';
        return false;
    }

    // Создаем сокет с флагами non-block и close-on-exec
    listener_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (listener_fd == -1) {
        perror("IPC: Failed to create listener socket");
        socket_path[0] = '\0';
        return false;
    }

    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    // Используем strnlen для большей безопасности, если доступно (C11), или strlen
    // size_t path_len = strnlen(socket_path, sizeof(addr.sun_path)); // C11 вариант
    size_t path_len = strlen(socket_path); // C99 вариант

    // Проверка, что путь помещается в sun_path (с учетом NULL-терминатора)
    if (path_len >= sizeof(addr.sun_path)) {
         fprintf(stderr, "IPC: Socket path '%s' is too long for sockaddr_un (max %zu)\n",
                 socket_path, sizeof(addr.sun_path) -1);
         close(listener_fd); listener_fd = -1;
         socket_path[0] = '\0';
         return false;
    }
    // Копируем путь. strncpy безопасен здесь, т.к. мы проверили длину
    // и addr была обнулена через memset.
    strncpy(addr.sun_path, socket_path, path_len);
    // Явное добавление NULL на всякий случай не повредит, хотя и не строго обязательно после memset/strncpy с проверкой длины
    addr.sun_path[path_len] = '\0';

    // sun_path должен быть null-терминирован для переносимости
    // Вычисляем длину структуры sockaddr_un для bind
    socklen_t addr_len = offsetof(struct sockaddr_un, sun_path) + path_len + 1; // +1 для \0

    // Попытка удалить старый сокет перед bind
    // Не считаем ошибкой, если файла не существует (ENOENT)
    if (unlink(socket_path) == -1 && errno != ENOENT) {
         perror("IPC Warning: Failed to unlink old socket file, bind might fail");
         // Не выходим, bind может все равно сработать или выдать более точную ошибку
    }

    // Привязываем сокет к адресу
    if (bind(listener_fd, (struct sockaddr*)&addr, addr_len) == -1) {
        perror("IPC: Failed to bind listener socket");
        close(listener_fd); listener_fd = -1;
        unlink(socket_path); // Попытка очистки
        socket_path[0] = '\0';
        return false;
    }

    // Начинаем слушать входящие соединения
    if (listen(listener_fd, MAX_CLIENTS) == -1) { // backlog = MAX_CLIENTS
        perror("IPC: Failed to listen on socket");
        close(listener_fd); listener_fd = -1;
        unlink(socket_path); // Попытка очистки
        socket_path[0] = '\0';
        return false;
    }

    // Сохраняем обработчик (уже нового типа)
    global_command_handler = handler;

    printf("IPC: Listener socket listening on %s (fd: %d)\n", socket_path, listener_fd);

    // Инициализируем массив дескрипторов клиентов
    for (int i = 0; i < MAX_CLIENTS; ++i) client_fds[i] = -1;
    n_clients = 0;
    return true;
}

void ipc_cleanup(void) {
    if (listener_fd >= 0) {
        close(listener_fd);
        listener_fd = -1;
        printf("IPC: Closed Listener FD.\n");
    }
    // Удаляем файл сокета, только если путь был успешно сформирован
    if (socket_path[0] != '\0') {
        if (unlink(socket_path) == 0) {
             printf("IPC: Unlinked socket file: %s\n", socket_path);
        } else if (errno != ENOENT) {
            // Если файл уже удален (ENOENT), это не ошибка при очистке
            perror("IPC Warning: Failed to unlink socket file during cleanup");
        }
        socket_path[0] = '\0'; // Сбрасываем путь в любом случае
    }
    printf("IPC: Closing client connections (%d)...\n", n_clients);
    int closed_count = 0;
    for (int i = 0; i < MAX_CLIENTS; ++i) {
        if (client_fds[i] >= 0) {
            close(client_fds[i]);
            client_fds[i] = -1;
            closed_count++;
        }
    }
    n_clients = 0; // Сбрасываем счетчик
    printf("IPC: Closed %d client FDs. Cleanup finished.\n", closed_count);
}

int ipc_get_listener_fd(void) {
    return listener_fd;
}

// Принимает нового клиента
// Возвращает FD клиента или -1 при ошибке/отсутствии новых клиентов
int ipc_accept_client(void) {
    if (listener_fd < 0) return -1; // Сервер не настроен

    // Используем accept4 для установки флагов non-block и close-on-exec атомарно
    int client_fd = accept4(listener_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
    if (client_fd < 0) {
        // EAGAIN/EWOULDBLOCK означают, что нет входящих соединений для приема прямо сейчас
        if (errno != EAGAIN && errno != EWOULDBLOCK) {
            perror("IPC: accept4 failed");
        }
        return -1; // Ошибка или нет входящих соединений
    }

    // Найти свободный слот
    int client_slot = -1;
    for(int i = 0; i < MAX_CLIENTS; ++i) {
        if (client_fds[i] == -1) {
            client_slot = i;
            break;
        }
    }

    if (client_slot != -1) {
        printf("IPC: Accepted client fd %d -> slot %d\n", client_fd, client_slot);
        client_fds[client_slot] = client_fd;
        n_clients++;
        return client_fd; // Возвращаем FD нового клиента
    } else {
        fprintf(stderr, "IPC: Max clients (%d) reached, rejecting fd %d\n", MAX_CLIENTS, client_fd);
        // Важно закрыть соединение, если мы не можем его обслужить
        close(client_fd);
        return -1; // Сигнализируем об ошибке (нет места)
    }
}

// Читает и обрабатывает команду от клиента
// Возвращает статус операции ipc_read_result_t
// buffer_size должен быть >= MAX_COMMAND_LEN + 1
ipc_read_result_t ipc_read_command(int client_fd, char *buffer, size_t buffer_size) {
    // Проверка входных параметров
    fprintf(stderr, "IPC: COMMASFN");
    if (!buffer || buffer_size <= MAX_COMMAND_LEN) {
         fprintf(stderr, "IPC: Buffer is NULL or too small for MAX_COMMAND_LEN (%d required, %zu provided).\n",
                 MAX_COMMAND_LEN + 1, buffer_size);
         // Не закрываем сокет здесь, т.к. это ошибка программирования, а не клиента
         return IPC_READ_RESULT_BUFFER_TOO_SMALL;
    }

    int slot = find_client_slot(client_fd);
    if (slot == -1) {
        fprintf(stderr, "IPC: Attempt to read from unknown or closed client fd %d\n", client_fd);
        return IPC_READ_RESULT_INVALID_FD;
    }

    // Читаем данные, оставляя место для NULL-терминатора
    // Читаем максимум buffer_size - 1, чтобы гарантированно поместить \0
    ssize_t n_read = read(client_fd, buffer, buffer_size - 1);

    if (n_read > 0) {
        buffer[n_read] = '\0'; // Всегда NULL-терминируем прочитанное

        // Ищем конец строки (простейший вариант фрейминга по '\n')
        char *newline_ptr = strchr(buffer, '\n');
        char *cmd_end_ptr = buffer + n_read; // Указатель на конец прочитанных данных

        // Указатель на начало текущей обрабатываемой команды в буфере
        char *current_cmd = buffer;

        while (current_cmd < cmd_end_ptr) {
            char *cmd_terminator; // Указатель на терминатор ('\n' или конец буфера)
            bool found_newline = false;

            // Ищем '\n' в оставшейся части буфера
            newline_ptr = strchr(current_cmd, '\n');

            if (newline_ptr != NULL) {
                cmd_terminator = newline_ptr;
                found_newline = true;
                *cmd_terminator = '\0'; // Заменяем '\n' на NULL-терминатор для обработки команды
            } else {
                // '\n' не найден в прочитанном блоке. Команда заканчивается концом буфера.
                cmd_terminator = cmd_end_ptr;
            }

            size_t command_len = cmd_terminator - current_cmd;

            // Проверяем длину команды *перед* вызовом обработчика
            if (command_len > MAX_COMMAND_LEN) {
                 fprintf(stderr, "IPC: Client fd %d (slot %d) sent command exceeding MAX_COMMAND_LEN (%zu > %d). Discarding fragment.\n",
                         client_fd, slot, command_len, MAX_COMMAND_LEN);
                 // Важно: Если \n не было, мы можем получить большой фрагмент,
                 // который не является полной командой. Простая очистка буфера
                 // или закрытие соединения может быть оправдано при строгой политике.
                 // Здесь мы просто игнорируем этот фрагмент и переходим к следующему (если был \n).
                 // Если \n не было, цикл завершится.
                 // Можно вернуть специальную ошибку, если вся прочитанная порция > MAX_COMMAND_LEN и без \n
                 if (!found_newline) {
                    // Весь блок без \n слишком длинный - вероятно проблема
                    // Можно закрыть соединение:
                    // printf("IPC: Closing client fd %d due to oversized command fragment.\n", client_fd);
                    // close(client_fd); client_fds[slot] = -1; n_clients--; return IPC_READ_RESULT_ERROR;
                    return IPC_READ_RESULT_TOO_LONG; // Сообщаем о слишком длинной команде/фрагменте
                 }
                 // Если был \n, но команда до него слишком длинная, игнорируем ее и идем дальше.

            } else if (command_len > 0) { // Обрабатываем только непустые команды
                 // === ВЫЗЫВАЕМ CALLBACK (если команда корректна по длине) ===
                 if (global_command_handler) {
                     // Передаем дескриптор, указатель на начало команды (const) и ее реальную длину
                     global_command_handler(client_fd, current_cmd, command_len);
                 } else {
                     fprintf(stderr, "IPC: Warning - received command but no handler set.\n");
                     // Не прерываем цикл, т.к. могли быть другие команды
                     // return IPC_READ_RESULT_NO_HANDLER; // Можно возвращать ошибку здесь
                 }
                 // =========================
            }
            // else: command_len == 0 (например, пустая строка при "\n\n") - игнорируем

            // Переходим к следующей команде (если был найден '\n')
            if (found_newline) {
                current_cmd = cmd_terminator + 1; // Переходим за '\0' (бывший '\n')
            } else {
                // '\n' не найден, значит, мы обработали все до конца буфера
                // Если данные остались (т.к. read < buffer_size-1), их нужно будет обработать при следующем чтении.
                // Текущая простая реализация их "теряет", если они не составляют полную команду < MAX_COMMAND_LEN.
                // Для надежной обработки нужен буфер накопления для каждого клиента.
                break; // Выходим из while
            }
        } // end while (current_cmd < cmd_end_ptr)

        // Если мы успешно обработали хотя бы одну команду (или просто прочитали данные),
        // возвращаем успех. Более сложная логика могла бы возвращать кол-во обработанных команд.
        return IPC_READ_RESULT_SUCCESS;

    } else if (n_read == 0) {
        // Клиент закрыл соединение (EOF)
        printf("IPC: Client fd %d (slot %d) disconnected (EOF).\n", client_fd, slot);
        close(client_fd);
        client_fds[slot] = -1;
        n_clients--;
        return IPC_READ_RESULT_EOF;
    } else { // n_read == -1
        // Ошибка чтения
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            // Нет данных для чтения прямо сейчас (не ошибка)
            return IPC_READ_RESULT_WOULD_BLOCK;
        } else {
             // Реальная ошибка чтения (ECONNRESET, EPIPE, etc.)
            perror("IPC: read from client failed");
            fprintf(stderr, "IPC: Closing client fd %d (slot %d) due to read error.\n", client_fd, slot);
            close(client_fd);
            client_fds[slot] = -1;
            n_clients--;
            return IPC_READ_RESULT_ERROR;
        }
    }
}


// Принудительно закрывает соединение с клиентом
// (без изменений)
void ipc_close_client(int client_fd) {
     int slot = find_client_slot(client_fd);
     if (slot != -1) {
         printf("IPC: Force closing client fd %d (slot %d)\n", client_fd, slot);
         close(client_fd);
         client_fds[slot] = -1; // Освобождаем слот
         n_clients--;
     } else {
         fprintf(stderr, "IPC: Attempt to close unknown or already closed client fd %d\n", client_fd);
     }
}

// --- Дополнительные функции (если нужны) ---

// Получить количество активных клиентов
int ipc_get_client_count(void) {
    return n_clients;
}

// Получить массив дескрипторов активных клиентов
// Возвращает количество скопированных дескрипторов
// `dest_fds` должен быть размером не менее MAX_CLIENTS
int ipc_get_active_clients(int *dest_fds, int max_dest_size) {
    if (!dest_fds || max_dest_size <= 0) return 0;
    int count = 0;
    for (int i = 0; i < MAX_CLIENTS && count < max_dest_size; ++i) {
        if (client_fds[i] != -1) {
            dest_fds[count++] = client_fds[i];
        }
    }
    return count;
}
