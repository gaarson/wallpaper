// mode_gradient.c
#include "../common.h"
#include "gradient.h"
#include "./../shader_utils.h" // For create_program_from_files
#include <stdlib.h>
#include <stdio.h>
#include <string.h> // For strcmp
#include <math.h>   // For M_PI, fmod, sin, cos
#include <GLES3/gl3.h> // For GL types and functions

// --- Define shader file paths ---
// Assume they are relative to the executable in a 'shaders' directory
#define GRADIENT_VERTEX_SHADER "src/renderer/shaders/gradient.vert"
#define GRADIENT_FRAGMENT_SHADER "src/renderer/shaders/gradient.frag"

// --- State ---
typedef struct {
    GLuint shader_program;
    GLint uniform_phase;
    // GLint uniform_resolution; // Not needed by this specific shader

    double phase;
    double speed_factor;
} GradientModeState;

// --- Interface Functions ---

static void* gradient_init(const char* arg, GLuint common_vbo) {
    (void)arg; // Might be used later for configuration
    (void)common_vbo; // Stored in params for render
    log_debug("ModeGradient: Initializing...\n");

    GradientModeState* state = calloc(1, sizeof(GradientModeState));
    if (!state) { perror("ModeGradient calloc state"); return NULL; }

    state->phase = 0.0;
    state->speed_factor = 1.0;

    // Create shader program from files
    state->shader_program = create_program_from_files(GRADIENT_VERTEX_SHADER, GRADIENT_FRAGMENT_SHADER);
    if (!state->shader_program) {
        fprintf(stderr, "ModeGradient Error: Failed to create shader program.\n");
        free(state);
        return NULL;
    }

    // Get uniform locations
    state->uniform_phase = glGetUniformLocation(state->shader_program, "uPhase");
    // state->uniform_resolution = glGetUniformLocation(state->shader_program, "uResolution");

    if (state->uniform_phase == -1) {
        fprintf(stderr, "ModeGradient Warning: Uniform 'uPhase' not found in shader.\n");
    }

    log_debug("ModeGradient: Initialized (Program ID: %u).\n", state->shader_program);
    return state;
}

static void gradient_cleanup(void* mode_state) {
    GradientModeState* state = (GradientModeState*)mode_state;
    if (!state) return;
    log_debug("ModeGradient: Cleaning up...\n");
    if (state->shader_program) {
        glDeleteProgram(state->shader_program);
        log_debug("ModeGradient: Shader program deleted.\n");
    }
    free(state);
}

static bool gradient_render(void* mode_state, const RenderParams* params) {
    GradientModeState* state = (GradientModeState*)mode_state;
    if (!state || !state->shader_program) return false;

    // Update animation phase
    double base_phase_change_per_second = (2.0 * M_PI) / 10.0;
    state->phase += params->time_delta_sec * base_phase_change_per_second * state->speed_factor;
    state->phase = fmod(state->phase, 2.0 * M_PI);

    glUseProgram(state->shader_program);

    // Set uniforms
    if (state->uniform_phase != -1) {
        glUniform1f(state->uniform_phase, (GLfloat)state->phase);
    }
    // if (state->uniform_resolution != -1) {
    //     glUniform2f(state->uniform_resolution, (GLfloat)params->physical_width, (GLfloat)params->physical_height);
    // }

    glBindBuffer(GL_ARRAY_BUFFER, params->common_vbo);

    // Setup vertex attributes (assuming standard quad layout: pos(2), tex(2))
    GLint pos_loc = glGetAttribLocation(state->shader_program, "aPosition");
    GLint tex_loc = glGetAttribLocation(state->shader_program, "aTexCoord"); // May not be used by frag shader, but VS needs it

    const GLsizei stride = 4 * sizeof(GLfloat);
    const void* pos_offset = (void*)0;
    const void* tex_offset = (void*)(2 * sizeof(GLfloat));

    if (pos_loc >= 0) {
        glVertexAttribPointer(pos_loc, 2, GL_FLOAT, GL_FALSE, stride, pos_offset);
        glEnableVertexAttribArray(pos_loc);
    }
    // Only enable texcoord if the attribute exists in the linked shader
    if (tex_loc >= 0) {
        glVertexAttribPointer(tex_loc, 2, GL_FLOAT, GL_FALSE, stride, tex_offset);
        glEnableVertexAttribArray(tex_loc);
    }

    glDrawArrays(GL_TRIANGLES, 0, 6); // Draw the quad (6 vertices)

    // Disable vertex attributes
    if (pos_loc >= 0) glDisableVertexAttribArray(pos_loc);
    if (tex_loc >= 0) glDisableVertexAttribArray(tex_loc);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);

    return true;
}

static bool gradient_handle_command(void* mode_state, const char* command) {
    GradientModeState* state = (GradientModeState*)mode_state;
    if (!state) return false;
    // (Same as before: faster, slower, reset_speed)
     if (strcmp(command, "faster") == 0) {
        state->speed_factor *= 1.2;
        log_debug("ModeGradient: Speed factor increased to %.2f\n", state->speed_factor);
        return true;
    } else if (strcmp(command, "slower") == 0) {
        state->speed_factor /= 1.2;
        if (state->speed_factor < 0.1) state->speed_factor = 0.1;
        log_debug("ModeGradient: Speed factor decreased to %.2f\n", state->speed_factor);
        return true;
    } else if (strcmp(command, "reset_speed") == 0) {
        state->speed_factor = 1.0;
        log_debug("ModeGradient: Speed factor reset to %.2f\n", state->speed_factor);
        return true;
    }
    return false;
}

static void gradient_resize(void* mode_state, int physical_width, int physical_height) {
    // This mode doesn't react to resize unless using uResolution uniform
    (void)mode_state;
    (void)physical_width;
    (void)physical_height;
}

static bool gradient_needs_redraw(void* mode_state) {
    (void)mode_state;
    return true; // Animation requires constant redraw
}

// --- Interface Instance ---
const RenderModeInterface gradient_mode_interface = {
    .init = gradient_init,
    .cleanup = gradient_cleanup,
    .render = gradient_render,
    .handle_command = gradient_handle_command,
    .resize = gradient_resize,
    .needs_redraw = gradient_needs_redraw,
};

// --- Shaders/gradient.vert ---
/* Contents should be:
#version 100
precision highp float;
precision highp int;

attribute vec4 aPosition; // Vertex coords (-1..1)
attribute vec2 aTexCoord; // Texture coords (0..1) - passed but not used by gradient fragment shader

// Uniforms for texture aspect ratio calculation (kept for compatibility if VS is shared)
uniform vec2 uResolution;        // Physical screen resolution
uniform vec2 uTextureResolution; // Usually 1x1 if no texture

varying vec2 vDeviceCoord; // Pass device coords (-1..1) to fragment shader
varying vec2 vTexCoord;    // Pass texture coords (calculated for 'cover')

void main() {
    gl_Position = aPosition; // Output position
    vDeviceCoord = aPosition.xy; // Pass device coords directly

    // Calculate texture coordinates for 'cover' mode (needed if VS is shared)
    // If this VS is ONLY for gradient, this calculation can be removed.
    float screenAspect = 1.0; if (uResolution.y > 0.0) screenAspect = uResolution.x / uResolution.y;
    float textureAspect = 1.0; if (uTextureResolution.y > 0.0) textureAspect = uTextureResolution.x / uTextureResolution.y;
    float scaleX = 1.0, scaleY = 1.0;
    // Avoid division by zero
    if (textureAspect > 0.0 && screenAspect > 0.0) {
        if (textureAspect > screenAspect) {
            scaleX = screenAspect / textureAspect; // Texture wider, fit height, scale X down
        } else {
            scaleY = textureAspect / screenAspect; // Texture taller, fit width, scale Y down
        }
    }
    // Apply scale centered
    vTexCoord.x = (aTexCoord.x - 0.5) * scaleX + 0.5;
    vTexCoord.y = (aTexCoord.y - 0.5) * scaleY + 0.5;
}
*/

// --- Shaders/gradient.frag ---
/* Contents should be:
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
*/
