
#include "starfield.h"
#include "./../shader_utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <EGL/egl.h>
#include <string.h>
#include <math.h>
#include <GLES3/gl3.h>

#define STARFIELD_VERTEX_SHADER "src/renderer/shaders/starfield.vert"
#define STARFIELD_FRAGMENT_SHADER "src/renderer/shaders/starfield.frag"

#define BASE_LAYER1_SPEED 0.01f
#define BASE_LAYER2_SPEED 0.05f
#define BASE_LAYER3_SPEED 0.1f

typedef struct {
    GLuint shader_program;
    double current_time_sec;

    
    GLint loc_uResolution;
    GLint loc_uTime;
    GLint loc_uSpeed;
    GLint loc_uDensity;
    GLint loc_uStarThreshold;
    GLint loc_uBrightness;
    GLint loc_uLayer1Speed;
    GLint loc_uLayer2Speed;
    GLint loc_uLayer3Speed;
    GLint loc_uNebulaBrightness;
    GLint loc_uNebulaDensity;
    GLint loc_uNebulaColor1;
    GLint loc_uNebulaColor2;
    GLint loc_uStarBrightness;
    GLint loc_uLensFieldDensity;
    GLint loc_uLensSizeMul;
    GLint loc_uLensGlowMul;
    GLint loc_uLensStrengthMul;
    
    GLint loc_uLensSpeedMul;
    GLint loc_uLensChance;

    
    float currentSpeed;
    float currentDensity;
    float currentStarThreshold;
    float currentBrightness;
    float currentLayer1Speed;
    float currentLayer2Speed;
    float currentLayer3Speed;
    float currentParallaxMul; 
    float currentNebulaBrightness;
    float currentNebulaDensity;
    float currentNebulaColor1[3];
    float currentNebulaColor2[3];
    float currentStarBrightness;
    float currentLensFieldDensity;
    float currentLensSizeMul;
    float currentLensGlowMul;
    float currentLensStrengthMul;
    
    float currentLensSpeedMul;
    float currentLensChance; 

} StarfieldModeState;

static void starfield_update_layer_speeds(StarfieldModeState* state) {
    if (!state) return;

    
    float master_speed_factor = state->currentSpeed;

    state->currentLayer1Speed = BASE_LAYER1_SPEED * state->currentParallaxMul * master_speed_factor;
    state->currentLayer2Speed = BASE_LAYER2_SPEED * state->currentParallaxMul * master_speed_factor;
    state->currentLayer3Speed = BASE_LAYER3_SPEED * state->currentParallaxMul * master_speed_factor;
}

static GLint get_uniform_location(GLuint program, const char* name) {
    GLint loc = glGetUniformLocation(program, name);
    if (loc == -1) {
        fprintf(stderr, "ModeStarfield Warning: Uniform '%s' not found or inactive.\n", name);
    }
    return loc;
}



static void* starfield_init(const char* arg, GLuint common_vbo) {
    (void)arg;
    (void)common_vbo;
    printf("ModeStarfield GLES3: Initializing...\n");

    StarfieldModeState* state = calloc(1, sizeof(StarfieldModeState));
    if (!state) {
        perror("ModeStarfield GLES3 calloc state failed");
        return NULL;
    }

    
    state->current_time_sec = 0.0;
    state->currentSpeed = 0.5f;
    state->currentDensity = 25.0f;
    state->currentStarThreshold = 1.9f;
    state->currentBrightness = 0.8f;
    state->currentParallaxMul = 1.0f;

    starfield_update_layer_speeds(state);

    state->currentStarBrightness = 20.0f;

    state->currentNebulaBrightness = 0.5f;
    state->currentNebulaDensity = 0.7f;
    state->currentNebulaColor1[0] = 0.8f; state->currentNebulaColor1[1] = 0.040f; state->currentNebulaColor1[2] = 0.2f;
    state->currentNebulaColor2[0] = 0.0f; state->currentNebulaColor2[1] = 0.3f; state->currentNebulaColor2[2] = 0.9f;
    
    state->currentLensFieldDensity = 3.5f;
    state->currentLensSizeMul = 1.0f;
    state->currentLensGlowMul = 0.5f;
    state->currentLensStrengthMul = 1.0f;
    
    
    state->currentLensSpeedMul = 1.0f; 

    state->currentLensChance = 0.03f; 

    
    state->shader_program = create_program_from_files(STARFIELD_VERTEX_SHADER, STARFIELD_FRAGMENT_SHADER);
    if (!state->shader_program) {
        fprintf(stderr, "ModeStarfield GLES3 Error: Failed to create shader program.\n");
        free(state);
        return NULL;
    }

    
    printf("ModeStarfield GLES3: Getting uniform locations...\n");
    
    state->loc_uResolution = get_uniform_location(state->shader_program, "uResolution");
    state->loc_uTime = get_uniform_location(state->shader_program, "uTime");
    state->loc_uSpeed = get_uniform_location(state->shader_program, "uSpeed");
    state->loc_uDensity = get_uniform_location(state->shader_program, "uDensity");
    state->loc_uStarThreshold = get_uniform_location(state->shader_program, "uStarThreshold");
    state->loc_uBrightness = get_uniform_location(state->shader_program, "uBrightness");
    state->loc_uLayer1Speed = get_uniform_location(state->shader_program, "uLayer1Speed");
    state->loc_uLayer2Speed = get_uniform_location(state->shader_program, "uLayer2Speed");
    state->loc_uLayer3Speed = get_uniform_location(state->shader_program, "uLayer3Speed");
    state->loc_uNebulaBrightness = get_uniform_location(state->shader_program, "uNebulaBrightness");
    state->loc_uNebulaDensity = get_uniform_location(state->shader_program, "uNebulaDensity");
    state->loc_uNebulaColor1 = get_uniform_location(state->shader_program, "uNebulaColor1");
    state->loc_uNebulaColor2 = get_uniform_location(state->shader_program, "uNebulaColor2");
    state->loc_uStarBrightness = get_uniform_location(state->shader_program, "uStarBrightness");
    state->loc_uLensFieldDensity = get_uniform_location(state->shader_program, "uLensFieldDensity");
    state->loc_uLensSizeMul = get_uniform_location(state->shader_program, "uLensSizeMul");
    state->loc_uLensGlowMul = get_uniform_location(state->shader_program, "uLensGlowMul");
    state->loc_uLensStrengthMul = get_uniform_location(state->shader_program, "uLensStrengthMul");
    state->loc_uLensChance = get_uniform_location(state->shader_program, "uLensChance");
    
    
    state->loc_uLensSpeedMul = get_uniform_location(state->shader_program, "uLensSpeedMul");

    printf("ModeStarfield GLES3: Initialized (Program ID: %u).\n", state->shader_program);
    return state;
}


static void starfield_cleanup(void* mode_state) { /* ... (код без изменений) ... */ 
    StarfieldModeState* state = (StarfieldModeState*)mode_state;
    if (!state) return;
    printf("ModeStarfield GLES3: Cleaning up...\n");
    if (state->shader_program) {
        glDeleteProgram(state->shader_program);
        state->shader_program = 0;
    }
    free(state);
}


static bool starfield_render(void* mode_state, const RenderParams* params) {
    StarfieldModeState* state = (StarfieldModeState*)mode_state;
    if (!state || !state->shader_program || !params) {
        fprintf(stderr, "ModeStarfield GLES3 Error: Invalid state/program/params in render!\n");
        return false;
    }

    state->current_time_sec += params->time_delta_sec;

    glUseProgram(state->shader_program);

    
    
    if (state->loc_uResolution != -1) glUniform2f(state->loc_uResolution, (GLfloat)params->physical_width, (GLfloat)params->physical_height);
    if (state->loc_uTime != -1) glUniform1f(state->loc_uTime, (GLfloat)state->current_time_sec);
    if (state->loc_uSpeed != -1) glUniform1f(state->loc_uSpeed, state->currentSpeed);
    if (state->loc_uDensity != -1) glUniform1f(state->loc_uDensity, state->currentDensity);
    if (state->loc_uStarThreshold != -1) glUniform1f(state->loc_uStarThreshold, state->currentStarThreshold);
    if (state->loc_uBrightness != -1) glUniform1f(state->loc_uBrightness, state->currentBrightness);
    if (state->loc_uLayer1Speed != -1) glUniform1f(state->loc_uLayer1Speed, state->currentLayer1Speed);
    if (state->loc_uLayer2Speed != -1) glUniform1f(state->loc_uLayer2Speed, state->currentLayer2Speed);
    if (state->loc_uLayer3Speed != -1) glUniform1f(state->loc_uLayer3Speed, state->currentLayer3Speed);
    if (state->loc_uNebulaBrightness != -1) glUniform1f(state->loc_uNebulaBrightness, state->currentNebulaBrightness);
    if (state->loc_uNebulaDensity != -1) glUniform1f(state->loc_uNebulaDensity, state->currentNebulaDensity);
    if (state->loc_uNebulaColor1 != -1) glUniform3fv(state->loc_uNebulaColor1, 1, state->currentNebulaColor1);
    if (state->loc_uNebulaColor2 != -1) glUniform3fv(state->loc_uNebulaColor2, 1, state->currentNebulaColor2);
    if (state->loc_uStarBrightness != -1) glUniform1f(state->loc_uStarBrightness, state->currentStarBrightness);
    if (state->loc_uLensFieldDensity != -1) glUniform1f(state->loc_uLensFieldDensity, state->currentLensFieldDensity);
    if (state->loc_uLensSizeMul != -1) glUniform1f(state->loc_uLensSizeMul, state->currentLensSizeMul);
    if (state->loc_uLensGlowMul != -1) glUniform1f(state->loc_uLensGlowMul, state->currentLensGlowMul);
    if (state->loc_uLensStrengthMul != -1) glUniform1f(state->loc_uLensStrengthMul, state->currentLensStrengthMul);

    
    if (state->loc_uLensSpeedMul != -1) glUniform1f(state->loc_uLensSpeedMul, state->currentLensSpeedMul);
    if (state->loc_uLensChance != -1) glUniform1f(state->loc_uLensChance, state->currentLensChance);


    
    glBindBuffer(GL_ARRAY_BUFFER, params->common_vbo);

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

    glDrawArrays(GL_TRIANGLES, 0, 6);

    if (pos_loc >= 0) glDisableVertexAttribArray(pos_loc);
    if (tex_loc >= 0) glDisableVertexAttribArray(tex_loc);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);

    return true;
}


static bool starfield_handle_command(void* mode_state, const char* command) {
    StarfieldModeState* state = (StarfieldModeState*)mode_state;
    if (!state || !command) return false;

    float v1, v2, v3;

    
    if (strcmp(command, "faster") == 0) {
 state->currentSpeed *= 1.25f; if (state->currentSpeed > 10.0f) state->currentSpeed = 10.0f;
 printf("ModeStarfield: Set speed=%.2f\n", state->currentSpeed); 
 starfield_update_layer_speeds(state); 
 return true;
 } else if (strcmp(command, "slower") == 0) {
 state->currentSpeed /= 1.25f; if (state->currentSpeed < 0.05f) state->currentSpeed = 0.05f;
 printf("ModeStarfield: Set speed=%.2f\n", state->currentSpeed);
 starfield_update_layer_speeds(state); 
 return true;
 } else if (sscanf(command, "set_speed %f", &v1) == 1) {
state->currentSpeed = v1 > 0.0f ? v1 : 0.05f;
printf("ModeStarfield: Set speed=%.2f\n", state->currentSpeed);
starfield_update_layer_speeds(state); 
return true;
 }else if (sscanf(command, "set_density %f", &v1) == 1) {
        state->currentDensity = v1 > 1.0f ? v1 : 1.0f;
        printf("ModeStarfield: Set density=%.2f\n", state->currentDensity); return true;
    } else if (sscanf(command, "set_brightness %f", &v1) == 1) {
        state->currentBrightness = v1 >= 0.0f ? v1 : 0.0f;
        printf("ModeStarfield: Set brightness=%.2f\n", state->currentBrightness); return true;
    } else if (sscanf(command, "set_star_count %f", &v1) == 1) {
        if (v1 < 0.0f) v1 = 0.0f; 
        if (v1 > 1.0f) v1 = 1.0f;
        state->currentStarThreshold = 1.0f - v1;
        printf("ModeStarfield: Set star count=%.2f%% (threshold=%.2f)\n", v1 * 100.0f, state->currentStarThreshold); return true;
    } else if (sscanf(command, "set_parallax_mul %f", &v1) == 1) {
        state->currentParallaxMul = v1 >= 0.0f ? v1 : 0.0f;
        starfield_update_layer_speeds(state);
        printf("ModeStarfield: Set parallax multiplier=%.2f\n", state->currentParallaxMul); return true;
    } else if (sscanf(command, "set_nebula_brightness %f", &v1) == 1) {
        state->currentNebulaBrightness = v1 >= 0.0f ? v1 : 0.0f;
        printf("ModeStarfield: Set nebula brightness=%.2f\n", state->currentNebulaBrightness); return true;
    } else if (sscanf(command, "set_nebula_density %f", &v1) == 1) {
        state->currentNebulaDensity = v1 > 0.1f ? v1 : 0.1f;
        printf("ModeStarfield: Set nebula density=%.2f\n", state->currentNebulaDensity); return true;
    } else if (sscanf(command, "set_nebula_color1 %f %f %f", &v1, &v2, &v3) == 3) {
        state->currentNebulaColor1[0] = v1; state->currentNebulaColor1[1] = v2; state->currentNebulaColor1[2] = v3;
        printf("ModeStarfield: Set nebula color1=(%.2f, %.2f, %.2f)\n", v1, v2, v3); return true;
    } else if (sscanf(command, "set_nebula_color2 %f %f %f", &v1, &v2, &v3) == 3) {
        state->currentNebulaColor2[0] = v1; state->currentNebulaColor2[1] = v2; state->currentNebulaColor2[2] = v3;
        printf("ModeStarfield: Set nebula color2=(%.2f, %.2f, %.2f)\n", v1, v2, v3); return true;
    } else if (sscanf(command, "set_star_brightness %f", &v1) == 1) {
        state->currentStarBrightness = v1 >= 0.0f ? v1 : 0.0f;
        printf("ModeStarfield: Set star brightness=%.2f\n", state->currentStarBrightness); return true;
    
    
    } else if (sscanf(command, "set_lens_density %f", &v1) == 1) {
        state->currentLensFieldDensity = v1 > 0.0f ? v1 : 0.1f;
        printf("ModeStarfield: Set lens field density=%.2f\n", state->currentLensFieldDensity); 
        return true;
    } else if (sscanf(command, "set_lens_size %f", &v1) == 1) {
        state->currentLensSizeMul = v1 >= 0.0f ? v1 : 0.0f;
        printf("ModeStarfield: Set lens size multiplier=%.2f\n", state->currentLensSizeMul); 
        return true;
    } else if (sscanf(command, "set_lens_glow %f", &v1) == 1) {
        state->currentLensGlowMul = v1 >= 0.0f ? v1 : 0.0f;
        printf("ModeStarfield: Set lens glow multiplier=%.2f\n", state->currentLensGlowMul); 
        return true;
    } else if (sscanf(command, "set_lens_strength %f", &v1) == 1) {
        state->currentLensStrengthMul = v1 >= 0.0f ? v1 : 0.0f;
        printf("ModeStarfield: Set lens strength multiplier=%.2f\n", state->currentLensStrengthMul); 
        return true;
        
    
    } else if (sscanf(command, "set_lens_speed_mul %f", &v1) == 1) {
        state->currentLensSpeedMul = v1 >= 0.0f ? v1 : 0.0f;
        printf("ModeStarfield: Set lens speed multiplier=%.2f\n", state->currentLensSpeedMul);
        return true;
    } else if (sscanf(command, "set_lens_chance %f", &v1) == 1) {
        if (v1 < 0.0f) v1 = 0.0f;
        if (v1 > 1.0f) v1 = 1.0f; 
        state->currentLensChance = v1;
        printf("ModeStarfield: Set lens chance=%.2f (%.0f%%)\n", state->currentLensChance, state->currentLensChance * 100.0f);
        return true;
    }

    return false;
}



static void starfield_resize(void* mode_state, int physical_width, int physical_height) {
    (void)mode_state; (void)physical_width; (void)physical_height;
}


static bool starfield_needs_redraw(void* mode_state) {
    (void)mode_state;
    return true; 
}


const RenderModeInterface starfield_mode_interface = {
    .init = starfield_init,
    .cleanup = starfield_cleanup,
    .render = starfield_render,
    .handle_command = starfield_handle_command,
    .resize = starfield_resize,
    .needs_redraw = starfield_needs_redraw,
};
