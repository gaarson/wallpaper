#version 100
precision mediump float; // Medium precision is usually enough for textures

uniform sampler2D uTexture; // The texture sampler
varying vec2 vTexCoord;    // Input: Interpolated texture coordinates from vertex shader

void main() {
    // Sample the texture at the calculated coordinates
    gl_FragColor = texture2D(uTexture, vTexCoord);
}
