#!/bin/bash
#
# wallpaper_journey.sh - Скрипт-автопилот для "увлекательного путешествия"
#

# --- Настройка ---
SOCK_PATH="/run/user/$(id -u)/wallpaper_control.sock"
UPDATE_INTERVAL=1 # Обновлять значения каждую 1 секунду

# --- Внутренняя функция для отправки команд ---
# Мы отправляем stdout в /dev/null, чтобы socat не жаловался,
# если сокет временно недоступен (например, при перезапуске обоев).
send_cmd() {
    echo "$1" | socat - UNIX-CONNECT:"$SOCK_PATH" > /dev/null 2>&1
}

echo "Запуск автопилота... Нажмите Ctrl+C, чтобы остановить (если не в фоне)."
echo "Путь к сокету: $SOCK_PATH"

# --- Исходное состояние ---
# Мы будем хранить состояние здесь, в переменных bash
vel_x=0.0
vel_y=0.0
vel_z=-4.0
time_counter=0 # Наш "глобальный таймер" для sin()

# --- Сброс параметров в известное "хорошее" состояние ---
send_cmd "set_nebula_brightness 0.15"
send_cmd "set_tail_probability 0.03"
send_cmd "set_lens_fade_times 15.0 30.0"
send_cmd "set_lens_params 40 2.5 40"

# --- Главный цикл ---
while true; do
    # Увеличиваем наш таймер
    time_counter=$((time_counter + 1))

    # === 1. Обновление скорости (Плавный дрейф) ===
    # Мы используем awk для float-математики и "случайного блуждания" (random walk)
    # Мы передаем awk наши текущие переменные и счетчик времени (для srand)
    # и читаем новые значения обратно в них.
    read vel_x vel_y vel_z < <(awk -v vx="$vel_x" -v vy="$vel_y" -v vz="$vel_z" -v t="$time_counter" 'BEGIN { 
        srand(t); # Используем счетчик времени как seed
        
        # Дрейф по X: блуждание от -1.5 до 1.5
        nvx = vx + (rand() - 0.5) * 0.1; 
        if (nvx > 1.5) nvx = 1.5; if (nvx < -1.5) nvx = -1.5; 
        
        # Дрейф по Y: блуждание от -1.5 до 1.5
        nvy = vy + (rand() - 0.5) * 0.1; 
        if (nvy > 1.5) nvy = 1.5; if (nvy < -1.5) nvy = -1.5; 
        
        # Скорость по Z: блуждание от 3.0 до 5.0
        nvz = vz + (rand() - 0.5) * 0.1; 
        if (nvz > 5.0) nvz = 5.0; if (nvz < 3.0) nvz = 3.0; 
        
        printf "%.2f %.2f %.2f", nvx, nvy, nvz
    }')
    
    # Отправляем новую скорость
    send_cmd "set_star_velocity $vel_x $vel_y $vel_z"


    # === 2. Обновление Линз (Пульсация) ===
    # Мы используем sin() в awk для плавной пульсации
    read lens_strength lens_rate < <(awk -v t="$time_counter" 'BEGIN {
        # Сила пульсирует от 1.0 до 4.0 (2.5 +/ 1.5)
        strength = 2.5 + sin(t * 0.1) * 1.5; 
        
        # Частота появления пульсирует от 0.0 до 0.2 (0.1 +/- 0.1)
        # (0.1 линзы в секунду = 1 линза каждые 10 сек)
        rate = 0.1 + sin(t * 0.07) * 0.1;
        if (rate < 0) rate = 0;
        
        printf "%.2f %.2f", strength, rate
    }')
    
    send_cmd "set_lens_params 40 $lens_strength 40" # R=40, TTL=40
    send_cmd "set_lens_spawn_rate $lens_rate"

    
    # === 3. Обновление Цветов (Переливание) ===
    # Снова используем sin(), чтобы "вращать" цвета по RGB
    read r1 g1 b1 r2 g2 b2 < <(awk -v t="$time_counter" 'BEGIN {
        # Цвет 1: Медленно вращается (R -> G -> B)
        r1 = 0.5 + 0.5 * sin(t * 0.02);
        g1 = 0.5 + 0.5 * sin(t * 0.02 + 2.09); # +120 град
        b1 = 0.5 + 0.5 * sin(t * 0.02 + 4.18); # +240 град
        
        # Цвет 2: Вращается чуть быстрее и в другом направлении
        r2 = 0.5 + 0.5 * sin(t * 0.03 + 3.14); # смещение
        g2 = 0.5 + 0.5 * sin(t * 0.03 + 1.04);
        b2 = 0.5 + 0.5 * sin(t * 0.03);
        
        printf "%.2f %.2f %.2f %.2f %.2f %.2f", r1, g1, b1, r2, g2, b2
    }')
    
    send_cmd "set_nebula_color1 $r1 $g1 $b1"
    send_cmd "set_nebula_color2 $r2 $g2 $b2"

    # Ждем до следующего обновления
    sleep $UPDATE_INTERVAL
done
