#version 300 es
precision highp float;

in vec4 aPosition;
in vec2 aTexCoord;

out vec2 vTexCoord_out;

void main() {
    gl_Position = aPosition;
    vTexCoord_out = aTexCoord;
}
