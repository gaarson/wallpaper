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
uniform float uLensDepthRange; 

out float vDepth;
out float vCameraSpeed;
out vec3 vBaseColor;
out float vCameraSpaceZ;
out float vLensEffect;

float hash31(vec3 p) {
    p = fract(p * 0.1031);
    p += dot(p, p.zyx + 33.33);
    return fract((p.x + p.y) * p.z);
}

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
    // ... (Логика "беговой дорожки" и vBaseColor без изменений) ...
    float z_offset = aPosition.z - uCameraPos.z;
    float wrapped_z_offset = mod(z_offset + (uUniverseSize / 2.0), uUniverseSize) - (uUniverseSize / 2.0);
    vec3 star_pos = vec3(aPosition.x, aPosition.y, uCameraPos.z + wrapped_z_offset);
    // ... (генерация vBaseColor) ...

    // --- 3. ЛОГИКА ЛИНЗ ---
    // Визуальное смещение (как и было)
    vec3 distorted_pos = apply_grav_lens(star_pos, uLensPos, uLensRadius, uLensStrength);

    // --- ИЗМЕНЕНИЕ: Вычисляем "Силу" эффекта ---
    
    // 1. Радиальный эффект (как было)
    //    (Насколько звезда близка к центру линзы по X/Y/Z)
    float radial_dist = distance(star_pos, uLensPos);
    float radial_effect = 1.0 - smoothstep(0.0, uLensRadius, radial_dist);
    radial_effect = pow(radial_effect, 2.0);

    // 2. Глубинный эффект (НОВОЕ)
    //    (Насколько звезда близка к Z-плоскости линзы)
    float z_dist = abs(star_pos.z - uLensPos.z);
    // 1.0 - smoothstep(0.0, uLensDepthRange, z_dist)
    // Если z_dist = 0, эффект = 1.0 (полный)
    // Если z_dist = uLensDepthRange, эффект = 0.0 (нет)
    float depth_effect = 1.0 - smoothstep(0.0, uLensDepthRange, z_dist);
    depth_effect = pow(depth_effect, 2.0); // (усилим эффект к центру плоскости)

    // 3. Итоговый эффект = Радиальный * Глубинный
    //    (Эффект будет только если ОБА условия выполнены)
    vLensEffect = radial_effect * depth_effect;
    // --- КОНЕЦ ИЗМЕНЕНИЙ ---

    // --- 4. ЛОГИКА ПРОЕКЦИИ ---
    vec3 camera_space_pos = distorted_pos - uCameraPos;
    // ... (остальной код main без изменений) ...
    vCameraSpaceZ = camera_space_pos.z;
    gl_Position = uProjectionMatrix * vec4(camera_space_pos, 1.0);
    float f_dist = length(camera_space_pos);
    f_dist = max(f_dist, 0.5);
    gl_PointSize = (1000.0 / f_dist) * (uResolution.y / 10000.0);
    vDepth = gl_Position.z / gl_Position.w;
    vCameraSpeed = uCameraSpeed;
}
