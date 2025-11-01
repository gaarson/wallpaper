// src/renderer/starfield.c

#include "starfield.h"
#include "./../shader_utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <EGL/egl.h>
#include <string.h>
#include <math.h>
#include <GLES3/gl3.h>
#include <time.h>

#define STARFIELD_STARS_VERTEX_SHADER "src/renderer/shaders/stars.vert"
#define STARFIELD_STARS_FRAGMENT_SHADER "src/renderer/shaders/stars.frag"
#define NUM_STARS 50000
#define UNIVERSE_SIZE 100.0f

#define STARFIELD_VERTEX_SHADER "src/renderer/shaders/starfield.vert"
#define STARFIELD_FRAGMENT_SHADER "src/renderer/shaders/starfield.frag"

// --- НОВЫЙ ШЕЙДЕР ---
#define STARFIELD_UPDATE_VERTEX_SHADER "src/renderer/shaders/starfield.vert" // Можно использовать тот же .vert
#define STARFIELD_UPDATE_FRAGMENT_SHADER "src/renderer/shaders/nebula_update.frag" // НОВЫЙ .frag

#define BASE_LAYER1_SPEED 0.01f
#define BASE_LAYER2_SPEED 0.05f
#define BASE_LAYER3_SPEED 0.1f

typedef struct {
    GLuint shader_program;           // Шейдер для фона (Проход 3)
    GLuint shader_program_stars;       // Шейдер для звезд (Проход 2 и 4)
    GLuint shader_program_nebula_update; // Шейдер для FBO (Проход 1)

    double current_time_sec;
    GLuint star_vbo;
    int num_stars;

    // --- НОВОЕ: FBO "Ping-Pong" буферы ---
    GLuint fbos[2];
    GLuint fbo_textures[2];
    GLsizei fbo_width;
    GLsizei fbo_height;
    bool ping_pong_state; // false = A->B, true = B->A
    
    // --- Uniform'ы для шейдера фона (ENV) ---
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
    GLint loc_env_uDynamicNebula; // <-- (для чтения из FBO)

    // --- Uniform'ы для шейдера ЗВЕЗД (STARS) ---
    GLint loc_stars_uProjectionMatrix;
    GLint loc_stars_uCameraPos;
    GLint loc_stars_uResolution;
    GLint loc_stars_uCameraSpeed;
    GLint loc_stars_uUniverseSize;
    GLint loc_stars_uLensPos;
    GLint loc_stars_uLensRadius;
    GLint loc_stars_uLensStrength;
    GLint loc_stars_uDissolveStart;
    GLint loc_stars_uDissolveEnd;
    GLint loc_stars_uNebulaDissolveColor;
    GLint loc_stars_uGlobalFade; // Глобальный фейд для живых звезд
    GLint loc_stars_uRenderPass_Alive; // <-- (для выбора прохода 2 или 4)
    GLint loc_stars_uLensDepthRange;   // <-- ДОБАВЬ ЭТУ СТРОКУ
    
    // --- Uniform'ы для шейдера ОБНОВЛЕНИЯ (UPDATE) ---
    GLint loc_update_uPreviousNebula;
    GLint loc_update_uFadeFactor;

    // --- Члены состояния ---
    float currentSpeed;
    float currentBrightness; // (uBrightness для фона)
    float currentParallaxMul;
    float currentNebulaBrightness;
    float currentNebulaDensity;
    float currentNebulaColor1[3];
    float currentNebulaColor2[3];
    float currentLensRadius;
    float currentLensStrength;
    float currentCameraPos[3];
    float currentGlobalFade; // (для живых звезд)
    float currentLensDepthRange; // <-- ДОБАВЬ ЭТУ СТРОКУ

} StarfieldModeState;

// ... (rand_float) ...
static float rand_float() {
    return (float)rand() / (float)RAND_MAX;
}

// --- НОВАЯ ФУНКЦИЯ: Инициализация/Изменение размера FBO ---
static bool starfield_resize_fbos(StarfieldModeState* state, GLsizei width, GLsizei height) {
    if (!state) return false;
    
    // Если размер не изменился, ничего не делаем
    if (state->fbo_width == width && state->fbo_height == height) {
        return true;
    }

    printf("ModeStarfield GLES3: Resizing FBOs to %dx%d\n", width, height);

    // 1. Очищаем старые ресурсы, если они есть
    if (state->fbos[0]) glDeleteFramebuffers(2, state->fbos);
    if (state->fbo_textures[0]) glDeleteTextures(2, state->fbo_textures);
    
    glGenFramebuffers(2, state->fbos);
    glGenTextures(2, state->fbo_textures);

    for (int i = 0; i < 2; i++) {
        glBindTexture(GL_TEXTURE_2D, state->fbo_textures[i]);
        // Используем 16-битный float-формат для HDR-блендинга (чтобы яркость могла > 1.0)
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_HALF_FLOAT, NULL);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // Привязываем текстуру к FBO
        glBindFramebuffer(GL_FRAMEBUFFER, state->fbos[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, state->fbo_textures[i], 0);

        // Проверяем FBO
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

    // Очистим новые текстуры
    for (int i = 0; i < 2; i++) {
        glBindFramebuffer(GL_FRAMEBUFFER, state->fbos[i]);
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    printf("ModeStarfield GLES3: FBOs created and sized.\n");
    return true;
}

static bool setup_star_vbo(StarfieldModeState* state) {
    if (!state) return false;

    state->num_stars = NUM_STARS;
    float* star_data = malloc(state->num_stars * 3 * sizeof(float));
    if (!star_data) {
        perror("Failed to allocate memory for stars");
        return false;
    }

    printf("ModeStarfield GLES3: Generating %d stars...\n", state->num_stars);
    for (int i = 0; i < state->num_stars; i++) {
        star_data[i*3 + 0] = (rand_float() - 0.5f) * UNIVERSE_SIZE;
        star_data[i*3 + 1] = (rand_float() - 0.5f) * UNIVERSE_SIZE;
        star_data[i*3 + 2] = (rand_float() - 0.5f) * UNIVERSE_SIZE;
    }

    glGenBuffers(1, &state->star_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, state->star_vbo);
    glBufferData(GL_ARRAY_BUFFER, state->num_stars * 3 * sizeof(float), star_data, GL_STATIC_DRAW);

    free(star_data);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    printf("ModeStarfield GLES3: Star VBO created (ID: %u).\n", state->star_vbo);
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

static void starfield_update_layer_speeds(StarfieldModeState* state) {
    if (!state) return;
    // [ИСПРАВЛЕНО] Убрана неиспользуемая переменная
    // float master_speed_factor = state->currentSpeed; 
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
    srand(time(NULL));

    StarfieldModeState* state = calloc(1, sizeof(StarfieldModeState));
    if (!state) return NULL;

    // --- 1. Настройка параметров ---
    state->currentSpeed = 0.030f;
    state->currentBrightness = 0.14f; // Яркость фонового 3D-шума
    state->currentParallaxMul = 1.4f;
    state->currentCameraPos[0] = 0.0f; state->currentCameraPos[1] = 0.0f; state->currentCameraPos[2] = 0.0f;
    state->currentGlobalFade = 0.0f; // Фейд для живых звезд
    state->currentNebulaBrightness = 0.1f; // Яркость фонового 3D-шума
    state->currentNebulaDensity = 0.3f;
    state->currentNebulaColor1[0] = 0.3f; state->currentNebulaColor1[1] = 0.40f; state->currentNebulaColor1[2] = 0.2f;
    state->currentNebulaColor2[0] = 0.9f; state->currentNebulaColor2[1] = 0.9f; state->currentNebulaColor2[2] = 0.3f;
    state->currentLensRadius = 100.5f;
    state->currentLensStrength = 2.0f;

    state->currentLensDepthRange = 10.0f; 
    
    state->fbo_width = -1; // FBO будут созданы при первом resize
    state->fbo_height = -1;
    state->ping_pong_state = false;

    // --- 2. Создание Программы 1 (Фон/Туманность - Проход 3) ---
    state->shader_program = create_program_from_files(STARFIELD_VERTEX_SHADER, STARFIELD_FRAGMENT_SHADER);
    if (!state->shader_program) { free(state); return NULL; }
    
    printf("ModeStarfield GLES3: Getting uniforms for ENV (Pass 3) shader...\n");
    state->loc_env_uResolution = get_uniform_location(state->shader_program, "uResolution");
    state->loc_env_uTime = get_uniform_location(state->shader_program, "uTime");
    state->loc_env_uNebulaBrightness = get_uniform_location(state->shader_program, "uNebulaBrightness");
    state->loc_env_uNebulaDensity = get_uniform_location(state->shader_program, "uNebulaDensity");
    state->loc_env_uNebulaColor1 = get_uniform_location(state->shader_program, "uNebulaColor1");
    state->loc_env_uNebulaColor2 = get_uniform_location(state->shader_program, "uNebulaColor2");
    state->loc_env_uCameraPos = get_uniform_location(state->shader_program, "uCameraPos");
    state->loc_env_uLensPos = get_uniform_location(state->shader_program, "uLensPos");
    state->loc_env_uLensRadius = get_uniform_location(state->shader_program, "uLensRadius");
    state->loc_env_uLensStrength = get_uniform_location(state->shader_program, "uLensStrength");
    state->loc_env_uDynamicNebula = get_uniform_location(state->shader_program, "uDynamicNebula"); // <-- НОВЫЙ

    // --- 3. Создание Программы 2 (Звезды - Проход 2 и 4) ---
    state->shader_program_stars = create_program_from_files(STARFIELD_STARS_VERTEX_SHADER, STARFIELD_STARS_FRAGMENT_SHADER);
    if (!state->shader_program_stars) { glDeleteProgram(state->shader_program); free(state); return NULL; }
    
    printf("ModeStarfield GLES3: Getting uniforms for STARS (Pass 2/4) shader...\n");
    state->loc_stars_uProjectionMatrix = get_uniform_location(state->shader_program_stars, "uProjectionMatrix");
    state->loc_stars_uCameraPos = get_uniform_location(state->shader_program_stars, "uCameraPos");
    state->loc_stars_uResolution = get_uniform_location(state->shader_program_stars, "uResolution");
    state->loc_stars_uCameraSpeed = get_uniform_location(state->shader_program_stars, "uCameraSpeed");
    state->loc_stars_uLensPos = get_uniform_location(state->shader_program_stars, "uLensPos");
    state->loc_stars_uLensRadius = get_uniform_location(state->shader_program_stars, "uLensRadius");
    state->loc_stars_uLensStrength = get_uniform_location(state->shader_program_stars, "uLensStrength");
    state->loc_stars_uUniverseSize = get_uniform_location(state->shader_program_stars, "uUniverseSize");
    state->loc_stars_uDissolveStart = get_uniform_location(state->shader_program_stars, "uDissolveStart");
    state->loc_stars_uDissolveEnd = get_uniform_location(state->shader_program_stars, "uDissolveEnd");
    state->loc_stars_uNebulaDissolveColor = get_uniform_location(state->shader_program_stars, "uNebulaDissolveColor");
    state->loc_stars_uGlobalFade = get_uniform_location(state->shader_program_stars, "uGlobalFade");
    state->loc_stars_uRenderPass_Alive = get_uniform_location(state->shader_program_stars, "uRenderPass_Alive"); // <-- НОВЫЙ
    state->loc_stars_uLensDepthRange = get_uniform_location(state->shader_program_stars, "uLensDepthRange"); // <-- ДОБАВЬ

    // --- 4. Создание Программы 3 (Обновление FBO - Проход 1) ---
    state->shader_program_nebula_update = create_program_from_files(STARFIELD_UPDATE_VERTEX_SHADER, STARFIELD_UPDATE_FRAGMENT_SHADER);
    if (!state->shader_program_nebula_update) { glDeleteProgram(state->shader_program); glDeleteProgram(state->shader_program_stars); free(state); return NULL; }
    
    printf("ModeStarfield GLES3: Getting uniforms for UPDATE (Pass 1) shader...\n");
    state->loc_update_uPreviousNebula = get_uniform_location(state->shader_program_nebula_update, "uPreviousNebula");
    state->loc_update_uFadeFactor = get_uniform_location(state->shader_program_nebula_update, "uFadeFactor");
    
    // --- 5. VBO для Звезд ---
    if (!setup_star_vbo(state)) { 
        glDeleteProgram(state->shader_program); 
        glDeleteProgram(state->shader_program_stars); 
        glDeleteProgram(state->shader_program_nebula_update); 
        free(state); 
        return NULL; 
    }

    printf("ModeStarfield GLES3: Initialized.\n");
    return state;
}

static void starfield_cleanup(void* mode_state) {
    StarfieldModeState* state = (StarfieldModeState*)mode_state;
    if (!state) return;
    printf("ModeStarfield GLES3: Cleaning up...\n");

    if (state->shader_program) glDeleteProgram(state->shader_program);
    if (state->shader_program_stars) glDeleteProgram(state->shader_program_stars);
    if (state->shader_program_nebula_update) glDeleteProgram(state->shader_program_nebula_update);
    
    if (state->star_vbo) glDeleteBuffers(1, &state->star_vbo);
    
    // --- Очистка FBO ---
    if (state->fbos[0]) glDeleteFramebuffers(2, state->fbos);
    if (state->fbo_textures[0]) glDeleteTextures(2, state->fbo_textures);
    
    memset(state, 0, sizeof(StarfieldModeState)); // Обнуляем, чтобы избежать двойного удаления
    free(state);
}

static bool starfield_render(void* mode_state, const RenderParams* params) {
    StarfieldModeState* state = (StarfieldModeState*)mode_state;
    
    // [ИСПРАВЛЕНО] Логическая ошибка. Проверяем, что fbo_width > 0
    if (!state || !state->shader_program || !state->shader_program_stars || !params || state->fbo_width <= 0) {
        return false; // Не рендерим, пока FBO не готовы
    }

    // --- 1. Обновление Логики ---
    state->current_time_sec += params->time_delta_sec;
    float flight_delta = (float)params->time_delta_sec * 2.0f * state->currentSpeed;
    state->currentCameraPos[2] += flight_delta;
    
    // Фейд для "живых" звезд
    if (state->currentGlobalFade < 1.0f) {
        state->currentGlobalFade += (float)params->time_delta_sec * 0.5f; // STARS_FADE_IN_SPEED
        if (state->currentGlobalFade > 1.0f) state->currentGlobalFade = 1.0f;
    }

    float aspect = (float)params->physical_width / (float)params->physical_height;
    float lens_z = state->currentCameraPos[2] + 40.0f;
    float lens_x = sinf((float)state->current_time_sec * 0.2f) * 20.0f;
    float lens_y = cosf((float)state->current_time_sec * 0.2f) * 20.0f;
    
    // Определяем FBO для чтения и записи
    GLuint fbo_write = state->fbos[state->ping_pong_state ? 0 : 1];
    GLuint tex_read = state->fbo_textures[state->ping_pong_state ? 1 : 0];
    GLuint tex_write = state->fbo_textures[state->ping_pong_state ? 0 : 1];

    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);

    // --- ПРОХОД 1: Затухание/Размытие (Рисуем в 'write' FBO) ---
    glBindFramebuffer(GL_FRAMEBUFFER, fbo_write);
    glViewport(0, 0, state->fbo_width, state->fbo_height);
    
    glUseProgram(state->shader_program_nebula_update);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_read);
    glUniform1i(state->loc_update_uPreviousNebula, 0);
    glUniform1f(state->loc_update_uFadeFactor, 0.99f); // "Рассеивание" тумана

    // Рисуем полноэкранный квадрат
    glBindBuffer(GL_ARRAY_BUFFER, params->common_vbo);
    GLint pos_loc_up = glGetAttribLocation(state->shader_program_nebula_update, "aPosition");
    GLint tex_loc_up = glGetAttribLocation(state->shader_program_nebula_update, "aTexCoord");
    
    // --- ПЕРВОЕ И ЕДИНСТВЕННОЕ ОБЪЯВЛЕНИЕ 'stride' ---
    const GLsizei stride = 4 * sizeof(GLfloat); 
    
    if (pos_loc_up >= 0) {
        glVertexAttribPointer(pos_loc_up, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
        glEnableVertexAttribArray(pos_loc_up);
    }
    if (tex_loc_up >= 0) {
        glVertexAttribPointer(tex_loc_up, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(GLfloat)));
        glEnableVertexAttribArray(tex_loc_up);
    }
    glDrawArrays(GL_TRIANGLES, 0, 6);
    if (pos_loc_up >= 0) glDisableVertexAttribArray(pos_loc_up);
    if (tex_loc_up >= 0) glDisableVertexAttribArray(tex_loc_up);


    // --- ПРОХОД 2: Накопление частиц (Рисуем в 'write' FBO) ---
    // (FBO уже привязан)
    glUseProgram(state->shader_program_stars);

    // Включаем аддитивный блендинг
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE); // Накопление яркости

    float proj_matrix[16];
    create_projection_matrix(70.0f, aspect, 0.1f, UNIVERSE_SIZE / 2.0f, proj_matrix);

    // Отправляем uniforms для ЗВЕЗД
    glUniformMatrix4fv(state->loc_stars_uProjectionMatrix, 1, GL_FALSE, proj_matrix);
    glUniform3fv(state->loc_stars_uCameraPos, 1, state->currentCameraPos);
    glUniform2f(state->loc_stars_uResolution, (GLfloat)params->physical_width, (GLfloat)params->physical_height);
    glUniform1f(state->loc_stars_uCameraSpeed, 2.0f * state->currentSpeed);
    glUniform1f(state->loc_stars_uUniverseSize, UNIVERSE_SIZE);
    glUniform1f(state->loc_stars_uDissolveStart, -10.0f);
    glUniform1f(state->loc_stars_uDissolveEnd, -50.0f);
    glUniform3fv(state->loc_stars_uNebulaDissolveColor, 1, state->currentNebulaColor1);
    
    glUniform3f(state->loc_stars_uLensPos, lens_x, lens_y, lens_z);
    glUniform1f(state->loc_stars_uLensRadius, state->currentLensRadius);
    glUniform1f(state->loc_stars_uLensStrength, state->currentLensStrength);
    glUniform1f(state->loc_stars_uLensDepthRange, state->currentLensDepthRange); // <-- ДОБАВЬ
                                                                                
    glUniform1i(state->loc_stars_uRenderPass_Alive, 0); // <-- 0 = Рендерим частицы

    // Биндим VBO со звездами
    glBindBuffer(GL_ARRAY_BUFFER, state->star_vbo);
    GLint star_pos_loc = glGetAttribLocation(state->shader_program_stars, "aPosition");
    if (star_pos_loc >= 0) {
        glVertexAttribPointer(star_pos_loc, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
        glEnableVertexAttribArray(star_pos_loc);
    }
    glDrawArrays(GL_POINTS, 0, state->num_stars); // Рисуем частицы
    if (star_pos_loc >= 0) glDisableVertexAttribArray(star_pos_loc);

    glDisable(GL_BLEND);

    // --- ПРОХОД 3: Фон (Рисуем на ЭКРАН) ---
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, params->physical_width, params->physical_height);
    
    glUseProgram(state->shader_program);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex_write); // Читаем то, куда только что писали
    glUniform1i(state->loc_env_uDynamicNebula, 0);

    // Отправляем остальные uniforms для фона
    glUniform1f(state->loc_env_uTime, (GLfloat)state->current_time_sec);
    glUniform2f(state->loc_env_uResolution, (GLfloat)params->physical_width, (GLfloat)params->physical_height);
    glUniform1f(state->loc_env_uNebulaBrightness, state->currentNebulaBrightness);
    glUniform1f(state->loc_env_uNebulaDensity, state->currentNebulaDensity);
    glUniform3fv(state->loc_env_uNebulaColor1, 1, state->currentNebulaColor1);
    glUniform3fv(state->loc_env_uNebulaColor2, 1, state->currentNebulaColor2);
    glUniform3fv(state->loc_env_uCameraPos, 1, state->currentCameraPos);
    glUniform3f(state->loc_env_uLensPos, lens_x, lens_y, lens_z);
    glUniform1f(state->loc_env_uLensRadius, state->currentLensRadius);
    glUniform1f(state->loc_env_uLensStrength, state->currentLensStrength);

    // Рисуем полноэкранный квадрат (фон)
    glBindBuffer(GL_ARRAY_BUFFER, params->common_vbo);
    GLint pos_loc_env = glGetAttribLocation(state->shader_program, "aPosition");
    GLint tex_loc_env = glGetAttribLocation(state->shader_program, "aTexCoord");
    
    // --- [ИСПРАВЛЕНО] ---
    // УДАЛЕНО: const GLsizei stride = 4 * sizeof(GLfloat); 
    // Мы ИСПОЛЬЗУЕМ 'stride', объявленный в Проходе 1.
    
    if (pos_loc_env >= 0) {
        glVertexAttribPointer(pos_loc_env, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
        glEnableVertexAttribArray(pos_loc_env);
    }
    if (tex_loc_env >= 0) {
        glVertexAttribPointer(tex_loc_env, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(GLfloat)));
        glEnableVertexAttribArray(tex_loc_env);
    }
    glDrawArrays(GL_TRIANGLES, 0, 6);
    if (pos_loc_env >= 0) glDisableVertexAttribArray(pos_loc_env);
    if (tex_loc_env >= 0) glDisableVertexAttribArray(tex_loc_env);

    // --- ПРОХОД 4: Живые звезды (Рисуем на ЭКРАН) ---
    glUseProgram(state->shader_program_stars);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); // Обычный аддитивный блендинг для звезд

    // Отправляем uniforms для звезд
    glUniformMatrix4fv(state->loc_stars_uProjectionMatrix, 1, GL_FALSE, proj_matrix);
    glUniform3fv(state->loc_stars_uCameraPos, 1, state->currentCameraPos);
    glUniform2f(state->loc_stars_uResolution, (GLfloat)params->physical_width, (GLfloat)params->physical_height);
    glUniform1f(state->loc_stars_uCameraSpeed, 2.0f * state->currentSpeed);
    glUniform1f(state->loc_stars_uUniverseSize, UNIVERSE_SIZE);
    glUniform1f(state->loc_stars_uDissolveStart, -10.0f);
    glUniform1f(state->loc_stars_uDissolveEnd, -50.0f);
    glUniform3fv(state->loc_stars_uNebulaDissolveColor, 1, state->currentNebulaColor1);

    glUniform1i(state->loc_stars_uRenderPass_Alive, 1); // <-- 1 = Рендерим "живые"
    glUniform1f(state->loc_stars_uGlobalFade, state->currentGlobalFade); // <-- Фейд для живых звезд

    glUniform3f(state->loc_stars_uLensPos, lens_x, lens_y, lens_z);
    glUniform1f(state->loc_stars_uLensRadius, state->currentLensRadius);
    glUniform1f(state->loc_stars_uLensStrength, state->currentLensStrength);
    glUniform1f(state->loc_stars_uLensDepthRange, state->currentLensDepthRange); // <-- ДОБАВЬ

    // Биндим VBO со звездами
    glBindBuffer(GL_ARRAY_BUFFER, state->star_vbo);
    if (star_pos_loc >= 0) {
        glVertexAttribPointer(star_pos_loc, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
        glEnableVertexAttribArray(star_pos_loc);
    }
    glDrawArrays(GL_POINTS, 0, state->num_stars); // Рисуем живые звезды
    if (star_pos_loc >= 0) glDisableVertexAttribArray(star_pos_loc);

    // --- 5. Очистка кадра ---
    glDisable(GL_BLEND);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    // --- Переключаем Ping-Pong ---
    state->ping_pong_state = !state->ping_pong_state;

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
        // (мертвый параметр)
        return true;
    } else if (sscanf(command, "set_brightness %f", &v1) == 1) {
        state->currentBrightness = v1 >= 0.0f ? v1 : 0.0f;
        printf("ModeStarfield: Set brightness=%.2f\n", state->currentBrightness); return true;
    } else if (sscanf(command, "set_star_count %f", &v1) == 1) {
        // (мертвый параметр)
        return true;
    } else if (sscanf(command, "set_parallax_mul %f", &v1) == 1) {
        state->currentParallaxMul = v1 >= 0.0f ? v1 : 0.0f;
        starfield_update_layer_speeds(state);
        printf("ModeStarfield: Set parallax multiplier=%.2f\n", state->currentParallaxMul); return true;
    } else if (sscanf(command, "set_nebula_brightness %f", &v1) == 1) {
        state->currentNebulaBrightness = v1 >= 0.0f ? v1 : 0.0f;
        printf("ModeStarfield: Set nebula brightness=%.2f\n", state->currentNebulaBrightness); return true;
    } else if (sscanf(command, "set_nebula_density %f", &v1) == 1) {
        state->currentNebulaDensity = v1 > 0.01f ? v1 : 0.01f; // (изменил > 0.1 на > 0.01)
        printf("ModeStarfield: Set nebula density=%.2f\n", state->currentNebulaDensity); return true;
    } else if (sscanf(command, "set_lens_radius %f", &v1) == 1) {
        state->currentLensRadius = v1 > 0.001f ? v1 : 0.001f; // (изменил > 0.1 на > 0.01)
        printf("ModeStarfield: Set nebula density=%.2f\n", state->currentNebulaDensity); return true;
    } else if (sscanf(command, "set_lens_strength %f", &v1) == 1) {
        state->currentLensStrength = v1 > 0.001f ? v1 : 0.001f; // (изменил > 0.1 на > 0.01)
        printf("ModeStarfield: Set nebula density=%.2f\n", state->currentNebulaDensity); return true;
    } else if (sscanf(command, "set_nebula_color1 %f %f %f", &v1, &v2, &v3) == 3) {
        state->currentNebulaColor1[0] = v1; state->currentNebulaColor1[1] = v2; state->currentNebulaColor1[2] = v3;
        printf("ModeStarfield: Set nebula color1=(%.2f, %.2f, %.2f)\n", v1, v2, v3); return true;
    } else if (sscanf(command, "set_nebula_color2 %f %f %f", &v1, &v2, &v3) == 3) {
        state->currentNebulaColor2[0] = v1; state->currentNebulaColor2[1] = v2; state->currentNebulaColor2[2] = v3;
        printf("ModeStarfield: Set nebula color2=(%.2f, %.2f, %.2f)\n", v1, v2, v3); return true;
    } else if (sscanf(command, "set_star_brightness %f", &v1) == 1) {
        // (мертвый параметр)
        return true;
    } else if (sscanf(command, "set_lens_range %f", &v1) == 1) {
        state->currentLensDepthRange = v1 > 0.0f ? v1 : 1.0f;
        printf("ModeStarfield: Set lens depth range=%.2f\n", state->currentLensDepthRange);
        return true;
    } else if (sscanf(command, "set_streaks %f", &v1) == 1) {
       // (мертвый параметр)
       return true;
    } else if (strcmp(command, "reset_camera") == 0) {
        state->currentCameraPos[0] = 0.0f;
        state->currentCameraPos[1] = 0.0f;
        state->currentCameraPos[2] = 0.0f;
        state->currentGlobalFade = 0.0f;

        // --- СБРОС FBO ---
        if (state->fbos[0]) { // Убедимся, что FBO существуют
            for (int i = 0; i < 2; i++) {
                glBindFramebuffer(GL_FRAMEBUFFER, state->fbos[i]);
                glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
                glClear(GL_COLOR_BUFFER_BIT);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }
        printf("ModeStarfield: Camera, fade and FBOs reset.\n");
        return true;
    } else if (sscanf(command, "set_camera_pos %f %f %f", &v1, &v2, &v3) == 3) {
        state->currentCameraPos[0] = v1;
        state->currentCameraPos[1] = v2;
        state->currentCameraPos[2] = v3;
        printf("ModeStarfield: Set camera position=(%.2f, %.2f, %.2f)\n", v1, v2, v3);
        return true;
    }
    return false;
}

static void starfield_resize(void* mode_state, int physical_width, int physical_height) {
    StarfieldModeState* state = (StarfieldModeState*)mode_state;
    if (!state) return;
    // --- ИНИЦИАЛИЗИРУЕМ/МЕНЯЕМ РАЗМЕР FBO ---
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
