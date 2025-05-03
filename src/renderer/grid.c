// mode_grid.c
#include "grid.h"
#include "./../shader_utils.h"
#include <stdlib.h>
#include <stdio.h>
#include <EGL/egl.h>
#include <string.h>
#include <math.h>
#include <GLES3/gl3.h> // Используем GLES 3.0

#define GRID_VERTEX_SHADER "src/renderer/shaders/grid.vert"   // Путь к вертексному шейдеру GLES 3.0
#define GRID_FRAGMENT_SHADER "src/renderer/shaders/grid.frag" // Путь к фрагментному шейдеру GLES 3.0

// --- Структура состояния режима сетки ---
typedef struct {
    GLuint shader_program;    // Шейдерная программа
    double current_time_sec;  // Текущее время

    // Локации Uniform-переменных
    GLint loc_uResolution;
    GLint loc_uTime;
    GLint loc_uGridDensity;
    GLint loc_uLineWidth;
    GLint loc_uScrollSpeed;
    GLint loc_uPerspectiveFactor;
    GLint loc_uHorizonY;
    GLint loc_uColorGrid;
    GLint loc_uColorGround;
    GLint loc_uColorSkyNear;
    GLint loc_uColorSkyFar;
    GLint loc_uFogDensity;
    GLint loc_uFogEnable;
    GLint loc_uGridFogEnable;

    // Текущие значения параметров (для установки uniform'ов и изменения командами)
    float gridDensity;
    float lineWidth;
    float scrollSpeed;
    float perspectiveFactor;
    float horizonY;
    float colorGrid[3];
    float colorGround[3];
    float colorSkyNear[3];
    float colorSkyFar[3];
    float fogDensity;
    GLint fogEnable;      // Используем GLint для bool uniform'ов (0 или 1)
    GLint gridFogEnable;  // Используем GLint для bool uniform'ов (0 или 1)

} GridModeState;

// --- Вспомогательная функция для получения локаций ---
// (Чтобы избежать повторений и добавить проверку)
static GLint get_uniform_location(GLuint program, const char* name) {
    GLint loc = glGetUniformLocation(program, name);
    if (loc == -1) {
        fprintf(stderr, "ModeGrid Warning: Uniform '%s' not found or inactive.\n", name);
    }
    return loc;
}


// --- Инициализация режима сетки ---
static void* grid_init(const char* arg, GLuint common_vbo) {
    (void)arg;
    (void)common_vbo;
    printf("ModeGrid GLES3: Initializing...\n");

    GridModeState* state = calloc(1, sizeof(GridModeState));
    if (!state) {
        perror("ModeGrid GLES3 calloc state failed");
        return NULL;
    }

    // --- Установка значений по умолчанию ---
    state->current_time_sec = 0.0;
    state->gridDensity = 2.5f;         // Плотность
    state->lineWidth = 2.0f;            // Ширина линии (множитель для fwidth)
    state->scrollSpeed = 0.3f;          // Скорость
    state->perspectiveFactor = 0.09f;    // Перспектива
    state->horizonY = 0.48f;            // Горизонт
    state->colorGrid[0] = 0.0f; state->colorGrid[1] = 0.9f; state->colorGrid[2] = 1.0f;       // Ярко-голубой
    state->colorGround[0] = 0.05f; state->colorGround[1] = 0.0f; state->colorGround[2] = 0.1f;  // Темно-фиолетовый/красный
    state->colorSkyNear[0] = 0.8f; state->colorSkyNear[1] = 0.2f; state->colorSkyNear[2] = 0.9f; // Розово-пурпурный
    state->colorSkyFar[0] = 0.1f; state->colorSkyFar[1] = 0.0f; state->colorSkyFar[2] = 0.2f;   // Темно-фиолетовый
    state->fogDensity = 0.0018f;          // Плотность тумана
    state->fogEnable = 1;               // Туман включен (1 = true)
    state->gridFogEnable = 1;           // Туман влияет на сетку (1 = true)

    // --- Создание шейдерной программы ---
    state->shader_program = create_program_from_files(GRID_VERTEX_SHADER, GRID_FRAGMENT_SHADER);
    if (!state->shader_program) {
        fprintf(stderr, "ModeGrid GLES3 Error: Failed to create shader program.\n");
        free(state);
        return NULL;
    }

    // --- Получение локаций uniform-переменных ---
    printf("ModeGrid GLES3: Getting uniform locations...\n");
    state->loc_uResolution = get_uniform_location(state->shader_program, "uResolution");
    state->loc_uTime = get_uniform_location(state->shader_program, "uTime");
    state->loc_uGridDensity = get_uniform_location(state->shader_program, "uGridDensity");
    state->loc_uLineWidth = get_uniform_location(state->shader_program, "uLineWidth");
    state->loc_uScrollSpeed = get_uniform_location(state->shader_program, "uScrollSpeed");
    state->loc_uPerspectiveFactor = get_uniform_location(state->shader_program, "uPerspectiveFactor");
    state->loc_uHorizonY = get_uniform_location(state->shader_program, "uHorizonY");
    state->loc_uColorGrid = get_uniform_location(state->shader_program, "uColorGrid");
    state->loc_uColorGround = get_uniform_location(state->shader_program, "uColorGround");
    state->loc_uColorSkyNear = get_uniform_location(state->shader_program, "uColorSkyNear");
    state->loc_uColorSkyFar = get_uniform_location(state->shader_program, "uColorSkyFar");
    state->loc_uFogDensity = get_uniform_location(state->shader_program, "uFogDensity");
    state->loc_uFogEnable = get_uniform_location(state->shader_program, "uFogEnable");
    state->loc_uGridFogEnable = get_uniform_location(state->shader_program, "uGridFogEnable");

    printf("ModeGrid GLES3: Initialized (Program ID: %u).\n", state->shader_program);
    return state;
}

// --- Очистка ---
static void grid_cleanup(void* mode_state) {
    GridModeState* state = (GridModeState*)mode_state;
    if (!state) return;
    printf("ModeGrid GLES3: Cleaning up...\n");
    if (state->shader_program) {
        glDeleteProgram(state->shader_program);
        state->shader_program = 0;
    }
    free(state);
}

// --- Рендеринг ---
static bool grid_render(void* mode_state, const RenderParams* params) {
    GridModeState* state = (GridModeState*)mode_state;
    if (!state || !state->shader_program || !params) {
        fprintf(stderr, "ModeGrid GLES3 Error: Invalid state/program/params in render!\n");
        return false;
    }

    state->current_time_sec += params->time_delta_sec;

    glUseProgram(state->shader_program);

    // --- Установка всех uniform-переменных ---
    if (state->loc_uResolution != -1) glUniform2f(state->loc_uResolution, (GLfloat)params->physical_width, (GLfloat)params->physical_height);
    if (state->loc_uTime != -1) glUniform1f(state->loc_uTime, (GLfloat)state->current_time_sec);
    if (state->loc_uGridDensity != -1) glUniform1f(state->loc_uGridDensity, state->gridDensity);
    if (state->loc_uLineWidth != -1) glUniform1f(state->loc_uLineWidth, state->lineWidth);
    if (state->loc_uScrollSpeed != -1) glUniform1f(state->loc_uScrollSpeed, state->scrollSpeed);
    if (state->loc_uPerspectiveFactor != -1) glUniform1f(state->loc_uPerspectiveFactor, state->perspectiveFactor);
    if (state->loc_uHorizonY != -1) glUniform1f(state->loc_uHorizonY, state->horizonY);
    if (state->loc_uColorGrid != -1) glUniform3fv(state->loc_uColorGrid, 1, state->colorGrid);
    if (state->loc_uColorGround != -1) glUniform3fv(state->loc_uColorGround, 1, state->colorGround);
    if (state->loc_uColorSkyNear != -1) glUniform3fv(state->loc_uColorSkyNear, 1, state->colorSkyNear);
    if (state->loc_uColorSkyFar != -1) glUniform3fv(state->loc_uColorSkyFar, 1, state->colorSkyFar);
    if (state->loc_uFogDensity != -1) glUniform1f(state->loc_uFogDensity, state->fogDensity);
    // Для bool uniform'ов используем glUniform1i
    if (state->loc_uFogEnable != -1) glUniform1i(state->loc_uFogEnable, state->fogEnable);
    if (state->loc_uGridFogEnable != -1) glUniform1i(state->loc_uGridFogEnable, state->gridFogEnable);

    // --- Настройка атрибутов и отрисовка (как раньше) ---
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

    glDrawArrays(GL_TRIANGLES, 0, 6); // Рисуем квад

    if (pos_loc >= 0) glDisableVertexAttribArray(pos_loc);
    if (tex_loc >= 0) glDisableVertexAttribArray(tex_loc);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);

    return true;
}

// --- Обработка команд ---
static bool grid_handle_command(void* mode_state, const char* command) {
    GridModeState* state = (GridModeState*)mode_state;
    if (!state || !command) return false;

    // Простой парсинг команд "имя_параметра значение"
    // Для простоты используем sscanf. В реальном приложении может потребоваться более надежный парсер.
    float value1, value2, value3;
    int value_int;

    if (sscanf(command, "set_density %f", &value1) == 1) {
        state->gridDensity = value1 > 0 ? value1 : 0.1f; // Простая проверка
        printf("ModeGrid: Set gridDensity=%.2f\n", state->gridDensity); return true;
    } else if (sscanf(command, "set_linewidth %f", &value1) == 1) {
        state->lineWidth = value1 > 0 ? value1 : 0.1f;
        printf("ModeGrid: Set lineWidth=%.2f\n", state->lineWidth); return true;
    } else if (sscanf(command, "set_speed %f", &value1) == 1) {
        state->scrollSpeed = value1;
        printf("ModeGrid: Set scrollSpeed=%.2f\n", state->scrollSpeed); return true;
    } else if (sscanf(command, "set_perspective %f", &value1) == 1) {
        state->perspectiveFactor = value1 > 0 ? value1 : 0.1f;
        printf("ModeGrid: Set perspectiveFactor=%.2f\n", state->perspectiveFactor); return true;
    } else if (sscanf(command, "set_horizon %f", &value1) == 1) {
        state->horizonY = value1 >= 0 && value1 <= 1 ? value1 : 0.5f;
        printf("ModeGrid: Set horizonY=%.2f\n", state->horizonY); return true;
    } else if (sscanf(command, "set_fog_density %f", &value1) == 1) {
        state->fogDensity = value1 >= 0 ? value1 : 0.0f;
        printf("ModeGrid: Set fogDensity=%.2f\n", state->fogDensity); return true;
    } else if (sscanf(command, "set_fog_enable %d", &value_int) == 1) {
        state->fogEnable = (value_int == 1);
        printf("ModeGrid: Set fogEnable=%d\n", state->fogEnable); return true;
    } else if (sscanf(command, "set_grid_fog %d", &value_int) == 1) {
        state->gridFogEnable = (value_int == 1);
        printf("ModeGrid: Set gridFogEnable=%d\n", state->gridFogEnable); return true;
    } else if (sscanf(command, "set_color_grid %f %f %f", &value1, &value2, &value3) == 3) {
        state->colorGrid[0]=value1; state->colorGrid[1]=value2; state->colorGrid[2]=value3;
        printf("ModeGrid: Set colorGrid=(%.2f, %.2f, %.2f)\n", value1, value2, value3); return true;
    } else if (sscanf(command, "set_color_ground %f %f %f", &value1, &value2, &value3) == 3) {
        state->colorGround[0]=value1; state->colorGround[1]=value2; state->colorGround[2]=value3;
        printf("ModeGrid: Set colorGround=(%.2f, %.2f, %.2f)\n", value1, value2, value3); return true;
    } // ... Добавить команды для других цветов по аналогии ...

    // Пример команды для вывода текущих значений
    else if (strcmp(command, "get_params") == 0) {
         printf("--- ModeGrid Params ---\n");
         printf(" Density: %.2f\n", state->gridDensity);
         printf(" LineWidth: %.2f\n", state->lineWidth);
         printf(" Speed: %.2f\n", state->scrollSpeed);
         printf(" Perspective: %.2f\n", state->perspectiveFactor);
         printf(" Horizon: %.2f\n", state->horizonY);
         printf(" Fog Density: %.2f\n", state->fogDensity);
         printf(" Fog Enable: %d\n", state->fogEnable);
         printf(" Grid Fog Enable: %d\n", state->gridFogEnable);
         printf(" Color Grid: (%.2f, %.2f, %.2f)\n", state->colorGrid[0], state->colorGrid[1], state->colorGrid[2]);
         // ... Вывести другие цвета ...
         printf("-----------------------\n");
         return true;
    }

    return false; // Команда не распознана
}

// --- Resize ---
static void grid_resize(void* mode_state, int physical_width, int physical_height) {
    // Разрешение передается в render через params, здесь ничего не делаем
    (void)mode_state; (void)physical_width; (void)physical_height;
}

// --- Needs Redraw ---
static bool grid_needs_redraw(void* mode_state) {
    (void)mode_state;
    return true; // Анимация требует постоянной перерисовки
}

// --- Экземпляр интерфейса ---
const RenderModeInterface grid_mode_interface = {
    .init = grid_init,
    .cleanup = grid_cleanup,
    .render = grid_render,
    .handle_command = grid_handle_command,
    .resize = grid_resize,
    .needs_redraw = grid_needs_redraw,
};
