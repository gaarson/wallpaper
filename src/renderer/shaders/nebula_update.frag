// src/renderer/shaders/nebula_update.frag
#version 300 es
precision highp float;

in vec2 vTexCoord_out;
out vec4 FragColor;

uniform sampler2D uPreviousNebula;
uniform float uFadeFactor; // (Мы посылаем 0.99)

void main() {
    // Просто читаем цвет из прошлого кадра и делаем его чуть темнее
    vec4 color = texture(uPreviousNebula, vTexCoord_out);
    FragColor = color * uFadeFactor;
}
