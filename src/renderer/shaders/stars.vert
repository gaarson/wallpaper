// src/renderer/shaders/stars.vert
#version 300 es
precision highp float;

#define MAX_LENSES 10

in vec3 aPosition;
in float aBaseSize;
in float aBaseBrightness;

uniform mat4 uProjectionMatrix;
uniform vec3 uCameraPos;
uniform vec2 uResolution;
uniform float uCameraSpeed;
uniform float uUniverseSize;

uniform vec3 uLensPos[MAX_LENSES];
uniform float uLensRadius[MAX_LENSES];
uniform float uLensStrength[MAX_LENSES];
uniform float uLensDepthRange[MAX_LENSES];
uniform int uActiveLensCount;

uniform float uStarBaseSize;
uniform float uStarBaseBrightness;

uniform vec3 uNebulaColor1;
uniform vec3 uNebulaColor2;


out float vDepth;
out float vCameraSpeed;
out vec3 vBaseColor;
out float vCameraSpaceZ;
out float vLensEffect;
out float vFinalBrightness;
out float vStableHash;

float hash31(vec3 p) {
    // [ИСПРАВЛЕНО] Умножаем на "магические" простые числа, 
    // чтобы разбить пространственную корреляцию.
    // Это гарантирует, что у близких по координатам звезд будут разные хэши.
    vec3 p3 = fract(p * vec3(.1031, .11369, .13157)); 
    p3 += dot(p3, p3.yzx + 19.19);
    return fract((p3.x + p3.y) * p3.z);
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
    // 1. Логика "беговой дорожки"
    vec3 offset = aPosition - uCameraPos;
    vec3 wrapped_offset = mod(offset + (uUniverseSize / 2.0), uUniverseSize) - (uUniverseSize / 2.0);
    vec3 star_pos = uCameraPos + wrapped_offset;
    
    // 2. Генерация vBaseColor
    vStableHash = hash31(aPosition);
    vec3 base_color = mix(uNebulaColor1, uNebulaColor2, vStableHash);
    float white_mix_factor = (aBaseBrightness - 0.3) / 0.7;
    vBaseColor = mix(base_color, vec3(1.0), white_mix_factor * 0.75);

    // 3. ЛОГИКА ЛИНЗ
    vec3 distorted_pos = star_pos;
    vLensEffect = 0.0;
    for (int i = 0; i < MAX_LENSES; i++) {
        if (i >= uActiveLensCount) break;
        distorted_pos = apply_grav_lens(distorted_pos, uLensPos[i], uLensRadius[i], uLensStrength[i]);
        float radial_dist = distance(star_pos, uLensPos[i]);
        float radial_effect = 1.0 - smoothstep(0.0, uLensRadius[i], radial_dist);
        radial_effect = pow(radial_effect, 2.0);
        float z_dist = abs(star_pos.z - uLensPos[i].z);
        float depth_effect = 1.0 - smoothstep(0.0, uLensDepthRange[i], z_dist);
        depth_effect = pow(depth_effect, 2.0);
        vLensEffect = max(vLensEffect, radial_effect * depth_effect);
    }

    // 4. ЛОГИКА ПРОЕКЦИИ
    vec3 camera_space_pos = distorted_pos - uCameraPos;
    vCameraSpaceZ = camera_space_pos.z;
    gl_Position = uProjectionMatrix * vec4(camera_space_pos, 1.0);
    
    float f_dist = length(camera_space_pos);
    f_dist = max(f_dist, 0.5);

    float final_base_size = aBaseSize * uStarBaseSize;
    gl_PointSize = (1000.0 / f_dist) * (uResolution.y / 10000.0) * final_base_size;
    
    vDepth = gl_Position.z / gl_Position.w;
    vCameraSpeed = uCameraSpeed;
    vFinalBrightness = aBaseBrightness * uStarBaseBrightness;
}
