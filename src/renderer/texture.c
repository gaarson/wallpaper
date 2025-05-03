// mode_texture.c
#include "texture.h"
#include "./../shader_utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h> // For strdup
#include <GLES3/gl3.h>

// Define STB_IMAGE_IMPLEMENTATION in exactly one C file
#define STB_IMAGE_IMPLEMENTATION
#include "./../stb_image.h" // Include stb_image implementation here

// --- Define shader file paths ---
#define TEXTURE_VERTEX_SHADER "src/renderer/shaders/texture.vert"
#define TEXTURE_FRAGMENT_SHADER "src/renderer/shaders/texture.frag"

// --- State ---
typedef struct {
    GLuint shader_program;
    GLuint texture_id;
    GLint uniform_texture_sampler;
    GLint uniform_resolution;       // Screen resolution uniform loc
    GLint uniform_texture_resolution; // Texture resolution uniform loc

    int tex_width;
    int tex_height;
    char* image_path; // Store the path for potential reloads/debugging
} TextureModeState;

// --- Helper to load texture ---
static bool load_texture_internal(TextureModeState* state, const char* image_path) {
     if (!state || !image_path) return false;

     // Delete previous texture if it exists
     if (state->texture_id != 0) {
         glDeleteTextures(1, &state->texture_id);
         state->texture_id = 0;
     }
     state->tex_width = 0;
     state->tex_height = 0;
     free(state->image_path); // Free old path if any
     state->image_path = NULL;

     int width, height, channels;
     stbi_set_flip_vertically_on_load(1); // Flip for OpenGL coord system
     unsigned char *img_data = stbi_load(image_path, &width, &height, &channels, 0);
     stbi_set_flip_vertically_on_load(0); // Reset flip

     if (!img_data) {
         fprintf(stderr, "ModeTexture Error: Failed to load image '%s': %s\n", image_path, stbi_failure_reason());
         return false;
     }

     GLenum format = GL_RGB;
     GLenum internal_format = GL_RGB; // GLES2 often requires internalformat = format
     if (channels == 4) { format = GL_RGBA; internal_format = GL_RGBA; }
     else if (channels == 3) { format = GL_RGB; internal_format = GL_RGB; }
     else if (channels == 1) { format = GL_LUMINANCE; internal_format = GL_LUMINANCE; }
     else {
         fprintf(stderr, "ModeTexture Error: Unsupported channel count (%d) for image '%s'\n", channels, image_path);
         stbi_image_free(img_data);
         return false;
     }

     glGenTextures(1, &state->texture_id);
     if (state->texture_id == 0) {
         fprintf(stderr, "ModeTexture Error: glGenTextures failed.\n");
         stbi_image_free(img_data);
         return false;
     }

     glBindTexture(GL_TEXTURE_2D, state->texture_id);
     // Set alignment based on format (important for RGB/LUMINANCE)
     glPixelStorei(GL_UNPACK_ALIGNMENT, (channels == 1 || channels == 3) ? 1 : 4);
     glTexImage2D(GL_TEXTURE_2D, 0, internal_format, width, height, 0, format, GL_UNSIGNED_BYTE, img_data);
     glPixelStorei(GL_UNPACK_ALIGNMENT, 4); // Restore default

     stbi_image_free(img_data); // Free CPU image data

     // Set texture parameters
     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); // Bilinear filtering
     glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
     // glGenerateMipmap(GL_TEXTURE_2D); // Optional: requires different MIN_FILTER

     glBindTexture(GL_TEXTURE_2D, 0); // Unbind

     GLenum gl_err = glGetError();
     if (gl_err != GL_NO_ERROR) {
          fprintf(stderr, "ModeTexture Error: OpenGL error after texture load: 0x%x\n", gl_err);
          glDeleteTextures(1, &state->texture_id);
          state->texture_id = 0;
          return false;
     }

     state->tex_width = width;
     state->tex_height = height;
     state->image_path = strdup(image_path); // Store the path
     if (!state->image_path) { perror("ModeTexture strdup image_path"); /* Non-fatal */ }

     printf("ModeTexture: Loaded texture '%s' (ID: %u, %dx%d, %d ch)\n",
            image_path, state->texture_id, width, height, channels);
     return true;
 }

// --- Interface Functions ---

static void* texture_init(const char* arg, GLuint common_vbo) {
    if (!arg || arg[0] == '\0') {
        fprintf(stderr, "ModeTexture Error: Image path argument is required.\n");
        return NULL;
    }
    (void)common_vbo; // Stored in params
    printf("ModeTexture: Initializing with image path: %s\n", arg);

    TextureModeState* state = calloc(1, sizeof(TextureModeState));
    if (!state) { perror("ModeTexture calloc state"); return NULL; }

    state->texture_id = 0; // Ensure texture ID starts at 0

    // Create shader program
    state->shader_program = create_program_from_files(TEXTURE_VERTEX_SHADER, TEXTURE_FRAGMENT_SHADER);
    if (!state->shader_program) {
        fprintf(stderr, "ModeTexture Error: Failed to create shader program.\n");
        free(state);
        return NULL;
    }

    // Get uniform locations (crucial for aspect ratio correction)
    state->uniform_texture_sampler = glGetUniformLocation(state->shader_program, "uTexture");
    state->uniform_resolution = glGetUniformLocation(state->shader_program, "uResolution");
    state->uniform_texture_resolution = glGetUniformLocation(state->shader_program, "uTextureResolution");

    if (state->uniform_texture_sampler == -1) fprintf(stderr, "ModeTexture Warning: Uniform 'uTexture' not found.\n");
    if (state->uniform_resolution == -1) fprintf(stderr, "ModeTexture Warning: Uniform 'uResolution' not found.\n");
    if (state->uniform_texture_resolution == -1) fprintf(stderr, "ModeTexture Warning: Uniform 'uTextureResolution' not found.\n");


    // Load the texture
    if (!load_texture_internal(state, arg)) {
         fprintf(stderr, "ModeTexture Error: Failed to load texture.\n");
         glDeleteProgram(state->shader_program); // Clean up already created program
         free(state->image_path); // load_texture_internal might have strdup'd
         free(state);
         return NULL;
    }

    printf("ModeTexture: Initialized (Program: %u, Texture: %u).\n", state->shader_program, state->texture_id);
    return state;
}

static void texture_cleanup(void* mode_state) {
    TextureModeState* state = (TextureModeState*)mode_state;
    if (!state) return;
    printf("ModeTexture: Cleaning up...\n");
    if (state->shader_program) {
        glDeleteProgram(state->shader_program);
        printf("ModeTexture: Shader program deleted.\n");
    }
    if (state->texture_id) {
        glDeleteTextures(1, &state->texture_id);
        printf("ModeTexture: Texture deleted.\n");
    }
    free(state->image_path);
    free(state);
}

static bool texture_render(void* mode_state, const RenderParams* params) {
    TextureModeState* state = (TextureModeState*)mode_state;
    if (!state || !state->shader_program || state->texture_id == 0) {
        // If texture failed but state exists, render black? Or rely on core fallback?
        // Let's assume if we got here, texture is valid.
        if (!state || !state->shader_program) return false; // Shader must exist
        if (state->texture_id == 0) { // Texture missing, clear to black
             glClearColor(0.1f, 0.0f, 0.1f, 1.0f); // Dark Magenta to indicate error
             glClear(GL_COLOR_BUFFER_BIT);
             return true;
        }
    }

    glUseProgram(state->shader_program);

    // Activate texture unit 0 and bind the texture
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, state->texture_id);

    // Set uniforms
    if (state->uniform_texture_sampler != -1) {
        glUniform1i(state->uniform_texture_sampler, 0); // Tell sampler to use texture unit 0
    }
    if (state->uniform_resolution != -1) {
        glUniform2f(state->uniform_resolution, (GLfloat)params->physical_width, (GLfloat)params->physical_height);
    }
    if (state->uniform_texture_resolution != -1) {
        glUniform2f(state->uniform_texture_resolution, (GLfloat)state->tex_width, (GLfloat)state->tex_height);
    }

    glBindBuffer(GL_ARRAY_BUFFER, params->common_vbo);

    // Setup vertex attributes
    GLint pos_loc = glGetAttribLocation(state->shader_program, "aPosition");
    GLint tex_loc = glGetAttribLocation(state->shader_program, "aTexCoord");

    const GLsizei stride = 4 * sizeof(GLfloat);
    const void* pos_offset = (void*)0;
    const void* tex_offset = (void*)(2 * sizeof(GLfloat));

    if (pos_loc >= 0) {
        glVertexAttribPointer(pos_loc, 2, GL_FLOAT, GL_FALSE, stride, pos_offset);
        glEnableVertexAttribArray(pos_loc);
    }
    if (tex_loc >= 0) {
        glVertexAttribPointer(tex_loc, 2, GL_FLOAT, GL_FALSE, stride, tex_offset);
        glEnableVertexAttribArray(tex_loc);
    }

    glDrawArrays(GL_TRIANGLES, 0, 6); // Draw the quad

    // Disable vertex attributes
    if (pos_loc >= 0) glDisableVertexAttribArray(pos_loc);
    if (tex_loc >= 0) glDisableVertexAttribArray(tex_loc);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0); // Unbind texture
    glUseProgram(0);

    return true;
}

// Implement other functions as stubs
static bool texture_handle_command(void* mode_state, const char* command) { (void)mode_state; (void)command; return false; }
static void texture_resize(void* mode_state, int w, int h) {
    // Resize might be important if the shader does complex calculations based on aspect ratio beyond the VS
     (void)mode_state; (void)w; (void)h;
 }
static bool texture_needs_redraw(void* mode_state) { (void)mode_state; return false; } // Static texture

// --- Interface Instance ---
const RenderModeInterface texture_mode_interface = {
    .init = texture_init,
    .cleanup = texture_cleanup,
    .render = texture_render,
    .handle_command = texture_handle_command,
    .resize = texture_resize,
    .needs_redraw = texture_needs_redraw,
};

// --- Shaders/texture.vert ---
/* Contents should be:
#version 100
precision highp float;
precision highp int;

attribute vec4 aPosition; // Vertex coords (-1..1)
attribute vec2 aTexCoord; // Base texture coords (0..1)

// Uniforms for aspect ratio correction ('cover' mode)
uniform vec2 uResolution;        // Physical screen resolution
uniform vec2 uTextureResolution; // Actual texture dimensions

varying vec2 vTexCoord; // Output: Corrected texture coordinates
// varying vec2 vDeviceCoord; // Not strictly needed by texture frag shader, but pass anyway if VS is shared

void main() {
    gl_Position = aPosition;
    // vDeviceCoord = aPosition.xy; // Pass if needed

    // Calculate texture coordinates for 'cover' scaling
    float screenAspect = 1.0;
    if (uResolution.y > 0.0) {
        screenAspect = uResolution.x / uResolution.y;
    }

    float textureAspect = 1.0;
    if (uTextureResolution.y > 0.0) {
        textureAspect = uTextureResolution.x / uTextureResolution.y;
    }

    float scaleX = 1.0, scaleY = 1.0;
    // Avoid division by zero or using aspect if resolution is invalid
    if (textureAspect > 0.0 && screenAspect > 0.0) {
         if (textureAspect > screenAspect) {
             // Texture wider than screen (aspect ratio > screen aspect ratio)
             // Fit texture height to screen height (scaleY = 1.0)
             // Scale texture width down to fit (scaleX < 1.0)
             scaleY = 1.0;
             scaleX = screenAspect / textureAspect;
         } else {
             // Texture taller than screen (or same aspect ratio)
             // Fit texture width to screen width (scaleX = 1.0)
             // Scale texture height down to fit (scaleY < 1.0)
             scaleX = 1.0;
             scaleY = textureAspect / screenAspect;
         }
    }

    // Center the texture coordinates and apply the calculated scale
    vTexCoord.x = (aTexCoord.x - 0.5) * scaleX + 0.5;
    vTexCoord.y = (aTexCoord.y - 0.5) * scaleY + 0.5;
}
*/

// --- Shaders/texture.frag ---
/* Contents should be:
#version 100
precision mediump float; // Medium precision is usually enough for textures

uniform sampler2D uTexture; // The texture sampler
varying vec2 vTexCoord;    // Input: Interpolated texture coordinates from vertex shader

void main() {
    // Sample the texture at the calculated coordinates
    gl_FragColor = texture2D(uTexture, vTexCoord);
}
*/
