#version 300 es
precision highp float;

in vec2 vTexCoord_out;
out vec4 FragColor;

// --- СТАРЫЕ UNIFORM'Ы (без изменений) ---
uniform vec2 uResolution;
uniform float uTime;
uniform float uSpeed;
uniform float uDensity;
uniform float uStarThreshold;
uniform float uBrightness;
uniform float uLayer1Speed;
uniform float uLayer2Speed;
uniform float uLayer3Speed;
uniform float uNebulaBrightness;
uniform float uNebulaDensity;
uniform vec3 uNebulaColor1;
uniform vec3 uNebulaColor2;
uniform float uStarBrightness;
uniform float uLensFieldDensity;
uniform float uLensSizeMul;
uniform float uLensGlowMul;
uniform float uLensStrengthMul;

// --- НОВЫЙ UNIFORM ДЛЯ СКОРОСТИ ЛИНЗ ---
uniform float uLensSpeedMul; // Множитель скорости линз
uniform float uLensChance;     // Шанс появления линзы (0.0 до 1.0)


// --- БЛОК ПРОЦЕДУРНОГО ШУМА (без изменений) ---
float hash(vec2 p) { /* ... (код без изменений) ... */
    vec3 p3 = fract(vec3(p.xyx) * .1031); p3 += dot(p3, p3.yzx + 33.33); return fract((p3.x + p3.y) * p3.z);
}
float noise(vec2 p) { /* ... (код без изменений) ... */
    vec2 i = floor(p); vec2 f = fract(p);
    float a = hash(i); float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0)); float d = hash(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.y * u.x;
}
float fbm(vec2 p) { /* ... (код без изменений) ... */
    float value = 0.0; float amplitude = 0.5;
    for (int i = 0; i < 4; i++) {
        value += amplitude * noise(p); p *= 2.0; amplitude *= 0.5;
    }
    return value;
}

// --- Функция рисования звезд (без изменений) ---
vec3 draw_stars(vec2 uv) {
    // ... (весь код функции draw_stars из предыдущей версии)
    vec3 final_color = vec3(0.0);
    // Слой 1
    vec2 uv_layer1 = fract(uv * 0.7 + vec2(uTime * uLayer1Speed, 0.0)); 
    vec2 grid_pos1 = floor(uv_layer1 * uDensity); vec2 star_pos1 = fract(uv_layer1 * uDensity) - 0.5;
    float star_hash1 = hash(grid_pos1); 
    if (star_hash1 > uStarThreshold) { 
        float star_size = fract(star_hash1 * 10.0) * 0.005 + 0.001;
        float intensity = smoothstep(star_size, 0.0, length(star_pos1));
        float color_hash = hash(grid_pos1 + 0.5);
        vec3 star_color = mix(vec3(0.7, 0.7, 1.0), vec3(1.0, 0.7, 0.7), color_hash);
        star_color *= vec3(0.8, 0.8, 1.0); 
        final_color += intensity * star_color;
    }
    // Слой 2
    vec2 uv_layer2 = fract(uv + vec2(uTime * uLayer2Speed, 0.0));
    vec2 grid_pos2 = floor(uv_layer2 * uDensity * 0.5); vec2 star_pos2 = fract(uv_layer2 * uDensity * 0.5) - 0.5;
    float star_hash2 = hash(grid_pos2);
    if (star_hash2 > (uStarThreshold + 0.02)) { 
        float star_size = fract(star_hash2 * 10.0) * 0.008 + 0.002;
        float intensity = smoothstep(star_size, 0.0, length(star_pos2));
        float color_hash = hash(grid_pos2 + 0.5);
        vec3 star_color = mix(vec3(0.8, 0.8, 1.0), vec3(1.0, 1.0, 0.9), color_hash);
        final_color += intensity * star_color;
    }
    // Слой 3
    vec2 uv_layer3 = fract(uv * 0.3 + vec2(uTime * uLayer3Speed, 0.0));
    vec2 grid_pos3 = floor(uv_layer3 * uDensity * 0.2); vec2 star_pos3 = fract(uv_layer3 * uDensity * 0.2) - 0.5;
    float star_hash3 = hash(grid_pos3);
    if (star_hash3 > uStarThreshold) {
        float star_size = fract(star_hash3 * 10.0) * 0.01 + 0.003;
        float intensity = smoothstep(star_size, 0.0, length(star_pos3));
        float color_hash = hash(grid_pos3 + 0.5);
        vec3 star_color = mix(vec3(1.0, 1.0, 0.8), vec3(1.0, 0.8, 0.8), color_hash);
        star_color *= vec3(1.0, 0.95, 0.9);
        final_color += intensity * star_color;
    }
    return final_color * uStarBrightness;
}

// --- Функция рисования туманности (без изменений) ---
vec3 draw_nebula(vec2 uv) {
    if (uNebulaBrightness <= 0.0) return vec3(0.0);
    vec2 nebula_uv = uv * uNebulaDensity + uTime * 0.02; // Медленный дрифт
    float noise = fbm(nebula_uv);
    vec3 nebula_color = mix(uNebulaColor1, uNebulaColor2, noise) * uNebulaBrightness;
    // Ослабление в центре при 'warp' (используем length(uv), а не warp-UV)
    // nebula_color *= smoothstep(0.0, 0.2, length(uv)) * (1.0 - uSpeed * 0.1);
    return nebula_color;
}

// --- Функция рисования линз (ИСПРАВЛЕНА) ---
struct LensInfo {
vec2 distortion;
vec3 glow;
};

LensInfo draw_lenses(vec2 uv) {
vec2 distortion = vec2(0.0);
vec3 glow = vec3(0.0);

// Добавляем небольшой боковой "дрейф", чтобы "сломать" кольца
vec2 uv_drift = uv + vec2(uTime * 0.01, 0.0);
vec2 uv_layer = fract(uv_drift * uLensFieldDensity);
vec2 grid_pos = floor(uv_drift * uLensFieldDensity);

vec2 cell_pos = fract(uv_layer) - 0.5;
float dist_to_cell_center = length(cell_pos);

float lens_hash = hash(grid_pos);

if (lens_hash > (1.0 - uLensChance)) {
// Случайный размер (от 0.5 до 1.0) * множитель * базовый радиус
float rand_size = (hash(grid_pos + 1.1) * 0.5 + 0.5) * uLensSizeMul * 0.1;

if (dist_to_cell_center < rand_size) {
float percent_dist = dist_to_cell_center / rand_size; // 0..1

// 1. Искажение (прозрачный вихрь)
float twirl_strength = (1.0 - percent_dist) * uLensStrengthMul;
float angle = atan(cell_pos.y, cell_pos.x) + twirl_strength;

vec2 distorted_cell_pos = vec2(cos(angle), sin(angle)) * dist_to_cell_center;
// Вектор искажения (разница)
distortion = (distorted_cell_pos - cell_pos) * 0.5; // 0.5 - множитель силы

// 2. Свечение
float rand_glow_strength = hash(grid_pos + 2.2) * uLensGlowMul;
vec3 rand_color = vec3(hash(grid_pos + 3.3), hash(grid_pos + 4.4), hash(grid_pos + 5.5));

// Свечение самое яркое на краю линзы (эффект кольца)
glow = (1.0 - abs(percent_dist - 0.7) * 3.0) * rand_color * rand_glow_strength;
glow = clamp(glow, 0.0, 1.0);
}
}
return LensInfo(distortion, glow);
}

void main() {
    vec2 base_uv = (vTexCoord_out - 0.5) * vec2(uResolution.x / uResolution.y, 1.0);
    float dist_to_center = length(base_uv);

    // Инициализируем direction безопасным значением по умолчанию
    vec2 direction = vec2(0.0, 1.0); 
    // Выполняем нормализацию (деление), только если длина не равна нулю
    if (dist_to_center > 0.00001) {
        direction = base_uv / dist_to_center; // Ручная и безопасная нормализация
    } 
    // 2. ЭФФЕКТ "WARP" (Разделен на два)
    float warp_factor = 1.0 + uSpeed * 5.0;
    float stretched_dist = pow(dist_to_center, 0.8) / warp_factor;
    
    // UV для ЗВЕЗД
    float star_flight_speed = uTime * uSpeed * 0.5;
    vec2 warp_uv_stars = direction * (stretched_dist - star_flight_speed);

    // UV для ЛИНЗ (использует uLensSpeedMul)
    float lens_flight_speed = star_flight_speed * uLensSpeedMul;
    vec2 warp_uv_lenses = direction * (stretched_dist - lens_flight_speed);

    // 3. РАСЧЕТ ЛИНЗ
    // Линзы ищутся в *своем* warp-пространстве
    LensInfo lens = draw_lenses(warp_uv_lenses);

    // 4. ПРИМЕНЕНИЕ ИСКАЖЕНИЯ
    // Искажение от линзы применяется и к фону, и к звездам
    
    // ИСКАЖЕННЫЕ UV ДЛЯ ТУМАННОСТИ (ИСПРАВЛЕНО!)
    // Мы искажаем base_uv (фон), а НЕ warp_uv
    vec2 uv_for_nebula = base_uv + lens.distortion;
    
    // ИСКАЖЕННЫЕ UV ДЛЯ ЗВЕЗД
    vec2 uv_for_stars = warp_uv_stars + lens.distortion;

    // 5. РИСУЕМ ФОН (Туманность)
    // Рисуем в пространстве uv_for_nebula. Полос больше не будет!
    vec3 nebula_color = draw_nebula(uv_for_nebula);
    
    // 6. РИСУЕМ ЗВЕЗДЫ
    vec3 star_color = vec3(0.0);
    float samples = 6.0;
    float streak_length = uSpeed * 0.05; 
    
    for (float i = 0.0; i < samples; i++) {
        float offset = i / samples * streak_length;
        // Звезды рисуются в пространстве uv_for_stars
        star_color += draw_stars(uv_for_stars + direction * offset);
    }
    star_color /= samples;

    // 7. СМЕШИВАЕМ И ФИНАЛИЗИРУЕМ
    
    // Допплер-эффект (используем `dist_to_center` от *неискаженных* UV)
    float doppler_shift = uSpeed * (0.5 - dist_to_center) * 0.5; 
    star_color.r *= (1.0 - doppler_shift);
    star_color.b *= (1.0 + doppler_shift);
    
    // Усиление яркости в центре (тоже от неискаженных UV)
    star_color *= (1.0 + smoothstep(0.3, 0.0, dist_to_center) * uSpeed * 2.0);

    // Смешиваем фон + звезды + свечение линзы (которое не искажается)
    vec3 final_output_color = nebula_color + star_color + lens.glow;

    FragColor = vec4(final_output_color * uBrightness, 1.0); 
}
