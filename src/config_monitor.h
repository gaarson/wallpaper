#ifndef CONFIG_MONITOR_H
#define CONFIG_MONITOR_H

#include <stdbool.h> // Для bool

// Макросы, определяющие поведение конфигурации
#define ANIMATION_COMMAND "%SETUP_ANIMATION%"
#define COMMAND_FILENAME "wallpaper_command.txt"

// Тип callback-функции, которую должен предоставить вызывающий код (main.c).
// Вызывается, когда изменяется команда или режим анимации.
// - new_command: Текущая команда (строка из файла или ANIMATION_COMMAND, или NULL если пусто/ошибка/нет файла).
//               Владение строкой остается у модуля config_monitor, НЕ освобождать ее в main.c!
// - animation_enabled: true, если текущий режим - анимация.
// - user_data: Произвольный указатель, переданный при инициализации.
typedef void (*config_change_callback_t)(const char *new_command, bool animation_enabled, void *user_data);

/**
 * @brief Инициализирует монитор конфигурации.
 *
 * Находит файл конфигурации, настраивает inotify, читает начальное состояние
 * и вызывает колбэк с начальным состоянием.
 *
 * @param callback Функция, вызываемая при изменении конфигурации.
 * @param user_data Указатель, передаваемый в callback.
 * @return true в случае успеха, false при ошибке инициализации.
 */
bool config_monitor_init(config_change_callback_t callback, void *user_data);

/**
 * @brief Возвращает файловый дескриптор inotify для использования в poll/epoll.
 *
 * @return Файловый дескриптор inotify или -1, если монитор не инициализирован или произошла ошибка.
 */
int config_monitor_get_fd(void);

/**
 * @brief Обрабатывает события inotify.
 *
 * Эту функцию следует вызывать, когда poll/epoll указывает на активность
 * на файловом дескрипторе, возвращенном config_monitor_get_fd().
 * Внутри эта функция читает события inotify, при необходимости перечитывает
 * файл конфигурации и вызывает callback при изменении состояния.
 */
void config_monitor_handle_event(void);

/**
 * @brief Проверяет, включен ли в данный момент режим анимации.
 *
 * @return true, если анимация активна, иначе false.
 */
bool config_monitor_is_animation_enabled(void);

/**
 * @brief Возвращает текущую команду конфигурации.
 *
 * Это может быть ANIMATION_COMMAND, строка из файла или NULL.
 *
 * @return Указатель на текущую строку команды. НЕ освобождать! NULL, если команда не установлена.
 */
const char* config_monitor_get_current_command(void);

/**
 * @brief Освобождает ресурсы, используемые монитором конфигурации.
 *
 * Закрывает файловый дескриптор inotify и удаляет watch.
 */
void config_monitor_cleanup(void);

void on_config_changed(const char *new_command, bool new_animation_state, void *user_data);


#endif // CONFIG_MONITOR_H
