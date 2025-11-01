#version 300 es
precision highp float;

in vec3 aPosition; 

uniform mat4 uProjectionMatrix;
uniform vec3 uCameraPos;
uniform vec2 uResolution;
uniform float uCameraSpeed; 
uniform float uUniverseSize;

uniform vec3 uLensPos;
uniform float uLensRadius;
uniform float uLensStrength;

out float vDepth;
out float vCameraSpeed;
out vec3 vBaseColor; // <-- ДОБАВИТЬ: Переменная для цвета

// --- ДОБАВИТЬ: Хеш-функция для генерации "случайного" числа из позиции
float hash31(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 33.33);
    return fract((p.x + p.y) * p.z);
}

// --- (Функция apply_grav_lens БЕЗ ИЗМЕНЕНИЙ) ---
vec3 apply_grav_lens(vec3 star_pos, vec3 lens_pos, float lens_radius, float lens_strength) {
    float dist = distance(star_pos, lens_pos);
    if (dist < lens_radius) {
        float percent = 1.0 - (dist / lens_radius);
        percent = pow(percent, 2.0); 
        vec3 dir_to_lens = normalize(lens_pos - star_pos);
        return star_pos + dir_to_lens * percent * lens_strength;
    }
    return star_pos;
}

void main() {
    // --- 1. "БЕСКОНЕЧНАЯ БЕГОВАЯ ДОРОЖКА" ---
    float z_offset = aPosition.z - uCameraPos.z;
    float wrapped_z_offset = mod(z_offset + (uUniverseSize / 2.0), uUniverseSize) - (uUniverseSize / 2.0);
    vec3 star_pos = vec3(aPosition.x, aPosition.y, uCameraPos.z + wrapped_z_offset);

    // --- 2. ГЕНЕРАЦИЯ БАЗОВОГО ЦВЕТА ---
    // Мы используем *оригинальную* aPosition как "зерно" (seed)
    float color_hash = hash31(aPosition);
    // Смешиваем "красноватую" звезду (тип K) и "голубоватую" (тип B)
    vec3 color_reddish = vec3(1.0, 0.9, 0.7);
    vec3 color_bluish = vec3(0.8, 0.9, 1.0);
    vBaseColor = mix(color_reddish, color_bluish, color_hash); // Передаем цвет "дальше"

    // --- 3. ЛОГИКА ЛИНЗ ---
    vec3 distorted_pos = apply_grav_lens(star_pos, uLensPos, uLensRadius, uLensStrength);
    
    // --- 4. ЛОГИКА ПРОЕКЦИИ ---
    vec3 camera_space_pos = distorted_pos - uCameraPos; 
    gl_Position = uProjectionMatrix * vec4(camera_space_pos, 1.0);
    float dist = length(camera_space_pos);
    dist = max(dist, 0.5); 
    gl_PointSize = (1000.0 / dist) * (uResolution.y / 10000.0);
    vDepth = gl_Position.z / gl_Position.w;
    vCameraSpeed = uCameraSpeed;
}
