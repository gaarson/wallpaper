#include "starfield.h"
#include "./../shader_utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <EGL/egl.h>
#include <string.h>
#include <math.h>
#include <GLES3/gl3.h>

#include <time.h> // Для srand()

#define STARFIELD_STARS_VERTEX_SHADER "src/renderer/shaders/stars.vert"
#define STARFIELD_STARS_FRAGMENT_SHADER "src/renderer/shaders/stars.frag"
#define NUM_STARS 50000 // 50,000 звезд
#define UNIVERSE_SIZE 100.0f // Наша "вселенная" - куб 100x100x100
                            
#define STARFIELD_VERTEX_SHADER "src/renderer/shaders/starfield.vert"
#define STARFIELD_FRAGMENT_SHADER "src/renderer/shaders/starfield.frag"

#define BASE_LAYER1_SPEED 0.01f
#define BASE_LAYER2_SPEED 0.05f
#define BASE_LAYER3_SPEED 0.1f

typedef struct {
    GLuint shader_program;
    double current_time_sec;

    // --- НОВЫЕ ПОЛЯ ---
    GLuint shader_program_stars; // Шейдер для звезд (GL_POINTS)
    GLuint star_vbo;             // VBO с 3D-позициями звезд
    int num_stars;               // Количество звезд ( NUM_STARS )
    
    GLint loc_stars_uProjectionMatrix; // Uniform'ы для шейдера звезд
    GLint loc_stars_uCameraPos;
    GLint loc_stars_uResolution;
    
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
    GLint loc_stars_uUniverseSize;

    GLint loc_stars_uLensPos;
    GLint loc_stars_uLensRadius;
    GLint loc_stars_uLensStrength;

    GLint loc_env_uLensPos;
    GLint loc_env_uLensRadius;
    GLint loc_env_uLensStrength;
    // GLint loc_uLensFieldDensity;
    // GLint loc_uLensSizeMul;
    // GLint loc_uLensGlowMul;
    // GLint loc_uLensStrengthMul;
    // GLint loc_uLensSpeedMul;
    // GLint loc_uLensChance;

    GLint loc_uStreakSamples;
    GLint loc_uCameraPos;  
    GLint loc_stars_uCameraSpeed; 
    
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

    // float currentLensFieldDensity;
    // float currentLensSizeMul;
    // float currentLensGlowMul;
    // float currentLensStrengthMul;
    // float currentLensSpeedMul;
    // float currentLensChance; 

    float currentLensRadius;
    float currentLensStrength;

    float currentStreakSamples;
    float currentCameraPos[3];

} StarfieldModeState;

// Простой генератор float от 0.0 до 1.0
static float rand_float() {
    return (float)rand() / (float)RAND_MAX;
}

// Создает VBO с 3D-позициями звезд
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
        // Позиция X
        star_data[i*3 + 0] = (rand_float() - 0.5f) * UNIVERSE_SIZE;
        // Позиция Y
        star_data[i*3 + 1] = (rand_float() - 0.5f) * UNIVERSE_SIZE;
        
        // --- ИСПРАВЛЕНИЕ: Сгущаем звезды "вдалеке" ---
        // Мы используем powf(..., 2.0f), чтобы сместить 
        // случайное распределение к краям.
        // (1.0f - powf(rand_float(), 2.0f)) -> дает больше чисел, близких к 1.0
        star_data[i*3 + 2] = rand_float() * UNIVERSE_SIZE;
    }

    glGenBuffers(1, &state->star_vbo);
    glBindBuffer(GL_ARRAY_BUFFER, state->star_vbo);
    glBufferData(GL_ARRAY_BUFFER, state->num_stars * 3 * sizeof(float), star_data, GL_STATIC_DRAW);
    
    free(star_data);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    printf("ModeStarfield GLES3: Star VBO created (ID: %u).\n", state->star_vbo);
    return true;
}

// Создает матрицу 3D-проекции (необходима для 3D)
static void create_projection_matrix(float fov_y_deg, float aspect, float z_near, float z_far, float* out_mat) {
    float f = 1.0f / tanf((fov_y_deg * 3.14159265f / 180.0f) / 2.0f);
    
    // Очистим матрицу (важно!)
    memset(out_mat, 0, 16 * sizeof(float));
    
    out_mat[0] = f / aspect;
    out_mat[5] = f;
    out_mat[10] = (z_far + z_near) / (z_near - z_far);
    out_mat[11] = -1.0f;
    out_mat[14] = (2.0f * z_far * z_near) / (z_near - z_far);
}

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
    
    // Инициализируем генератор случайных чисел
    srand(time(NULL));

    StarfieldModeState* state = calloc(1, sizeof(StarfieldModeState));
    if (!state) {
        perror("ModeStarfield GLES3 calloc state failed");
        return NULL;
    }

    // --- 1. Настройка параметров ---
    state->currentSpeed = 1.0f;          // Оставляем твою скорость
    state->currentDensity = 5.0f;         // (Мертвый параметр, но пусть будет)
    state->currentStarThreshold = 0.9f;   // (Мертвый параметр, но пусть будет)
    state->currentBrightness = 0.4f;      // Нормальная яркость
    state->currentParallaxMul = 6.4f;
    state->currentStreakSamples = 1.0f;
    state->currentCameraPos[0] = 0.0f;
    state->currentCameraPos[1] = 0.0f;
    state->currentCameraPos[2] = 0.0f;
    state->currentStarBrightness = 40.0f;
    state->currentNebulaBrightness = 0.63f;
    state->currentNebulaDensity = 0.03f;
    state->currentNebulaColor1[0] = 0.8f; state->currentNebulaColor1[1] = 0.040f; state->currentNebulaColor1[2] = 0.2f;
    state->currentNebulaColor2[0] = 0.0f; state->currentNebulaColor2[1] = 0.3f; state->currentNebulaColor2[2] = 0.9f;
    
    // Параметры линз (мы их настроили в прошлый раз)
    state->currentLensRadius = 400.0f;
    state->currentLensStrength = 50.0f;

    // --- 2. Создание Программы 1 (Фон/Туманность) ---
    state->shader_program = create_program_from_files(STARFIELD_VERTEX_SHADER, STARFIELD_FRAGMENT_SHADER);
    if (!state->shader_program) {
        fprintf(stderr, "ModeStarfield GLES3 Error: Failed to create ENV shader program.\n");
        free(state);
        return NULL;
    }

    // --- 3. Получение Uniform'ов для Программы 1 ---
    printf("ModeStarfield GLES3: Getting uniform locations for ENV shader...\n");
    state->loc_uResolution = get_uniform_location(state->shader_program, "uResolution");
    state->loc_uTime = get_uniform_location(state->shader_program, "uTime");
    state->loc_uBrightness = get_uniform_location(state->shader_program, "uBrightness");
    state->loc_uNebulaBrightness = get_uniform_location(state->shader_program, "uNebulaBrightness");
    state->loc_uNebulaDensity = get_uniform_location(state->shader_program, "uNebulaDensity");
    state->loc_uNebulaColor1 = get_uniform_location(state->shader_program, "uNebulaColor1");
    state->loc_uNebulaColor2 = get_uniform_location(state->shader_program, "uNebulaColor2");
    // (Мертвые uniforms, которые ты оставил - можно их удалить, но они не мешают)
    state->loc_uSpeed = get_uniform_location(state->shader_program, "uSpeed");
    state->loc_uDensity = get_uniform_location(state->shader_program, "uDensity");
    state->loc_uStarThreshold = get_uniform_location(state->shader_program, "uStarThreshold");
    state->loc_uLayer1Speed = get_uniform_location(state->shader_program, "uLayer1Speed");
    state->loc_uLayer2Speed = get_uniform_location(state->shader_program, "uLayer2Speed");
    state->loc_uLayer3Speed = get_uniform_location(state->shader_program, "uLayer3Speed");
    state->loc_uStarBrightness = get_uniform_location(state->shader_program, "uStarBrightness");
    state->loc_uStreakSamples = get_uniform_location(state->shader_program, "uStreakSamples");
    state->loc_uCameraPos = get_uniform_location(state->shader_program, "uCameraPos");
    state->loc_env_uLensPos = get_uniform_location(state->shader_program, "uLensPos");
    state->loc_env_uLensRadius = get_uniform_location(state->shader_program, "uLensRadius");
    state->loc_env_uLensStrength = get_uniform_location(state->shader_program, "uLensStrength");


    // --- 4. Создание Программы 2 (Звезды) ---
    state->shader_program_stars = create_program_from_files(STARFIELD_STARS_VERTEX_SHADER, STARFIELD_STARS_FRAGMENT_SHADER);
    if (!state->shader_program_stars) {
        fprintf(stderr, "ModeStarfield GLES3 Error: Failed to create STARS shader program.\n");
        glDeleteProgram(state->shader_program);
        free(state);
        return NULL;
    }

    // --- 5. Получение Uniform'ов для Программы 2 (ПРАВИЛЬНЫЙ ПОРЯДОК) ---
    printf("ModeStarfield GLES3: Getting uniform locations for STARS shader...\n");
    state->loc_stars_uProjectionMatrix = get_uniform_location(state->shader_program_stars, "uProjectionMatrix");
    state->loc_stars_uCameraPos = get_uniform_location(state->shader_program_stars, "uCameraPos");
    state->loc_stars_uResolution = get_uniform_location(state->shader_program_stars, "uResolution");
    state->loc_stars_uCameraSpeed = get_uniform_location(state->shader_program_stars, "uCameraSpeed");
    state->loc_stars_uLensPos = get_uniform_location(state->shader_program_stars, "uLensPos");
    state->loc_stars_uLensRadius = get_uniform_location(state->shader_program_stars, "uLensRadius");
    state->loc_stars_uLensStrength = get_uniform_location(state->shader_program_stars, "uLensStrength");
    state->loc_stars_uUniverseSize = get_uniform_location(state->shader_program_stars, "uUniverseSize");


    // --- 6. VBO для Звезд ---
    if (!setup_star_vbo(state)) {
        glDeleteProgram(state->shader_program);
        glDeleteProgram(state->shader_program_stars);
        free(state);
        return NULL;
    }

    printf("ModeStarfield GLES3: Initialized (Env Prog ID: %u, Stars Prog ID: %u).\n", state->shader_program, state->shader_program_stars);
    return state;
}

static void starfield_cleanup(void* mode_state) {
    StarfieldModeState* state = (StarfieldModeState*)mode_state;
    if (!state) return;
    printf("ModeStarfield GLES3: Cleaning up...\n");
    
    if (state->shader_program) {
        glDeleteProgram(state->shader_program);
        state->shader_program = 0;
    }
    // --- ОЧИСТКА НОВЫХ РЕСУРСОВ ---
    if (state->shader_program_stars) {
        glDeleteProgram(state->shader_program_stars);
        state->shader_program_stars = 0;
    }
    if (state->star_vbo) {
        glDeleteBuffers(1, &state->star_vbo);
        state->star_vbo = 0;
    }
    // --- КОНЕЦ ОЧИСТКИ ---
    
    free(state);
}

static bool starfield_render(void* mode_state, const RenderParams* params) {
    StarfieldModeState* state = (StarfieldModeState*)mode_state;
    if (!state || !state->shader_program || !state->shader_program_stars || !params) {
        fprintf(stderr, "ModeStarfield GLES3 Error: Invalid state/programs/params in render!\n");
        return false;
    }

    // --- 1. Обновление Логики ---
    // !!! УБЕДИСЬ, ЧТО ЭТИ 3 СТРОКИ НА МЕСТЕ И НЕ ЗАКОММЕНТИРОВАНЫ !!!
    state->current_time_sec += params->time_delta_sec;
    float flight_delta = (float)params->time_delta_sec * 2.0f * state->currentSpeed;
    state->currentCameraPos[2] += flight_delta;
    // !!! КОНЕЦ ПРОВЕРКИ !!!

    float aspect = (float)params->physical_width / (float)params->physical_height;

    float lens_z = state->currentCameraPos[2] + 40.0f;
    float lens_x = sinf((float)state->current_time_sec * 0.2f) * 20.0f;
    float lens_y = cosf((float)state->current_time_sec * 0.2f) * 20.0f;

    // --- 2. ПРОХОД 1: Фон (Туманность) ---
    glDisable(GL_BLEND); 
    glUseProgram(state->shader_program);

    // !!! УБЕДИСЬ, ЧТО ЭТА СТРОКА НА МЕСТЕ (ДЛЯ ДВИЖЕНИЯ ТУМАНА) !!!
    if (state->loc_uTime != -1) glUniform1f(state->loc_uTime, (GLfloat)state->current_time_sec);
    
    // ... (отправка uResolution, uBrightness, uNebula... и т.д.) ...
    if (state->loc_uResolution != -1) glUniform2f(state->loc_uResolution, (GLfloat)params->physical_width, (GLfloat)params->physical_height);
    if (state->loc_uBrightness != -1) glUniform1f(state->loc_uBrightness, state->currentBrightness);
    if (state->loc_uNebulaBrightness != -1) glUniform1f(state->loc_uNebulaBrightness, state->currentNebulaBrightness);
    if (state->loc_uNebulaDensity != -1) glUniform1f(state->loc_uNebulaDensity, state->currentNebulaDensity);
    if (state->loc_uNebulaColor1 != -1) glUniform3fv(state->loc_uNebulaColor1, 1, state->currentNebulaColor1);
    if (state->loc_uNebulaColor2 != -1) glUniform3fv(state->loc_uNebulaColor2, 1, state->currentNebulaColor2);
    // --- ДОБАВЛЯЕМ ОТПРАВКУ ЛИНЗЫ В ШЕЙДЕР ФОНА ---
    if (state->loc_uCameraPos != -1) glUniform3fv(state->loc_uCameraPos, 1, state->currentCameraPos); // Уже было
    if (state->loc_env_uLensPos != -1) glUniform3f(state->loc_env_uLensPos, lens_x, lens_y, lens_z);
    if (state->loc_env_uLensRadius != -1) glUniform1f(state->loc_env_uLensRadius, state->currentLensRadius);
    if (state->loc_env_uLensStrength != -1) glUniform1f(state->loc_env_uLensStrength, state->currentLensStrength);
    // --- КОНЕЦ БЛОКА ---
    // if (state->loc_uLensFieldDensity != -1) glUniform1f(state->loc_uLensFieldDensity, state->currentLensFieldDensity);
    // if (state->loc_uLensSizeMul != -1) glUniform1f(state->loc_uLensSizeMul, state->currentLensSizeMul);
    // if (state->loc_uLensGlowMul != -1) glUniform1f(state->loc_uLensGlowMul, state->currentLensGlowMul);
    // if (state->loc_uLensStrengthMul != -1) glUniform1f(state->loc_uLensStrengthMul, state->currentLensStrengthMul);
    // if (state->loc_uLensSpeedMul != -1) glUniform1f(state->loc_uLensSpeedMul, state->currentLensSpeedMul);
    // if (state->loc_uLensChance != -1) glUniform1f(state->loc_uLensChance, state->currentLensChance);

    // Рисуем полноэкранный квадрат (VBO из `params`)
    glBindBuffer(GL_ARRAY_BUFFER, params->common_vbo);
    GLint pos_loc = glGetAttribLocation(state->shader_program, "aPosition");
    GLint tex_loc = glGetAttribLocation(state->shader_program, "aTexCoord");
    const GLsizei stride = 4 * sizeof(GLfloat);
    if (pos_loc >= 0) {
        glVertexAttribPointer(pos_loc, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
        glEnableVertexAttribArray(pos_loc);
    }
    if (tex_loc >= 0) {
        glVertexAttribPointer(tex_loc, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(GLfloat)));
        glEnableVertexAttribArray(tex_loc);
    }
    glDrawArrays(GL_TRIANGLES, 0, 6);
    if (pos_loc >= 0) glDisableVertexAttribArray(pos_loc);
    if (tex_loc >= 0) glDisableVertexAttribArray(tex_loc);


    // --- 3. ПРОХОД 2: Звезды (Точки) ---
    // Включаем "аддитивное" смешивание: цвет звезды (SRC) просто добавляется (ONE) к тому, что уже есть
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE); 
    
    glUseProgram(state->shader_program_stars);

    // Создаем матрицу проекции
    float proj_matrix[16];
    create_projection_matrix(70.0f, aspect, 0.1f, UNIVERSE_SIZE / 2.0f, proj_matrix);

    // Отправляем uniforms для ЗВЕЗД
    glUniformMatrix4fv(state->loc_stars_uProjectionMatrix, 1, GL_FALSE, proj_matrix);
    glUniform3fv(state->loc_stars_uCameraPos, 1, state->currentCameraPos);
    glUniform2f(state->loc_stars_uResolution, (GLfloat)params->physical_width, (GLfloat)params->physical_height);
    glUniform1f(state->loc_stars_uCameraSpeed, 2.0f * state->currentSpeed);

    glUniform1f(state->loc_stars_uUniverseSize, UNIVERSE_SIZE);

    // float lens_z = fmodf(state->currentCameraPos[2] + (UNIVERSE_SIZE / 2.0f), UNIVERSE_SIZE);
   // Помещаем линзу в 30 единицах *перед* камерой (ближе = заметнее)
    // --- ОТПРАВКА ЛИНЗЫ В ШЕЙДЕР ЗВЕЗД (мы удаляем расчет отсюда) ---
    glUniform3f(state->loc_stars_uLensPos, lens_x, lens_y, lens_z);
    glUniform1f(state->loc_stars_uLensRadius, state->currentLensRadius);
    glUniform1f(state->loc_stars_uLensStrength, state->currentLensStrength);
    // --- КОНЕЦ БЛОКА ---
    // Биндим VBO со звездами
    glBindBuffer(GL_ARRAY_BUFFER, state->star_vbo);
    GLint star_pos_loc = glGetAttribLocation(state->shader_program_stars, "aPosition");
    if (star_pos_loc >= 0) {
        // Stride=0, т.к. данные (XXX, YYY, ZZZ) идут плотно
        glVertexAttribPointer(star_pos_loc, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
        glEnableVertexAttribArray(star_pos_loc);
    }
    
    // Рисуем все звезды как точки
    glDrawArrays(GL_POINTS, 0, state->num_stars);
    
    if (star_pos_loc >= 0) glDisableVertexAttribArray(star_pos_loc);

    // --- 4. Очистка ---
    glDisable(GL_BLEND);
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
    }  else if (sscanf(command, "set_streaks %f", &v1) == 1) {
       state->currentStreakSamples = v1 >= 1.0f ? v1 : 1.0f;
       printf("ModeStarfield: Set streak samples=%.1f\n", state->currentStreakSamples);
       return true;
    } else if (strcmp(command, "reset_camera") == 0) {
        state->currentCameraPos[0] = 0.0f;
        state->currentCameraPos[1] = 0.0f;
        state->currentCameraPos[2] = 0.0f;
        printf("ModeStarfield: Camera position reset.\n");
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
