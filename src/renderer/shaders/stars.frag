// src/renderer/shaders/stars.frag
#version 300 es
precision highp float;
out vec4 FragColor;

in float vDepth;
in float vCameraSpeed;
in vec3 vBaseColor;
in float vCameraSpaceZ;
in float vLensEffect; // <-- ПРИНИМАЕМ СИЛУ ЛИНЗЫ

uniform float uDissolveStart;
uniform float uDissolveEnd;
uniform vec3 uNebulaDissolveColor;
uniform float uGlobalFade;         
uniform bool uRenderPass_Alive; // <-- НОВЫЙ КОНТРОЛЛЕР

float hash21(vec2 p) {
    p = fract(p * vec2(123.34, 456.21));
    p += dot(p, p.yx + 19.19);
    return fract(p.x * p.y);
}

void main() {
    vec2 circ = gl_PointCoord - 0.5;
    float r = length(circ);
    float base_alpha = max(0.0, 1.0 - r*r*4.0);
    base_alpha *= (1.0 - smoothstep(0.99, 1.3, vDepth));

    // 1. Получаем обычный фактор растворения по Z-координате
    float dissolve_factor = smoothstep(uDissolveEnd, uDissolveStart, vCameraSpaceZ);

    // --- НОВОЕ: ЭФФЕКТ ЛИНЗЫ ---
    // vLensEffect = 1.0 (в центре), 0.0 (далеко)
    // 1.0 - vLensEffect = 0.0 (в центре), 1.0 (далеко)
    // min(dissolve_factor, 0.0) = 0.0 (мгновенное растворение в центре)
    // min(dissolve_factor, 1.0) = dissolve_factor (нет эффекта)
    dissolve_factor = min(dissolve_factor, 1.0 - vLensEffect);
    // --- КОНЕЦ НОВОГО БЛОКА ---

    if (uRenderPass_Alive) {
        // --- ПРОХОД 4: Рисуем "ЖИВЫЕ" звезды на экран ---
        
        // (Эта логика теперь автоматически отработает
        //  для звезд, попавших в линзу)
        if (dissolve_factor < 1.0) {
            discard;
        }

        vec3 color = vBaseColor;
        // ... (остальной код 'if' блока без изменений) ...
        float doppler = vCameraSpeed * 0.2;
        color.r -= doppler;
        color.b += doppler;
        color = clamp(color, 0.0, 3.0); 
        FragColor = vec4(color * uGlobalFade, base_alpha * uGlobalFade);

    } else {
        // --- ПРОХОД 2: Рисуем "ЧАСТИЦЫ" в FBO ---

        // (Эта логика тоже отработает)
        if (dissolve_factor == 1.0) {
            discard;
        }

        // Шум распада
        if (dissolve_factor < 1.0) {
            float noise = hash21(gl_PointCoord.xy * vCameraSpaceZ);
            if (noise > dissolve_factor) {
                discard;
            }
        }
        
        vec3 color = vBaseColor;
        // ... (остальной код 'else' блока без изменений) ...
        color = mix(color, uNebulaDissolveColor, 1.0 - dissolve_factor);
        float doppler = vCameraSpeed * 0.2;
        color.r -= doppler;
        color.b += doppler;
        color = clamp(color, 0.0, 3.0); 
        float particle_brightness = (1.0 - dissolve_factor) * 2.0; 
        FragColor = vec4(color * particle_brightness * base_alpha, 1.0);
    }
}
