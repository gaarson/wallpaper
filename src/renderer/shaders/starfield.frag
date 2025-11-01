// src/renderer/shaders/starfield.frag
#version 300 es
precision highp float;

#define MAX_LENSES 10

in vec2 vTexCoord_out;
out vec4 FragColor;

// Uniform'ы для 3D-шума (Фоновый газ)
uniform vec2 uResolution;
uniform float uTime;
uniform float uNebulaBrightness;
uniform float uNebulaDensity;
uniform vec3 uNebulaColor1;
uniform vec3 uNebulaColor2;
uniform vec3 uCameraPos;

uniform vec3 uLensPos[MAX_LENSES];
uniform float uLensRadius[MAX_LENSES];
uniform float uLensStrength[MAX_LENSES];
uniform int uActiveLensCount;


// Uniform для FBO (Динамическая туманность)
uniform sampler2D uDynamicNebula;
// ... (функции hash31, noise3D, fbm3D без изменений) ...
float hash31(vec3 p) {
    p = fract(p * 0.1031); p += dot(p, p.zyx + 33.33); return fract((p.x + p.y) * p.z);
}
float noise3D(vec3 p) {
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
    float value = 0.0; float amplitude = 0.5; float frequency = 1.0;
    for (int i = 0; i < 4; i++) {
        value += amplitude * noise3D(p * frequency);
        p *= 2.0; amplitude *= 0.5; frequency *= 2.0;
    }
    return value;
}

vec3 draw_nebula_3D(vec3 world_pos, vec2 distortion_uv) {
    if (uNebulaBrightness <= 0.0) return vec3(0.0);
    vec3 nebula_uv = world_pos * uNebulaDensity * 0.5 + uTime * 0.02;
    nebula_uv.xy += distortion_uv;
    float noise = fbm3D(nebula_uv);
    vec3 nebula_color = mix(uNebulaColor1, uNebulaColor2, noise) * uNebulaBrightness;
    return nebula_color;
}

void main() {
    vec2 screen_uv = (gl_FragCoord.xy / uResolution.xy) * 2.0 - 1.0;
    screen_uv.x *= uResolution.x / uResolution.y;

    const float f = 1.4281; 
    vec3 ray_direction = normalize(vec3(screen_uv, f));
    
    vec2 nebula_distortion = vec2(0.0);
    
    for (int i = 0; i < MAX_LENSES; i++) {
        if (i >= uActiveLensCount) break;
        
        float t = (uLensPos[i].z - uCameraPos.z) / ray_direction.z;
        if (t > 0.0) {
            vec3 intersection = uCameraPos + ray_direction * t;
            float dist = distance(intersection.xy, uLensPos[i].xy);
            
            if (dist < uLensRadius[i]) {
                // [ИСПРАВЛЕНО] Было uRadius[i]
                float percent = 1.0 - (dist / uLensRadius[i]);
                percent = pow(percent, 2.0);
                vec2 dir_to_lens = normalize(uLensPos[i].xy - intersection.xy);
                nebula_distortion += dir_to_lens * percent * (uLensStrength[i] / 50.0); 
            }
        }
    }

    // --- 1. Получаем фоновый 3D-газ (как раньше) ---
    vec3 nebula_pos = uCameraPos + (ray_direction * 100.0);
    vec3 gas_color = draw_nebula_3D(nebula_pos, nebula_distortion);

    // --- 2. Получаем динамическую туманность из FBO ---
    vec2 fbo_uv = vTexCoord_out + nebula_distortion;
    vec3 dynamic_nebula_color = texture(uDynamicNebula, fbo_uv).rgb;

    // --- 3. Смешиваем их ---
    vec3 final_output_color = gas_color + dynamic_nebula_color;

    FragColor = vec4(final_output_color, 1.0);
}
