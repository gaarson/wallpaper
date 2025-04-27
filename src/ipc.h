#ifndef IPC_H
#define IPC_H

#include <stddef.h> // For size_t
#include <stdbool.h> // For bool

// Максимальная длина команды (для информации пользователя библиотеки)
// Убедитесь, что это значение совпадает с #define в ipc.c
#define IPC_MAX_COMMAND_LEN 1024

typedef enum {
    IPC_READ_RESULT_SUCCESS,
    IPC_READ_RESULT_WOULD_BLOCK,
    IPC_READ_RESULT_EOF,
    IPC_READ_RESULT_ERROR,
    IPC_READ_RESULT_TOO_LONG,
    IPC_READ_RESULT_NO_HANDLER,
    IPC_READ_RESULT_INVALID_FD,
    IPC_READ_RESULT_BUFFER_TOO_SMALL
} ipc_read_result_t;

// Обработчик команд: int client_fd, const char *command, size_t command_len
typedef void (*ipc_command_handler_t)(int, const char *, size_t);

// Настройка IPC сервера
// socket_name: имя файла сокета (будет создан в $XDG_RUNTIME_DIR)
// handler: функция обратного вызова для обработки команд
bool ipc_setup(const char *socket_name, ipc_command_handler_t handler);

// Очистка ресурсов IPC сервера
void ipc_cleanup(void);

// Получение дескриптора слушающего сокета (для event loop)
int ipc_get_listener_fd(void);

// Прием нового клиента (вызывать, когда listener_fd готов к чтению)
// Возвращает FD клиента или -1
int ipc_accept_client(void);

// Чтение и обработка команды от клиента (вызывать, когда client_fd готов к чтению)
// buffer: буфер для чтения данных
// buffer_size: размер буфера (должен быть >= IPC_MAX_COMMAND_LEN + 1)
// Возвращает статус операции ipc_read_result_t
ipc_read_result_t ipc_read_command(int client_fd, char *buffer, size_t buffer_size);

// Принудительное закрытие соединения с клиентом
void ipc_close_client(int client_fd);

// Получить количество активных клиентов
int ipc_get_client_count(void);

// Получить массив дескрипторов активных клиентов
int ipc_get_active_clients(int *dest_fds, int max_dest_size);

#endif // IPC_H
