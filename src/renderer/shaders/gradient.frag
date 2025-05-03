#version 100
precision mediump float;

uniform float uPhase;       // Animation phase
varying vec2 vDeviceCoord; // Input: device coordinates (-1..1)

void main() {
    // Normalize Y coordinate from [-1, 1] to [0, 1]
    float yNorm = (vDeviceCoord.y + 1.0) * 0.5;

    // Calculate first color based on phase
    float r1 = 0.5 + 0.5 * sin(uPhase);
    float g1 = 0.1;
    float b1 = 0.5 + 0.5 * cos(uPhase);
    vec3 color1 = vec3(r1, g1, b1);

    // Calculate second color with a phase offset
    float phase2 = uPhase + 3.14159 * 0.8; // Offset phase
    float r2 = 0.5 + 0.5 * sin(phase2);
    float g2 = 0.5 + 0.5 * cos(phase2);
    float b2 = 0.1;
    vec3 color2 = vec3(r2, g2, b2);

    // Linearly interpolate between the two colors based on normalized Y coordinate
    vec3 finalColor = mix(color1, color2, yNorm);

    // Output the final color
    gl_FragColor = vec4(finalColor, 1.0);
}
