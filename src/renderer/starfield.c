// src/renderer/starfield.c
#include "../common.h"

#include "starfield.h"
#include "./../shader_utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <EGL/egl.h>
#include <string.h>
#include <math.h>
#include <GLES3/gl3.h>
#include <time.h>

// ... (все #define) ...
#define STARFIELD_STARS_VERTEX_SHADER "src/renderer/shaders/stars.vert"
#define STARFIELD_STARS_FRAGMENT_SHADER "src/renderer/shaders/stars.frag"
#define NUM_STARS 50000
#define UNIVERSE_SIZE 100.0f
#define STARFIELD_VERTEX_SHADER "src/renderer/shaders/starfield.vert"
#define STARFIELD_FRAGMENT_SHADER "src/renderer/shaders/starfield.frag"
#define STARFIELD_UPDATE_VERTEX_SHADER "src/renderer/shaders/starfield.vert" 
#define STARFIELD_UPDATE_FRAGMENT_SHADER "src/renderer/shaders/nebula_update.frag"
#define MAX_LENSES 10


// ... (struct DynamicLens) ...
typedef struct {
    float pos[3];
    float radius;
    float strength;
    float depth_range;
    float age;
    float total_life;
} DynamicLens;

// ... (struct StarfieldModeState) ...
typedef struct {
    GLuint shader_program;
    GLuint shader_program_stars;
    GLuint shader_program_nebula_update;
    double current_time_sec;
    GLuint star_vbo;
    size_t num_stars;
    GLuint fbos[2];
    GLuint fbo_textures[2];
    GLsizei fbo_width;
    GLsizei fbo_height;
    bool ping_pong_state;
    DynamicLens active_lenses[MAX_LENSES];
    int active_lens_count;
    float lens_spawn_timer;
    float lens_spawn_frequency;
    float lens_velocity[3];
    float lens_base_radius;
    float lens_base_strength;
    float lens_base_ttl;
    float lens_fade_in_time;
    float lens_fade_out_time;
    GLint loc_env_uResolution;
    GLint loc_env_uTime;
    GLint loc_env_uNebulaBrightness;
    GLint loc_env_uNebulaDensity;
    GLint loc_env_uNebulaColor1;
    GLint loc_env_uNebulaColor2;
    GLint loc_env_uCameraPos;
    GLint loc_env_uLensPos;
    GLint loc_env_uLensRadius;
    GLint loc_env_uLensStrength;
    GLint loc_env_uLensDepthRange;
    GLint loc_env_uActiveLensCount;
    GLint loc_env_uDynamicNebula;
    GLint loc_stars_uProjectionMatrix;
    GLint loc_stars_uCameraPos;
    GLint loc_stars_uResolution;
    GLint loc_stars_uCameraSpeed;
    GLint loc_stars_uUniverseSize;
    GLint loc_stars_uDissolveStart;
    GLint loc_stars_uDissolveEnd;
    GLint loc_stars_uNebulaDissolveColor;
    GLint loc_stars_uGlobalFade;
    GLint loc_stars_uRenderPass_Alive;
    GLint loc_stars_uTailProbability;
    GLint loc_stars_uLensPos;
    GLint loc_stars_uLensRadius;
    GLint loc_stars_uLensStrength;
    GLint loc_stars_uLensDepthRange;
    GLint loc_stars_uActiveLensCount;
    GLint loc_stars_aBaseSize;
    GLint loc_stars_aBaseBrightness;
    GLint loc_stars_uStarBaseSize;
    GLint loc_stars_uStarBaseBrightness;
    GLint loc_stars_uNebulaColor1;
    GLint loc_stars_uNebulaColor2;
    GLint loc_update_uPreviousNebula;
    GLint loc_update_uFadeFactor;
    float currentSpeed;
    float currentNebulaBrightness;
    float currentNebulaDensity;
    float currentNebulaColor1[3];
    float currentNebulaColor2[3];
    float currentStarPos[3]; 
    float currentStarVelocity[3];
    float currentGlobalFade;
    float currentStarBaseSize;
    float currentStarBaseBrightness;
    float currentTailProbability;
} StarfieldModeState;

static float rand_float() {
    return (float)rand() / (float)RAND_MAX;
}
static float rand_float_range(float min, float max) {
    return min + (rand_float() * (max - min));
}

// [НОВОЕ] C-эквивалент GLSL smoothstep()
static float smoothstep_c(float edge0, float edge1, float x) {
    // Clamp
    float t = (x - edge0) / (edge1 - edge0);
    t = fmax(0.0f, fmin(1.0f, t));
    // Hermite interpolation
    return t * t * (3.0f - 2.0f * t);
}


// ... (starfield_resize_fbos, starfield_rebuild_stars, create_projection_matrix, get_uniform_location, starfield_update_lenses... БЕЗ ИЗМЕНЕНИЙ) ...
static bool starfield_resize_fbos(StarfieldModeState* state, GLsizei width, GLsizei height) {
    if (!state) return false;
    if (state->fbo_width == width && state->fbo_height == height) {
        return true;
    }
    log_debug("ModeStarfield GLES3: Resizing FBOs to %dx%d\n", width, height);
    if (state->fbos[0]) glDeleteFramebuffers(2, state->fbos);
    if (state->fbo_textures[0]) glDeleteTextures(2, state->fbo_textures);
    glGenFramebuffers(2, state->fbos);
    glGenTextures(2, state->fbo_textures);
    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, state->fbo_textures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_MIRRORED_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_MIRRORED_REPEAT);
        glBindFramebuffer(GL_FRAMEBUFFER, state->fbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, state->fbo_textures[i], 0);
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            fprintf(stderr, "ModeStarfield GLES3 Error: FBO %d is not complete! (Status: 0x%x)\n", i, status);
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return false;
        }
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    state->fbo_width = width;
    state->fbo_height = height;
    for (int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, state->fbos[i]);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    log_debug("ModeStarfield GLES3: FBOs created and sized.\n");
    return true;
}
static bool starfield_rebuild_stars(StarfieldModeState* state, size_t new_num_stars) {
    if (!state) return false;
    if (state->star_vbo) {
        glDeleteBuffers(1, &state->star_vbo);
        state->star_vbo = 0;
    }
    state->num_stars = new_num_stars;
    const size_t floats_per_star = 5;
    const size_t total_floats = state->num_stars * floats_per_star;
    float* star_data = malloc(total_floats * sizeof(float));
    if (!star_data) {
        perror("Failed to allocate memory for stars");
        state->num_stars = 0;
        return false;
    }
    log_debug("ModeStarfield GLES3: Generating %zu stars...\n", state->num_stars);
    for (size_t i = 0; i < state->num_stars; i++) {
        star_data[i * floats_per_star + 0] = (rand_float() - 0.5f) * UNIVERSE_SIZE;
        star_data[i * floats_per_star + 1] = (rand_float() - 0.5f) * UNIVERSE_SIZE;
        star_data[i * floats_per_star + 2] = (rand_float() - 0.5f) * UNIVERSE_SIZE;
        star_data[i * floats_per_star + 3] = rand_float() * 0.5f + 0.5f; 
        star_data[i * floats_per_star + 4] = rand_float() * 0.7f + 0.3f;
    }
    glGenBuffers(1, &state->star_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, state->star_vbo);
    glBufferData(GL_ARRAY_BUFFER, total_floats * sizeof(float), star_data, GL_STATIC_DRAW);
    free(star_data);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    log_debug("ModeStarfield GLES3: Star VBO created (ID: %u) with %zu stars.\n", state->star_vbo, state->num_stars);
    return true;
}
static void create_projection_matrix(float fov_y_deg, float aspect, float z_near, float z_far, float* out_mat) {
    float f = 1.0f / tanf((fov_y_deg * 3.14159265f / 180.0f) / 2.0f);
    memset(out_mat, 0, 16 * sizeof(float));
    out_mat[0] = f / aspect;
    out_mat[5] = f;
    out_mat[10] = (z_far + z_near) / (z_near - z_far);
    out_mat[11] = -1.0f;
    out_mat[14] = (2.0f * z_far * z_near) / (z_near - z_far);
}
static GLint get_uniform_location(GLuint program, const char* name) {
    GLint loc = glGetUniformLocation(program, name);
    if (loc == -1) {
        fprintf(stderr, "ModeStarfield Warning: Uniform '%s' not found or inactive.\n", name);
    }
    return loc;
}
static void starfield_update_lenses(StarfieldModeState* state, float delta_sec_f) {
    for (int i = 0; i < state->active_lens_count; ) {
        DynamicLens* lens = &state->active_lenses[i];
        lens->age += delta_sec_f;
        if (lens->age >= lens->total_life) {
            state->active_lenses[i] = state->active_lenses[state->active_lens_count - 1];
            state->active_lens_count--;
        } else {
            lens->pos[0] += state->lens_velocity[0] * delta_sec_f;
            lens->pos[1] += state->lens_velocity[1] * delta_sec_f;
            lens->pos[2] += state->lens_velocity[2] * delta_sec_f;
            i++;
        }
    }
    if (state->lens_spawn_frequency <= 0.0f) return;
    state->lens_spawn_timer += delta_sec_f;
    float time_between_spawns = 1.0f / state->lens_spawn_frequency;
    if (state->lens_spawn_timer >= time_between_spawns) {
        state->lens_spawn_timer -= time_between_spawns; 
        if (state->active_lens_count < MAX_LENSES) {
            DynamicLens* new_lens = &state->active_lenses[state->active_lens_count];
            new_lens->pos[0] = rand_float_range(-30.0f, 30.0f);
            new_lens->pos[1] = rand_float_range(-30.0f, 30.0f);
            new_lens->pos[2] = state->currentStarPos[2] + (UNIVERSE_SIZE / 2.0f) - 10.0f; 
            new_lens->radius = rand_float_range(state->lens_base_radius * 0.5f, state->lens_base_radius * 1.5f);
            new_lens->strength = rand_float_range(state->lens_base_strength * 0.5f, state->lens_base_strength * 1.5f);
            new_lens->depth_range = 10.0f;
            new_lens->total_life = rand_float_range(state->lens_base_ttl * 0.7f, state->lens_base_ttl * 1.3f);
            new_lens->age = 0.0f;
            state->active_lens_count++;
        }
    }
}


// ... (starfield_init БЕЗ ИЗМЕНЕНИЙ) ...
static void* starfield_init(const char* arg, GLuint common_vbo) {
    (void)arg;
    (void)common_vbo;
    log_debug("ModeStarfield GLES3: Initializing...\n");
    srand(time(NULL));
    StarfieldModeState* state = calloc(1, sizeof(StarfieldModeState));
    if (!state) return NULL;
    state->currentSpeed = 1.10f; 
    state->currentStarPos[0] = 0.0f; state->currentStarPos[1] = 0.0f; state->currentStarPos[2] = 0.0f;
    state->currentStarVelocity[0] = 0.0f; 
    state->currentStarVelocity[1] = 0.0f; 
    state->currentStarVelocity[2] = 4.0f;
    state->currentGlobalFade = 0.0f;
    state->currentNebulaBrightness = 0.8f;
    state->currentNebulaDensity = 0.003f;
    state->currentNebulaColor1[0] = 0.0f; state->currentNebulaColor1[1] = 0.10f; state->currentNebulaColor1[2] = 0.1f;
    state->currentNebulaColor2[0] = 0.1f; state->currentNebulaColor2[1] = 0.0f; state->currentNebulaColor2[2] = 0.3f;
    state->currentStarBaseSize = 1.0f;
    state->currentStarBaseBrightness = 1.0f;
    state->currentTailProbability = 0.10f; 
    state->active_lens_count = 0;
    state->lens_spawn_timer = 0.0f;
    state->lens_spawn_frequency = 0.1f;
    state->lens_velocity[0] = 0.0f;
    state->lens_velocity[1] = 0.0f;
    state->lens_velocity[2] = -2.0f;
    state->lens_base_radius = 40.0f;
    state->lens_base_strength = 2.5f;
    state->lens_base_ttl = 40.0f;
    state->lens_fade_in_time = 20.0f;
    state->lens_fade_out_time = 200.0f;
    state->fbo_width = -1;
    state->fbo_height = -1;
    state->ping_pong_state = false;
    state->shader_program = create_program_from_files(STARFIELD_VERTEX_SHADER, STARFIELD_FRAGMENT_SHADER);
    if (!state->shader_program) { free(state); return NULL; }
    log_debug("ModeStarfield GLES3: Getting uniforms for ENV (Pass 3) shader...\n");
    state->loc_env_uResolution = get_uniform_location(state->shader_program, "uResolution");
    state->loc_env_uTime = get_uniform_location(state->shader_program, "uTime");
    state->loc_env_uNebulaBrightness = get_uniform_location(state->shader_program, "uNebulaBrightness");
    state->loc_env_uNebulaDensity = get_uniform_location(state->shader_program, "uNebulaDensity");
    state->loc_env_uNebulaColor1 = get_uniform_location(state->shader_program, "uNebulaColor1");
    state->loc_env_uNebulaColor2 = get_uniform_location(state->shader_program, "uNebulaColor2");
    state->loc_env_uCameraPos = get_uniform_location(state->shader_program, "uCameraPos");
    state->loc_env_uDynamicNebula = get_uniform_location(state->shader_program, "uDynamicNebula");
    state->loc_env_uLensPos = get_uniform_location(state->shader_program, "uLensPos[0]");
    state->loc_env_uLensRadius = get_uniform_location(state->shader_program, "uLensRadius[0]");
    state->loc_env_uLensStrength = get_uniform_location(state->shader_program, "uLensStrength[0]");
    state->loc_env_uLensDepthRange = get_uniform_location(state->shader_program, "uLensDepthRange[0]");
    state->loc_env_uActiveLensCount = get_uniform_location(state->shader_program, "uActiveLensCount");
    state->shader_program_stars = create_program_from_files(STARFIELD_STARS_VERTEX_SHADER, STARFIELD_STARS_FRAGMENT_SHADER);
    if (!state->shader_program_stars) { glDeleteProgram(state->shader_program); free(state); return NULL; }
    log_debug("ModeStarfield GLES3: Getting uniforms for STARS (Pass 2/4) shader...\n");
    state->loc_stars_uProjectionMatrix = get_uniform_location(state->shader_program_stars, "uProjectionMatrix");
    state->loc_stars_uCameraPos = get_uniform_location(state->shader_program_stars, "uCameraPos");
    state->loc_stars_uResolution = get_uniform_location(state->shader_program_stars, "uResolution");
    state->loc_stars_uCameraSpeed = get_uniform_location(state->shader_program_stars, "uCameraSpeed");
    state->loc_stars_uUniverseSize = get_uniform_location(state->shader_program_stars, "uUniverseSize");
    state->loc_stars_uDissolveStart = get_uniform_location(state->shader_program_stars, "uDissolveStart");
    state->loc_stars_uDissolveEnd = get_uniform_location(state->shader_program_stars, "uDissolveEnd");
    state->loc_stars_uNebulaDissolveColor = get_uniform_location(state->shader_program_stars, "uNebulaDissolveColor");
    state->loc_stars_uGlobalFade = get_uniform_location(state->shader_program_stars, "uGlobalFade");
    state->loc_stars_uRenderPass_Alive = get_uniform_location(state->shader_program_stars, "uRenderPass_Alive");
    state->loc_stars_uTailProbability = get_uniform_location(state->shader_program_stars, "uTailProbability");
    state->loc_stars_uLensPos = get_uniform_location(state->shader_program_stars, "uLensPos[0]");
    state->loc_stars_uLensRadius = get_uniform_location(state->shader_program_stars, "uLensRadius[0]");
    state->loc_stars_uLensStrength = get_uniform_location(state->shader_program_stars, "uLensStrength[0]");
    state->loc_stars_uLensDepthRange = get_uniform_location(state->shader_program_stars, "uLensDepthRange[0]");
    state->loc_stars_uActiveLensCount = get_uniform_location(state->shader_program_stars, "uActiveLensCount");
    state->loc_stars_uStarBaseSize = get_uniform_location(state->shader_program_stars, "uStarBaseSize");
    state->loc_stars_uStarBaseBrightness = get_uniform_location(state->shader_program_stars, "uStarBaseBrightness");
    state->loc_stars_aBaseSize = glGetAttribLocation(state->shader_program_stars, "aBaseSize");
    state->loc_stars_aBaseBrightness = glGetAttribLocation(state->shader_program_stars, "aBaseBrightness");
    if (state->loc_stars_aBaseSize < 0) fprintf(stderr, "ModeStarfield Warning: Attribute 'aBaseSize' not found.\n");
    if (state->loc_stars_aBaseBrightness < 0) fprintf(stderr, "ModeStarfield Warning: Attribute 'aBaseBrightness' not found.\n");
    state->loc_stars_uNebulaColor1 = get_uniform_location(state->shader_program_stars, "uNebulaColor1");
    state->loc_stars_uNebulaColor2 = get_uniform_location(state->shader_program_stars, "uNebulaColor2");
    state->shader_program_nebula_update = create_program_from_files(STARFIELD_UPDATE_VERTEX_SHADER, STARFIELD_UPDATE_FRAGMENT_SHADER);
    if (!state->shader_program_nebula_update) { glDeleteProgram(state->shader_program); glDeleteProgram(state->shader_program_stars); free(state); return NULL; }
    log_debug("ModeStarfield GLES3: Getting uniforms for UPDATE (Pass 1) shader...\n");
    state->loc_update_uPreviousNebula = get_uniform_location(state->shader_program_nebula_update, "uPreviousNebula");
    state->loc_update_uFadeFactor = get_uniform_location(state->shader_program_nebula_update, "uFadeFactor");
    if (!starfield_rebuild_stars(state, NUM_STARS)) {
        glDeleteProgram(state->shader_program);
        glDeleteProgram(state->shader_program_stars);
        glDeleteProgram(state->shader_program_nebula_update);
        free(state);
        return NULL;
    }
    log_debug("ModeStarfield GLES3: Initialized.\n");
    return state;
}

// ... (starfield_cleanup БЕЗ ИЗМЕНЕНИЙ) ...
static void starfield_cleanup(void* mode_state) {
    StarfieldModeState* state = (StarfieldModeState*)mode_state;
    if (!state) return;
    log_debug("ModeStarfield GLES3: Cleaning up...\n");
    if (state->shader_program) glDeleteProgram(state->shader_program);
    if (state->shader_program_stars) glDeleteProgram(state->shader_program_stars);
    if (state->shader_program_nebula_update) glDeleteProgram(state->shader_program_nebula_update);
    if (state->star_vbo) glDeleteBuffers(1, &state->star_vbo);
    if (state->fbos[0]) glDeleteFramebuffers(2, state->fbos);
    if (state->fbo_textures[0]) glDeleteTextures(2, state->fbo_textures);
    memset(state, 0, sizeof(StarfieldModeState));
    free(state);
}


// --- [ИЗМЕНЕНО] Хелпер для отправки данных о линзах в GPU ---
static void starfield_upload_lens_data(StarfieldModeState* state, GLint locPos, GLint locRad, GLint locStr, GLint locDepth, GLint locCount) {
    if (state->active_lens_count == 0) {
        glUniform1i(locCount, 0);
        return;
    }

    float lens_pos_data[MAX_LENSES * 3];
    float lens_radius_data[MAX_LENSES];
    float lens_strength_data[MAX_LENSES];
    float lens_depth_data[MAX_LENSES];

    for (int i = 0; i < state->active_lens_count; i++) {
        DynamicLens* lens = &state->active_lenses[i];
        lens_pos_data[i*3 + 0] = lens->pos[0];
        lens_pos_data[i*3 + 1] = lens->pos[1];
        lens_pos_data[i*3 + 2] = lens->pos[2];
        
        // --- [НОВАЯ ЛОГИКА FADE] ---
        
        // 1. Ramp-up (0 -> 1)
        float fade_in_ramp = lens->age / state->lens_fade_in_time;
        
        // 2. Ramp-down (1 -> 0)
        float time_remaining = lens->total_life - lens->age;
        float fade_out_ramp = time_remaining / state->lens_fade_out_time;

        // 3. Выбираем наименьший ramp и зажимаем на 1.0
        float life_fade = fmin(fmin(fade_in_ramp, fade_out_ramp), 1.0f);
        life_fade = fmax(life_fade, 0.0f);

        // --- [НОВОЕ] 4. Proximity Fade (Затухание по Z-координате) ---
        // Рассчитываем Z-позицию линзы *относительно* камеры
        float z_relative = lens->pos[2] - state->currentStarPos[2];
        
        // Начинаем затухание за 5 юнитов до камеры,
        // полностью выключаем за 5 юнитов позади камеры.
        // Используем наш C-эквивалент smoothstep(edge0, edge1, x)
        float proximity_fade = smoothstep_c(-5.0f, 5.0f, z_relative);

        // --- 5. Final Fade ---
        // Итоговый fade - это *минимум* из двух.
        float final_fade = fmin(life_fade, proximity_fade);
        
        lens_radius_data[i] = lens->radius * final_fade;
        lens_strength_data[i] = lens->strength * final_fade;
        lens_depth_data[i] = lens->depth_range;
    }

    glUniform1i(locCount, state->active_lens_count);
    glUniform3fv(locPos, state->active_lens_count, lens_pos_data);
    glUniform1fv(locRad, state->active_lens_count, lens_radius_data);
    glUniform1fv(locStr, state->active_lens_count, lens_strength_data);
    glUniform1fv(locDepth, state->active_lens_count, lens_depth_data);
}


// ... (starfield_render БЕЗ ИЗМЕНЕНИЙ) ...
// (Он просто вызывает обновленный starfield_upload_lens_data)
static bool starfield_render(void* mode_state, const RenderParams* params) {
    StarfieldModeState* state = (StarfieldModeState*)mode_state;
    if (!state || !state->shader_program || !state->shader_program_stars || !params || state->fbo_width <= 0 || state->num_stars == 0) {
        return false;
    }
    float delta_sec_f = (float)params->time_delta_sec;
    state->current_time_sec += delta_sec_f;
    state->currentStarPos[0] += state->currentStarVelocity[0] * state->currentSpeed * delta_sec_f;
    state->currentStarPos[1] += state->currentStarVelocity[1] * state->currentSpeed * delta_sec_f;
    state->currentStarPos[2] += state->currentStarVelocity[2] * state->currentSpeed * delta_sec_f;
    starfield_update_lenses(state, delta_sec_f);
    if (state->currentGlobalFade < 1.0f) {
        state->currentGlobalFade += delta_sec_f * 0.5f; 
        if (state->currentGlobalFade > 1.0f) state->currentGlobalFade = 1.0f;
    }
    float aspect = (float)params->physical_width / (float)params->physical_height;
    GLuint fbo_write = state->fbos[state->ping_pong_state ? 0 : 1];
    GLuint tex_read = state->fbo_textures[state->ping_pong_state ? 1 : 0];
    GLuint tex_write = state->fbo_textures[state->ping_pong_state ? 0 : 1];
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_write);
    glViewport(0, 0, state->fbo_width, state->fbo_height);
    glUseProgram(state->shader_program_nebula_update);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_read);
    glUniform1i(state->loc_update_uPreviousNebula, 0);
    glUniform1f(state->loc_update_uFadeFactor, 0.99f); 
    glBindBuffer(GL_ARRAY_BUFFER, params->common_vbo);
    GLint pos_loc_up = glGetAttribLocation(state->shader_program_nebula_update, "aPosition");
    GLint tex_loc_up = glGetAttribLocation(state->shader_program_nebula_update, "aTexCoord");
    const GLsizei common_stride = 4 * sizeof(GLfloat);
    if (pos_loc_up >= 0) {
        glVertexAttribPointer(pos_loc_up, 2, GL_FLOAT, GL_FALSE, common_stride, (void*)0);
        glEnableVertexAttribArray(pos_loc_up);
    }
    if (tex_loc_up >= 0) {
        glVertexAttribPointer(tex_loc_up, 2, GL_FLOAT, GL_FALSE, common_stride, (void*)(2 * sizeof(GLfloat)));
        glEnableVertexAttribArray(tex_loc_up);
    }
    glDrawArrays(GL_TRIANGLES, 0, 6);
    if (pos_loc_up >= 0) glDisableVertexAttribArray(pos_loc_up);
    if (tex_loc_up >= 0) glDisableVertexAttribArray(tex_loc_up);
    glUseProgram(state->shader_program_stars);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    float proj_matrix[16];
    create_projection_matrix(70.0f, aspect, 0.1f, UNIVERSE_SIZE, proj_matrix);
    float speed_magnitude = sqrtf(
        state->currentStarVelocity[0] * state->currentStarVelocity[0] +
        state->currentStarVelocity[1] * state->currentStarVelocity[1] +
        state->currentStarVelocity[2] * state->currentStarVelocity[2]
    );
    glUniformMatrix4fv(state->loc_stars_uProjectionMatrix, 1, GL_FALSE, proj_matrix);
    glUniform3fv(state->loc_stars_uCameraPos, 1, state->currentStarPos);
    glUniform2f(state->loc_stars_uResolution, (GLfloat)params->physical_width, (GLfloat)params->physical_height);
    glUniform1f(state->loc_stars_uCameraSpeed, speed_magnitude * state->currentSpeed);
    glUniform1f(state->loc_stars_uUniverseSize, UNIVERSE_SIZE);
    glUniform1f(state->loc_stars_uDissolveStart, 40.0f);
    glUniform1f(state->loc_stars_uDissolveEnd, 50.0f);
    glUniform3fv(state->loc_stars_uNebulaDissolveColor, 1, state->currentNebulaColor1);
    starfield_upload_lens_data(state, 
        state->loc_stars_uLensPos, state->loc_stars_uLensRadius, 
        state->loc_stars_uLensStrength, state->loc_stars_uLensDepthRange, 
        state->loc_stars_uActiveLensCount);
    glUniform1f(state->loc_stars_uStarBaseSize, state->currentStarBaseSize);
    glUniform1f(state->loc_stars_uStarBaseBrightness, state->currentStarBaseBrightness);
    glUniform1f(state->loc_stars_uTailProbability, state->currentTailProbability); 
    glUniform3fv(state->loc_stars_uNebulaColor1, 1, state->currentNebulaColor1);
    glUniform3fv(state->loc_stars_uNebulaColor2, 1, state->currentNebulaColor2);
    glUniform1i(state->loc_stars_uRenderPass_Alive, 0);
    glBindBuffer(GL_ARRAY_BUFFER, state->star_vbo);
    const GLsizei star_stride = 5 * sizeof(GLfloat);
    GLint star_pos_loc = glGetAttribLocation(state->shader_program_stars, "aPosition");
    if (star_pos_loc >= 0) {
        glVertexAttribPointer(star_pos_loc, 3, GL_FLOAT, GL_FALSE, star_stride, (void*)0);
        glEnableVertexAttribArray(star_pos_loc);
    }
    if (state->loc_stars_aBaseSize >= 0) {
        glVertexAttribPointer(state->loc_stars_aBaseSize, 1, GL_FLOAT, GL_FALSE, star_stride, (void*)(3 * sizeof(GLfloat)));
        glEnableVertexAttribArray(state->loc_stars_aBaseSize);
    }
    if (state->loc_stars_aBaseBrightness >= 0) {
        glVertexAttribPointer(state->loc_stars_aBaseBrightness, 1, GL_FLOAT, GL_FALSE, star_stride, (void*)(4 * sizeof(GLfloat)));
        glEnableVertexAttribArray(state->loc_stars_aBaseBrightness);
    }
    glDrawArrays(GL_POINTS, 0, (GLsizei)state->num_stars); 
    if (star_pos_loc >= 0) glDisableVertexAttribArray(star_pos_loc);
    if (state->loc_stars_aBaseSize >= 0) glDisableVertexAttribArray(state->loc_stars_aBaseSize);
    if (state->loc_stars_aBaseBrightness >= 0) glDisableVertexAttribArray(state->loc_stars_aBaseBrightness);
    glDisable(GL_BLEND);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, params->physical_width, params->physical_height);
    glUseProgram(state->shader_program);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_write); 
    glUniform1i(state->loc_env_uDynamicNebula, 0);
    glUniform1f(state->loc_env_uTime, (GLfloat)state->current_time_sec);
    glUniform2f(state->loc_env_uResolution, (GLfloat)params->physical_width, (GLfloat)params->physical_height);
    glUniform1f(state->loc_env_uNebulaBrightness, state->currentNebulaBrightness);
    glUniform1f(state->loc_env_uNebulaDensity, state->currentNebulaDensity);
    glUniform3fv(state->loc_env_uNebulaColor1, 1, state->currentNebulaColor1);
    glUniform3fv(state->loc_env_uNebulaColor2, 1, state->currentNebulaColor2);
    glUniform3fv(state->loc_env_uCameraPos, 1, state->currentStarPos);
    starfield_upload_lens_data(state, 
        state->loc_env_uLensPos, state->loc_env_uLensRadius, 
        state->loc_env_uLensStrength, state->loc_env_uLensDepthRange, 
        state->loc_env_uActiveLensCount);
    glBindBuffer(GL_ARRAY_BUFFER, params->common_vbo);
    GLint pos_loc_env = glGetAttribLocation(state->shader_program, "aPosition");
    GLint tex_loc_env = glGetAttribLocation(state->shader_program, "aTexCoord");
    if (pos_loc_env >= 0) {
        glVertexAttribPointer(pos_loc_env, 2, GL_FLOAT, GL_FALSE, common_stride, (void*)0);
        glEnableVertexAttribArray(pos_loc_env);
    }
    if (tex_loc_env >= 0) {
        glVertexAttribPointer(tex_loc_env, 2, GL_FLOAT, GL_FALSE, common_stride, (void*)(2 * sizeof(GLfloat)));
        glEnableVertexAttribArray(tex_loc_env);
    }
    glDrawArrays(GL_TRIANGLES, 0, 6);
    if (pos_loc_env >= 0) glDisableVertexAttribArray(pos_loc_env);
    if (tex_loc_env >= 0) glDisableVertexAttribArray(tex_loc_env);
    glUseProgram(state->shader_program_stars);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);
    glUniform3fv(state->loc_stars_uNebulaColor1, 1, state->currentNebulaColor1);
    glUniform3fv(state->loc_stars_uNebulaColor2, 1, state->currentNebulaColor2);
    glUniform1i(state->loc_stars_uRenderPass_Alive, 1); 
    glUniform1f(state->loc_stars_uGlobalFade, state->currentGlobalFade);
    glBindBuffer(GL_ARRAY_BUFFER, state->star_vbo);
    if (star_pos_loc >= 0) {
        glVertexAttribPointer(star_pos_loc, 3, GL_FLOAT, GL_FALSE, star_stride, (void*)0);
        glEnableVertexAttribArray(star_pos_loc);
    }
    if (state->loc_stars_aBaseSize >= 0) {
        glVertexAttribPointer(state->loc_stars_aBaseSize, 1, GL_FLOAT, GL_FALSE, star_stride, (void*)(3 * sizeof(GLfloat)));
        glEnableVertexAttribArray(state->loc_stars_aBaseSize);
    }
    if (state->loc_stars_aBaseBrightness >= 0) {
        glVertexAttribPointer(state->loc_stars_aBaseBrightness, 1, GL_FLOAT, GL_FALSE, star_stride, (void*)(4 * sizeof(GLfloat)));
        glEnableVertexAttribArray(state->loc_stars_aBaseBrightness);
    }
    glDrawArrays(GL_POINTS, 0, (GLsizei)state->num_stars); 
    if (star_pos_loc >= 0) glDisableVertexAttribArray(star_pos_loc);
    if (state->loc_stars_aBaseSize >= 0) glDisableVertexAttribArray(state->loc_stars_aBaseSize);
    if (state->loc_stars_aBaseBrightness >= 0) glDisableVertexAttribArray(state->loc_stars_aBaseBrightness);
    glDisable(GL_BLEND);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    state->ping_pong_state = !state->ping_pong_state;
    return true;
}


// --- [ИЗМЕНЕНО] Добавлена команда set_lens_fade_times ---
static bool starfield_handle_command(void* mode_state, const char* command) {
    StarfieldModeState* state = (StarfieldModeState*)mode_state;
    if (!state || !command) return false;
    float v1, v2, v3;
    if (strcmp(command, "faster") == 0) {
        state->currentSpeed *= 1.25f; if (state->currentSpeed > 10.0f) state->currentSpeed = 10.0f;
        log_debug("ModeStarfield: Set speed multiplier=%.2f\n", state->currentSpeed);
        return true;
    } else if (strcmp(command, "slower") == 0) {
        state->currentSpeed /= 1.25f; if (state->currentSpeed < 0.05f) state->currentSpeed = 0.05f;
        log_debug("ModeStarfield: Set speed multiplier=%.2f\n", state->currentSpeed);
        return true;
    } else if (sscanf(command, "set_speed %f", &v1) == 1) {
        state->currentSpeed = v1 > 0.0f ? v1 : 0.0f;
        log_debug("ModeStarfield: Set speed multiplier=%.2f\n", state->currentSpeed);
        return true;
    } else if (sscanf(command, "set_star_velocity %f %f %f", &v1, &v2, &v3) == 3) {
        state->currentStarVelocity[0] = v1; state->currentStarVelocity[1] = v2; state->currentStarVelocity[2] = v3;
        log_debug("ModeStarfield: Set star velocity=(%.2f, %.2f, %.2f)\n", v1, v2, v3); return true;
    } else if (sscanf(command, "set_tail_probability %f", &v1) == 1) {
        if (v1 < 0.0f) v1 = 0.0f; if (v1 > 1.0f) v1 = 1.0f;
        state->currentTailProbability = v1;
        log_debug("ModeStarfield: Set tail probability=%.2f\n", state->currentTailProbability); return true;
    } else if (sscanf(command, "set_lens_velocity %f %f %f", &v1, &v2, &v3) == 3) {
        state->lens_velocity[0] = v1; state->lens_velocity[1] = v2; state->lens_velocity[2] = v3;
        log_debug("ModeStarfield: Set lens velocity=(%.2f, %.2f, %.2f)\n", v1, v2, v3); return true;
    } else if (sscanf(command, "set_lens_spawn_rate %f", &v1) == 1) {
        state->lens_spawn_frequency = v1 >= 0.0f ? v1 : 0.0f;
        log_debug("ModeStarfield: Set lens spawn rate=%.2f Hz\n", state->lens_spawn_frequency); return true;
    } else if (sscanf(command, "set_lens_fade_times %f %f", &v1, &v2) == 2) {
        // [НОВОЕ]
        state->lens_fade_in_time = v1 > 0.01f ? v1 : 0.01f;
        state->lens_fade_out_time = v2 > 0.01f ? v2 : 0.01f;
        log_debug("ModeStarfield: Set lens fade times (In=%.2f, Out=%.2f)\n", state->lens_fade_in_time, state->lens_fade_out_time);
        return true;
    } else if (sscanf(command, "set_lens_params %f %f %f", &v1, &v2, &v3) == 3) {
        state->lens_base_radius = v1 > 0.1f ? v1 : 0.1f;
        state->lens_base_strength = v2;
        state->lens_base_ttl = v3 > 0.1f ? v3 : 0.1f;
        log_debug("ModeStarfield: Set lens params (Radius=%.1f, Strength=%.1f, TTL=%.1f)\n", v1, v2, v3); return true;
    } else if (sscanf(command, "set_star_count %f", &v1) == 1) {
        size_t count = (size_t)v1;
        if (count < 100) count = 100;
        if (count > 500000) count = 500000;
        log_debug("ModeStarfield: Rebuilding stars with count=%zu\n", count);
        if (!starfield_rebuild_stars(state, count)) {
             fprintf(stderr, "ModeStarfield: CRITICAL: Failed to rebuild stars!\n");
        }
        return true;
    } else if (sscanf(command, "set_star_size %f", &v1) == 1) {
        state->currentStarBaseSize = v1 > 0.0f ? v1 : 0.0f;
        log_debug("ModeStarfield: Set star base size=%.2f\n", state->currentStarBaseSize); return true;
    } else if (sscanf(command, "set_star_brightness %f", &v1) == 1) {
        state->currentStarBaseBrightness = v1 > 0.0f ? v1 : 0.0f;
        log_debug("ModeStarfield: Set star base brightness=%.2f\n", state->currentStarBaseBrightness); return true;
    } else if (sscanf(command, "set_nebula_brightness %f", &v1) == 1) {
        state->currentNebulaBrightness = v1 >= 0.0f ? v1 : 0.0f;
        log_debug("ModeStarfield: Set nebula brightness=%.2f\n", state->currentNebulaBrightness); return true;
    } else if (sscanf(command, "set_nebula_density %f", &v1) == 1) {
        state->currentNebulaDensity = v1 > 0.01f ? v1 : 0.01f;
        log_debug("ModeStarfield: Set nebula density=%.2f\n", state->currentNebulaDensity); return true;
    } else if (sscanf(command, "set_nebula_color1 %f %f %f", &v1, &v2, &v3) == 3) {
        state->currentNebulaColor1[0] = v1; state->currentNebulaColor1[1] = v2; state->currentNebulaColor1[2] = v3;
        log_debug("ModeStarfield: Set nebula color1=(%.2f, %.2f, %.2f)\n", v1, v2, v3); return true;
    } else if (sscanf(command, "set_nebula_color2 %f %f %f", &v1, &v2, &v3) == 3) {
        state->currentNebulaColor2[0] = v1; state->currentNebulaColor2[1] = v2; state->currentNebulaColor2[2] = v3;
        log_debug("ModeStarfield: Set nebula color2=(%.2f, %.2f, %.2f)\n", v1, v2, v3); return true;
    } else if (strcmp(command, "reset_camera") == 0) {
        state->currentStarPos[0] = 0.0f;
        state->currentStarPos[1] = 0.0f;
        state->currentStarPos[2] = 0.0f;
        state->currentGlobalFade = 0.0f;
        state->active_lens_count = 0; 
        if (state->fbos[0]) {
            for (int i = 0; i < 2; i++) {
                glBindFramebuffer(GL_FRAMEBUFFER, state->fbos[i]);
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        log_debug("ModeStarfield: Camera, fade, FBOs and lenses reset.\n");
        return true;
    } else if (sscanf(command, "set_camera_pos %f %f %f", &v1, &v2, &v3) == 3) {
        state->currentStarPos[0] = v1;
        state->currentStarPos[1] = v2;
        state->currentStarPos[2] = v3;
        log_debug("ModeStarfield: Set camera position=(%.2f, %.2f, %.2f)\n", v1, v2, v3);
        return true;
    }
    return false;
}

// ... (starfield_resize, starfield_needs_redraw, starfield_mode_interface БЕЗ ИЗМЕНЕНИЙ) ...
static void starfield_resize(void* mode_state, int physical_width, int physical_height) {
    StarfieldModeState* state = (StarfieldModeState*)mode_state;
    if (!state) return;
    if (!starfield_resize_fbos(state, (GLsizei)physical_width, (GLsizei)physical_height)) {
        fprintf(stderr, "ModeStarfield CRITICAL: FBO resize failed!\n");
        return;
    }
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
