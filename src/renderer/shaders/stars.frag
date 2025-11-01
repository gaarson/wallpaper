// src/renderer/shaders/stars.frag
#version 300 es
precision highp float;
out vec4 FragColor;

in float vDepth;
in float vCameraSpeed;
in vec3 vBaseColor;
in float vCameraSpaceZ; // <--- Нам понадобится эта переменная
in float vLensEffect;
in float vFinalBrightness;
in float vStableHash;

uniform float uDissolveStart;
uniform float uDissolveEnd;
uniform vec3 uNebulaDissolveColor;
uniform float uGlobalFade;
uniform bool uRenderPass_Alive;
uniform float uTailProbability;

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

    float dissolve_factor = smoothstep(uDissolveEnd, uDissolveStart, vCameraSpaceZ);
    dissolve_factor = min(dissolve_factor, 1.0 - vLensEffect);

    if (uRenderPass_Alive) {
        // --- ПРОХОД 4: Рисуем "ЖИВЫЕ" звезды на экран ---
        if (dissolve_factor < 1.0) {
            discard;
        }
        vec3 color = vBaseColor * vFinalBrightness;
        
        // --- [ИСПРАВЛЕНО] Логика Доплера ---
        // 1. Рассчитываем "фактор удаленности" (0.0 для близких, 1.0 для далеких)
        //    Эффект начнет появляться с 5 юнитов и достигнет 100% на 30 юнитах.
        float doppler_factor = smoothstep(5.0, 30.0, vCameraSpaceZ);
        
        // 2. Максимальная сила доплера (как и было)
        float max_doppler = vCameraSpeed * 0.2;
        
        // 3. Итоговый доплер = Макс * Фактор
        float doppler = max_doppler * doppler_factor;
        // --- [КОНЕЦ ИСПРАВЛЕНИЙ] ---

        color.r -= doppler;
        color.b += doppler;
        color = clamp(color, 0.0, 3.0);
        FragColor = vec4(color * uGlobalFade, base_alpha * uGlobalFade);

    } else {
        // --- ПРОХОД 2: Рисуем "ЧАСТИЦЫ" в FBO ---

        // 1. Проверяем, должна ли эта звезда оставлять хвост
        if (vStableHash > uTailProbability) {
            discard;
        }

        // 2. Шум распада (оставляем, как было)
        if (dissolve_factor < 1.0) {
            float noise = hash21(gl_PointCoord.xy * vCameraSpaceZ);
            if (noise > dissolve_factor) {
                discard;
            }
        }
        
        // 3. Вычисляем цвет
        vec3 color = vBaseColor * vFinalBrightness;
        
        // --- [ИСПРАВЛЕНО] Та же самая логика Доплера, что и выше ---
        float doppler_factor = smoothstep(5.0, 30.0, vCameraSpaceZ);
        float max_doppler = vCameraSpeed * 0.2;
        float doppler = max_doppler * doppler_factor;
        // --- [КОНЕЦ ИСПРАВЛЕНИЙ] ---

        color.r -= doppler;
        color.b += doppler;
        color = clamp(color, 0.0, 3.0);
        
        // 4. Логика яркости
        float dissolve_brightness = (1.0 - dissolve_factor) * 2.0;
        float base_glow = vFinalBrightness * 0.2;
        float final_particle_brightness = base_glow + dissolve_brightness;
        
        FragColor = vec4(color * final_particle_brightness * base_alpha, 1.0);
    }
}
