#version 300 es
precision highp float;

// Входные атрибуты из VBO
in vec4 aPosition;
in vec2 aTexCoord;

// Выходная переменная для фрагментного шейдера
out vec2 vTexCoord_out;

void main() {
    gl_Position = aPosition;
    vTexCoord_out = aTexCoord;
}
