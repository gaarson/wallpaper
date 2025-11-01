
#ifndef SHADER_UTILS_H
#define SHADER_UTILS_H

#include <GLES3/gl3.h>
#include <stdbool.h>

/**
 * @brief Loads shader source code from a file.
 * @param filename Path to the shader file.
 * @return A dynamically allocated string containing the shader source,
 * or NULL if the file cannot be read. The caller must free() the string.
 */
char* load_shader_source(const char* filename);

/**
 * @brief Creates a shader program from vertex and fragment shader files.
 * @param vertex_shader_path Path to the vertex shader file.
 * @param fragment_shader_path Path to the fragment shader file.
 * @return The ID of the linked shader program, or 0 if compilation/linking fails.
 */
GLuint create_program_from_files(const char* vertex_shader_path, const char* fragment_shader_path);





#endif 
