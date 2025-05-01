#ifndef TIMER_H
#define TIMER_H

#include "common.h"

// --- Прототипы ---
int setup_timer(long interval_ns); // Возвращает timer FD или -1 при ошибке
void cleanup_timer(int timer_fd);

#endif // TIMER_Hjk
