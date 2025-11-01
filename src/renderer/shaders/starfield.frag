#version 300 es
precision highp float;

in vec2 vTexCoord_out;
out vec4 FragColor;

uniform vec2 uResolution;
uniform float uTime;
uniform float uBrightness;
uniform float uNebulaBrightness;
uniform float uNebulaDensity;
uniform vec3 uNebulaColor1;
uniform vec3 uNebulaColor2;

// --- ДОБАВЛЯЕМ UNIFORM'Ы ЛИНЗЫ И КАМЕРЫ ---
uniform vec3 uCameraPos;
uniform vec3 uLensPos;
uniform float uLensRadius;
uniform float uLensStrength;

// --- 3D-ХЕШИРОВАНИЕ И ШУМ (БЕЗ ИЗМЕНЕНИЙ) ---
float hash31(vec3 p) {
    p = fract(p * 0.1031); p += dot(p, p.zyx + 33.33); return fract((p.x + p.y) * p.z);
}
float noise3D(vec3 p) {
    // ... (код noise3D без изменений) ...
    vec3 ip = floor(p); vec3 fp = fract(p);
    fp = fp * fp * (3.0 - 2.0 * fp);
    float n000 = hash31(ip);
    float n100 = hash31(ip + vec3(1, 0, 0));
    float n010 = hash31(ip + vec3(0, 1, 0));
    float n110 = hash31(ip + vec3(1, 1, 0));
    float n001 = hash31(ip + vec3(0, 0, 1));
    float n101 = hash31(ip + vec3(1, 0, 1));
    float n011 = hash31(ip + vec3(0, 1, 1));
    float n111 = hash31(ip + vec3(1, 1, 1));
    float x0 = mix(n000, n100, fp.x);
    float x1 = mix(n010, n110, fp.x);
    float x2 = mix(n001, n101, fp.x);
    float x3 = mix(n011, n111, fp.x);
    float y0 = mix(x0, x1, fp.y);
    float y1 = mix(x2, x3, fp.y);
    return mix(y0, y1, fp.z);
}
float fbm3D(vec3 p) {
    // ... (код fbm3D без изменений) ...
    float value = 0.0; float amplitude = 0.5; float frequency = 1.0;
    for (int i = 0; i < 4; i++) {
        value += amplitude * noise3D(p * frequency);
        p *= 2.0; amplitude *= 0.5; frequency *= 2.0;
    }
    return value;
}

// --- 3D ТУМАННОСТЬ (ИЗМЕНЕНО) ---
// Теперь она принимает 'distortion_uv'
vec3 draw_nebula_3D(vec3 world_pos, vec2 distortion_uv) {
    if (uNebulaBrightness <= 0.0) return vec3(0.0);
    
    // Координаты для сэмплирования туманности
    vec3 nebula_uv = world_pos * uNebulaDensity * 0.5 + uTime * 0.02;
    
    // --- ПРИМЕНЯЕМ ИСКАЖЕНИЕ ---
    nebula_uv.xy += distortion_uv; 
    
    float noise = fbm3D(nebula_uv);
    vec3 nebula_color = mix(uNebulaColor1, uNebulaColor2, noise) * uNebulaBrightness;
    return nebula_color;
}

// --- ГЛАВНАЯ ФУНКЦИЯ (ИЗМЕНЕНО) ---
void main() {
    vec2 screen_uv = (gl_FragCoord.xy / uResolution.xy) * 2.0 - 1.0;
    screen_uv.x *= uResolution.x / uResolution.y;
    vec3 ray_direction = normalize(vec3(screen_uv, 1.0));

    // --- ЛОГИКА "МАРКЕРА" ЛИНЗЫ ---
    vec2 nebula_distortion = vec2(0.0);

    // t = (lens.z - cam.z) / ray.z
    // Находим "время" (t), за которое луч (ray) долетит до ПЛОСКОСТИ линзы
    float t = (uLensPos.z - uCameraPos.z) / ray_direction.z;
    
    // Если линза перед нами (t > 0)
    if (t > 0.0) {
        // Находим 3D-точку пересечения луча с плоскостью линзы
        vec3 intersection = uCameraPos + ray_direction * t;
        
        // Находим 2D-расстояние от точки пересечения до центра линзы
        float dist = distance(intersection.xy, uLensPos.xy);
        
        if (dist < uLensRadius) {
            // Мы "смотрим" сквозь линзу!
            
            // 1.0 в центре линзы, 0.0 на краю
            float percent = 1.0 - (dist / uLensRadius);
            percent = pow(percent, 2.0); // Усилим эффект к центру
            
            // Вектор от точки пересечения К центру линзы
            vec2 dir_to_lens = normalize(uLensPos.xy - intersection.xy);
            
            // "Пинч": сдвигаем UV-координаты туманности к центру линзы
            // (uLensStrength / 50.0) - "магическое" число для контроля силы
            nebula_distortion = dir_to_lens * percent * (uLensStrength / 50.0);
        }
    }
    
    // --- ПРОХОД 1: ТУМАННОСТЬ (ФОН) ---
    // Позиция на "небесной сфере"
    vec3 nebula_pos = (ray_direction * 100.0); 
    // Рисуем туманность, передавая ей наше искажение
    vec3 nebula_color = draw_nebula_3D(nebula_pos, nebula_distortion);
    
    vec3 final_output_color = nebula_color; 
    FragColor = vec4(final_output_color * uBrightness, 1.0);
}
