#include "timer.h"

void arm_timer(int timer_fd, long interval_ns) {
    if (timer_fd < 0 || interval_ns <= 0) return;

    struct itimerspec ts = {0};
    const long NANO_PER_SEC = 1000000000L;

    ts.it_interval.tv_sec = interval_ns / NANO_PER_SEC;
    ts.it_interval.tv_nsec = interval_ns % NANO_PER_SEC;
    
    // Минимальный интервал
    if (ts.it_interval.tv_sec == 0 && ts.it_interval.tv_nsec == 0) {
        ts.it_interval.tv_nsec = 1;
    }
    
    // Первый запуск - немедленно (или почти)
    ts.it_value.tv_sec = 0;
    ts.it_value.tv_nsec = 1; // Запускаем "сейчас"

    if (timerfd_settime(timer_fd, 0, &ts, NULL) == -1) {
        perror("arm_timer: timerfd_settime failed");
    }
}

// Разоружает (останавливает) таймер
void disarm_timer(int timer_fd) {
    if (timer_fd < 0) return;

    struct itimerspec ts = {0}; // Нулевая структура
    
    if (timerfd_settime(timer_fd, 0, &ts, NULL) == -1) {
        perror("disarm_timer: timerfd_settime failed");
    }
}

int setup_timer(long interval_ns) {
    int timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (timer_fd == -1) {
        perror("timerfd_create failed");
        return -1;
    }

    if (interval_ns <= 0) {
        fprintf(stderr, "Error: Invalid timer interval (%ld ns). Timer disabled.\n", interval_ns);
        close(timer_fd);
        errno = EINVAL;
        return -1; // Возвращаем -1, чтобы показать, что таймер не создан
    }

    struct itimerspec ts = {0};
    const long NANO_PER_SEC = 1000000000L;

    // Расчет интервала
    ts.it_interval.tv_sec = interval_ns / NANO_PER_SEC;
    ts.it_interval.tv_nsec = interval_ns % NANO_PER_SEC;

    // Первый запуск таймера - используем тот же интервал
    ts.it_value = ts.it_interval;

    // Минимальный интервал, чтобы избежать нулевого значения, которое отключает таймер
    if (ts.it_interval.tv_sec == 0 && ts.it_interval.tv_nsec == 0) {
         fprintf(stderr, "Warning: Timer interval too small (%ld ns), setting to 1 ns.\n", interval_ns);
        ts.it_interval.tv_nsec = 1;
        ts.it_value.tv_nsec = 1;
    }

    if (timerfd_settime(timer_fd, 0, &ts, NULL) == -1) {
        perror("timerfd_settime failed");
        close(timer_fd);
        return -1;
    }

    printf("Timer configured: interval %ld ns (fd: %d)\n", interval_ns, timer_fd);
    return timer_fd;
}

void cleanup_timer(int timer_fd) {
     if (timer_fd >= 0) {
         printf("Closing timer fd: %d\n", timer_fd);
         if (close(timer_fd) == -1) {
             perror("Warning: close timer_fd failed");
         }
     }
}
