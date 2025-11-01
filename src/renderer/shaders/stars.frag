#version 300 es
precision highp float;
out vec4 FragColor;

in float vDepth;
in float vCameraSpeed;
in vec3 vBaseColor; // <-- ДОБАВИТЬ: Принимаем базовый цвет

void main() {
    vec2 circ = gl_PointCoord - 0.5;
    float r = length(circ);
    
    float alpha = 1.0 - smoothstep(0.4, 0.5, r);
    
    // Затухание у горизонта (оставляем)
    alpha *= (1.0 - smoothstep(0.99, 1.3, vDepth));
    
    // --- ИСПРАВЛЕНИЕ: Доплеровский сдвиг ---
    
    // 1. Берем базовый цвет, сгенерированный в .vert
    vec3 color = vBaseColor; 
    
    // 2. Увеличиваем множитель в 20 раз (0.005 -> 0.1)
    float doppler = vCameraSpeed * 0.2; // Было 0.1
    
    // Применяем сдвиг (летящие на нас звезды становятся синими)
    color.r -= doppler; 
    color.b += doppler;
    
    // Ограничиваем, чтобы цвет не "сломался"
    color = clamp(color, 0.0, 3.0); // Позволяем синему стать "ярче" (до 2.0)
    
    FragColor = vec4(color, alpha);
}
