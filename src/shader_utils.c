
#include "shader_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h> 


static GLuint compile_shader(GLenum type, const char* source) {
    if (!source) {
        fprintf(stderr, "Shader Error: Shader source is NULL.\n");
        return 0;
    }
    GLuint shader = glCreateShader(type);
    if (shader == 0) {
        fprintf(stderr, "Shader Error: glCreateShader failed for type %d.\n", type);
        return 0;
    }
    glShaderSource(shader, 1, &source, NULL);
    glCompileShader(shader);

    GLint compiled = 0;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
    if (!compiled) {
        GLint infoLen = 0;
        glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 1) {
            char* infoLog = malloc(infoLen);
            if (infoLog) {
                glGetShaderInfoLog(shader, infoLen, NULL, infoLog);
                fprintf(stderr, "Shader Error: Failed to compile shader:\n%s\n", infoLog);
                free(infoLog);
            }
        } else {
            fprintf(stderr, "Shader Error: Failed to compile shader (no info log).\n");
        }
        glDeleteShader(shader);
        return 0;
    }
    return shader;
}



char* load_shader_source(const char* filename) {
    FILE* fp = fopen(filename, "rb"); 
    if (!fp) {
        perror("Shader Utils Error: fopen failed");
        fprintf(stderr, "  -> Failed to open file: %s\n", filename);
        return NULL;
    }

    fseek(fp, 0, SEEK_END);
    long file_size = ftell(fp);
    if (file_size < 0) {
        perror("Shader Utils Error: ftell failed");
        fprintf(stderr, "  -> Failed to get size for file: %s\n", filename);
        fclose(fp);
        return NULL;
    }
    rewind(fp); 

    
    char* buffer = (char*)malloc(file_size + 1);
    if (!buffer) {
        fprintf(stderr, "Shader Utils Error: malloc failed for shader source buffer (%ld bytes).\n", file_size + 1);
        fclose(fp);
        return NULL;
    }

    
    size_t read_size = fread(buffer, 1, file_size, fp);
    if ((long)read_size != file_size) { 
        fprintf(stderr, "Shader Utils Error: fread failed or read incomplete (%zu/%ld bytes) for file: %s\n", read_size, file_size, filename);
        free(buffer);
        fclose(fp);
        return NULL;
    }

    
    buffer[file_size] = '\0';

    fclose(fp);
    return buffer;
}


GLuint create_program_from_files(const char* vertex_shader_path, const char* fragment_shader_path) {
    printf("Shader Utils: Loading vertex shader: %s\n", vertex_shader_path);
    char* vertex_source = load_shader_source(vertex_shader_path);
    if (!vertex_source) {
        return 0; 
    }

    printf("Shader Utils: Loading fragment shader: %s\n", fragment_shader_path);
    char* fragment_source = load_shader_source(fragment_shader_path);
    if (!fragment_source) {
        free(vertex_source);
        return 0;
    }

    GLuint vertex_shader = compile_shader(GL_VERTEX_SHADER, vertex_source);
    GLuint fragment_shader = compile_shader(GL_FRAGMENT_SHADER, fragment_source);

    
    free(vertex_source);
    free(fragment_source);

    if (vertex_shader == 0 || fragment_shader == 0) {
        
        if (vertex_shader != 0) glDeleteShader(vertex_shader);
        if (fragment_shader != 0) glDeleteShader(fragment_shader);
        return 0;
    }

    GLuint program = glCreateProgram();
    if (program == 0) {
         fprintf(stderr, "Shader Error: glCreateProgram failed.\n");
         glDeleteShader(vertex_shader);
         glDeleteShader(fragment_shader);
         return 0;
    }

    glAttachShader(program, vertex_shader);
    glAttachShader(program, fragment_shader);
    glLinkProgram(program);

    
    glDeleteShader(vertex_shader);
    glDeleteShader(fragment_shader);

    GLint linked = 0;
    glGetProgramiv(program, GL_LINK_STATUS, &linked);
    if (!linked) {
        GLint infoLen = 0;
        glGetProgramiv(program, GL_INFO_LOG_LENGTH, &infoLen);
        if (infoLen > 1) {
            char* infoLog = malloc(infoLen);
             if (infoLog) {
                 glGetProgramInfoLog(program, infoLen, NULL, infoLog);
                 fprintf(stderr, "Shader Error: Failed to link program:\n%s\n", infoLog);
                 free(infoLog);
             }
        } else {
             fprintf(stderr, "Shader Error: Failed to link program (no info log).\n");
        }
        glDeleteProgram(program);
        return 0;
    }

    printf("Shader Utils: Program created successfully (ID: %u) from %s and %s\n", program, vertex_shader_path, fragment_shader_path);
    return program;
}
