#version 300 es
precision highp float; // Используем highp для вычислений цвета и координат

// --- Uniform-переменные (Настраиваются из C/C++ кода) ---
uniform vec2 uResolution;        // Разрешение экрана (может не использоваться напрямую здесь, но полезно иметь)
uniform float uTime;             // Текущее время для анимации

// Параметры сетки и вида
uniform float uGridDensity;      // Плотность линий (больше = плотнее)
uniform float uLineWidth;        // Базовая ширина линии (влияет на fwidth сглаживание)
uniform float uScrollSpeed;      // Скорость прокрутки сетки
uniform float uPerspectiveFactor; // Сила эффекта перспективы
uniform float uHorizonY;         // Положение горизонта (0=низ, 1=верх)

// Цвета
uniform vec3 uColorGrid;         // Цвет линий сетки
uniform vec3 uColorGround;       // Цвет "земли" под сеткой
uniform vec3 uColorSkyNear;      // Цвет неба у горизонта
uniform vec3 uColorSkyFar;       // Цвет неба в зените

// Параметры тумана
uniform float uFogDensity;       // Плотность тумана (больше = гуще/ближе)
uniform bool uFogEnable;         // Включен ли туман
uniform bool uGridFogEnable;     // Влияет ли туман на сетку (или только на фон)

// --- Входные данные из вершинного шейдера ---
// 'in' переменная должна совпадать по имени и типу с 'out' переменной вершинного шейдера
in vec2 vTexCoord_out;           // Интерполированные текстурные координаты (0..1)

// --- Выходная переменная ---
// Обязательная выходная переменная для фрагментного шейдера в GLSL 300 es
out vec4 FragColor;              // Итоговый цвет фрагмента (пикселя)

// --- Вспомогательные функции ---

// Вычисление цвета фона (неба/земли)
vec3 getBackgroundColor(float screen_y) {
    // Плавный переход от земли к небу у горизонта
    float sky_blend_factor = smoothstep(uHorizonY - 0.05, uHorizonY + 0.05, screen_y);
    // Интерполяция цвета неба от ближнего к дальнему
    float sky_color_factor = smoothstep(uHorizonY, 1.0, screen_y);
    vec3 skyColor = mix(uColorSkyNear, uColorSkyFar, sky_color_factor);
    // Смешиваем цвет земли и неба
    return mix(uColorGround, skyColor, sky_blend_factor);
}

// Вычисление интенсивности линии сетки с использованием fwidth для сглаживания
float getGridLineIntensity(vec2 grid_uv, float line_width_factor) {
    // grid_uv: Координаты в пространстве сетки (уже с перспективой и плотностью)
    // line_width_factor: Множитель для управления видимой толщиной (e.g., 0.5 - 1.5)

    // Расстояние до линии по каждой оси [0 = на линии, 0.5 = в центре ячейки]
    vec2 dist_to_line = 0.5 - abs(fract(grid_uv) - 0.5);

    // Производная координат сетки (насколько быстро они меняются на пиксель)
    // fwidth() = abs(dFdx(p)) + abs(dFdy(p))
    // Требует, чтобы вычисления происходили во фрагментном шейдере
    vec2 fw = fwidth(grid_uv);

    // Рассчитываем интенсивность для каждой оси, используя производную для ширины перехода
    // smoothstep(edge0, edge1, x) -> переход от 0 к 1 при x от edge0 до edge1
    // Мы хотим переход от 1 к 0, когда dist_to_line уходит от 0.
    // Границы: 0.0 (полная интенсивность) и fw * line_width_factor (нулевая интенсивность)
    vec2 intensity_axis = smoothstep(fw * line_width_factor, vec2(0.0), dist_to_line);

    // Возвращаем максимальную интенсивность (если пиксель на верт. или гориз. линии)
    return max(intensity_axis.x, intensity_axis.y);
}

// Вычисление фактора тумана (экспоненциальный)
float getFogFactor(float depth) {
    // depth: Расстояние/глубина точки
    // density: Плотность тумана
    float fogCoordinate = depth;
    // Экспоненциальный туман (exp2)
    float fog = 1.0 - exp(-pow(fogCoordinate * uFogDensity, 2.0));
    return clamp(fog, 0.0, 1.0); // Ограничиваем от 0 до 1
}


// --- Основная функция ---
void main() {
    // Используем входные текстурные координаты, которые были интерполированы
    vec2 uv = vTexCoord_out;

    // 1. Рассчитываем базовый цвет фона (земля/небо)
    vec3 finalColor = getBackgroundColor(uv.y);
    float finalFogFactor = 0.0; // Инициализируем фактор тумана

    // 2. Рисуем сетку и применяем туман только ниже горизонта
    if (uv.y < uHorizonY) {
        // --- Расчет перспективы ---
        // Добавляем малое число для стабильности у горизонта
        // Деление на 0 может произойти точно на горизонте, но smoothstep в фоне и этот if должны это обрабатывать
        // Убедитесь, что uHorizonY действительно находится в диапазоне (0, 1), а не точно 0 или 1, если uv может их достигать.
        float perspective_divisor = max(uHorizonY - uv.y, 0.001); // Используем max для предотвращения деления на ноль или отрицательные числа

        // Преобразуем Y для создания глубины, добавляем анимацию
        float y_transformed = (1.0 / perspective_divisor) * uPerspectiveFactor;
        y_transformed += uTime * uScrollSpeed; // Анимация прокрутки
        // Преобразуем X для схождения линий к точке схода на горизонте
        float x_transformed = (uv.x - 0.5) / perspective_divisor; // uv.x - 0.5 центрирует X

        // --- Расчет координат сетки ---
        vec2 grid_uv = vec2(x_transformed, y_transformed) * uGridDensity;

        // --- Расчет интенсивности сетки ---
        float gridIntensity = getGridLineIntensity(grid_uv, uLineWidth);

        // --- Расчет и применение тумана ---
        if (uFogEnable) {
            // Глубина для тумана может быть связана с y_transformed или perspective_divisor
            // Используем 1.0 / perspective_divisor как меру "расстояния" от зрителя
            float depth = (1.0 / perspective_divisor);
            finalFogFactor = getFogFactor(depth);

            // Опционально ослабляем сетку туманом
            if (uGridFogEnable) {
                // Уменьшаем интенсивность сетки по мере увеличения тумана
                gridIntensity *= (1.0 - finalFogFactor);
            }
        }

        // --- Смешивание цвета сетки ---
        // Смешиваем цвет сетки с текущим цветом (фоном) на основе интенсивности линии
        finalColor = mix(finalColor, uColorGrid, gridIntensity);

    } // Конец if (uv.y < uHorizonY)

    // 3. Применяем цвет тумана ко всему (поверх сетки/фона)
    // Туман влияет на все пиксели, но его интенсивность (finalFogFactor) была рассчитана
    // только для пикселей ниже горизонта (где она не равна 0).
    if (uFogEnable) {
        // Цвет тумана часто совпадает с цветом неба у горизонта или в зените.
        // Здесь используется uColorSkyFar, что логично для дальнего тумана.
        finalColor = mix(finalColor, uColorSkyFar, finalFogFactor);
    }

    // 4. Выводим итоговый цвет. Alpha обычно 1.0 (непрозрачный).
    FragColor = vec4(finalColor, 1.0);
}
