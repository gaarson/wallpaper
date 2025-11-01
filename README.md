# Animated Wayland Wallpaper

This is an advanced, shader-based animated wallpaper application for Wayland compositors that support the `wlr-layer-shell` protocol. It creates a background surface for each monitor and renders dynamic content using OpenGL ES 3.0.

## Features

* **Shader-Based Rendering:** All visuals are rendered on the GPU using GLES 3.0 shaders for high performance.
* **Dynamic Render Modes:** Switch between different visualizers (e.g., static images, gradients, shader toy-style animations) at runtime.
* **Live Configuration:** Control the wallpaper by writing commands to a file. The application watches this file and reloads instantly.
* **IPC Control:** Send modifier commands (e.g., "faster", "slower", "set_color") to the running instance via a UNIX socket for real-time animation control.
* **Multi-Monitor Support:** Correctly handles multiple displays, creating a separate wallpaper for each.
* **High-DPI & Fractional Scaling:** Uses `wp_viewporter` and `wp_fractional_scale_manager` to render crisply on any display.

---

## 1. Dependencies

You must have the following `pkg-config` libraries installed to build the project:

* `meson` and `ninja` (for building)
* `wayland-client`
* `wayland-egl`
* `egl`
* `glesv2` (Note: The project uses GLES 3.0, but the `glesv2` package often provides the headers/libs)
* `cairo` (Used for SHM buffer stride calculations)

On a Debian/Devuan-based system, you can install them with:

```bash
sudo apt install meson ninja-build libwayland-dev libwayland-egl-backend-dev \
                 libegl-dev libgles2-dev libcairo2-dev
````

-----

## 2\. Build Instructions

### Standard Build

1.  Clone this repository.
2.  Set up the build directory with Meson:
    ```bash
    meson setup build
    ```
3.  Compile the project with Ninja:
    ```bash
    meson compile -C build
    ```
    The final executable will be located at `build/wallpaper`.

### Debug Build

To compile with debug logging enabled (which will print `log_debug` messages to `stdout`), configure Meson with the `debug_logs` option set to `true`:

```bash
# To set up a new build directory with debug logs
meson setup build -Ddebug_logs=true

# Or, to re-configure an existing build directory
meson configure build -Ddebug_logs=true
```

Then, compile as usual: `meson compile -C build`.

-----

## 3\. Usage & Configuration

There are two primary ways to control the wallpaper after it's running.

### Method 1: Setting the Mode (Config File)

This is the main way to **set or change the entire scene**.

The application watches a file in your runtime directory:
`$XDG_RUNTIME_DIR/wallpaper_command.txt`

You can change the wallpaper at any time by writing a new command into this file.

**Example:**

```bash
# Set a static dark purple color
echo "color #2a0033" > $XDG_RUNTIME_DIR/wallpaper_command.txt

# Change to the animated grid shader
echo "grid" > $XDG_RUNTIME_DIR/wallpaper_command.txt

# Show a static image (full path required)
echo "texture /home/ruser/pictures/my-wallpaper.png" > $XDG_RUNTIME_DIR/wallpaper_command.txt

# Switch to the starfield
echo "starfield" > $XDG_RUNTIME_DIR/wallpaper_command.txt
```

### Method 2: Modifying the Mode (IPC Socket)

This method is for **sending commands to the *currently active* mode**. This is useful for changing animation parameters without fully reloading the scene.

The application listens on a UNIX socket:
`$XDG_RUNTIME_DIR/wallpaper_control.sock`

You can send newline-separated commands to this socket using tools like `socat` or `ncat`.

**Example:**
(Assuming the `starfield` mode is already active)

```bash
# Make the starfield animation faster
echo "faster" | socat - $XDG_RUNTIME_DIR/wallpaper_control.sock

# Reset the camera position
echo "reset_camera" | socat - $XDG_RUNTIME_DIR/wallpaper_control.sock

# Set the number of stars
echo "set_star_count 5000" | socat - $XDG_RUNTIME_DIR/wallpaper_control.sock
```

This command will be ignored if the active mode (e.g., `texture`) doesn't understand the command "faster".

-----

## 4\. Available Render Modes

Commands are sent via the **config file** (`wallpaper_command.txt`).

| Mode Key | Argument | Description |
| :--- | :--- | :--- |
| `default` | (none) | Displays a solid black screen. |
| `color` | `#RRGGBB` or `#RGB` | Displays a solid static color. |
| `gradient` | (none) | Displays a simple, animated vertical gradient. |
| `texture` | `/full/path/to/image.png` | Displays a static image. (JPG, PNG, etc. supported). |
| `grid` | (none) | Renders a "synthwave" style animated 3D grid shader. |
| `starfield` | (none) | Renders an animated 3D "warp speed" starfield with a nebula. |

### Mode-Specific IPC Commands

When an animated mode is active, you can send it commands via the **IPC socket** (`wallpaper_control.sock`).

#### 🎨 `gradient` Commands

  * `faster`: Increases animation speed.
  * `slower`: Decreases animation speed.

#### 🏁 `grid` Commands

  * `set_density <float>`: e.g., `set_density 4.5`
  * `set_linewidth <float>`: e.g., `set_linewidth 1.5`
  * `set_speed <float>`: e.g., `set_speed 0.2`
  * `set_perspective <float>`: e.g., `set_perspective 0.1`
  * `set_horizon <float>`: (0.0 to 1.0) e.g., `set_horizon 0.45`
  * `set_fog_density <float>`: e.g., `set_fog_density 0.002`
  * `set_fog_enable <0 or 1>`: e.g., `set_fog_enable 1`
  * `set_grid_fog <0 or 1>`: e.g., `set_grid_fog 1`
  * `set_color_grid <r> <g> <b>`: (0.0 to 1.0) e.g., `set_color_grid 0.0 0.8 1.0`
  * `set_color_ground <r> <g> <b>`
  * `set_color_sky_near <r> <g> <b>`
  * `set_color_sky_far <r> <g> <b>`

#### 🌟 `starfield` Commands

  * `faster`: Increases travel speed.
  * `slower`: Decreases travel speed.
  * `set_speed <float>`: Sets travel speed multiplier.
  * `set_star_count <int>`: Re-generates stars, e.g., `set_star_count 20000`
  * `set_star_size <float>`: Base size multiplier.
  * `set_star_brightness <float>`: Base brightness multiplier.
  * `set_tail_probability <float>`: (0.0 to 1.0) Chance for a star to be a particle tail.
  * `set_nebula_brightness <float>`
  * `set_nebula_density <float>`
  * `set_nebula_color1 <r> <g> <b>`
  * `set_nebula_color2 <r> <g> <b>`
  * `set_lens_spawn_rate <float>`: (Hz) How often "lenses" spawn, e.g., `set_lens_spawn_rate 0.5`
  * `set_lens_params <radius> <strength> <ttl>`: e.g., `set_lens_params 40.0 2.5 40.0`
  * `set_lens_fade_times <in> <out>`: (seconds) e.g., `set_lens_fade_times 2.0 2.0`
  * `reset_camera`: Resets camera to origin and clears FBOs.

-----

## 5\. How It Works (Architecture)

1.  **Main:** The `main.c` file sets up a `poll()` loop to listen on multiple file descriptors.
2.  **Wayland Setup:** Connects to the Wayland display, binds global interfaces like `wl_compositor`, `zwlr_layer_shell_v1`, `wp_viewporter`, and `wp_fractional_scale_manager_v1`.
3.  **Output Management:** `output.c` listens for `wl_output` globals. For each monitor, it creates a `wl_surface` and a `zwlr_layer_surface_v1` (a layer shell surface).
4.  **Renderer Core:** `renderer/core.c` is instantiated *for each output*. It manages the EGL context, GLES3 state, and a hash table (`mode_registry`) of available render modes (like `grid`, `texture`, etc.).
5.  **Config Monitor:** `config_monitor.c` uses `inotify` to watch `$XDG_RUNTIME_DIR/wallpaper_command.txt`. When this file is modified, it reads the new command (e.g., "grid") and calls `renderer_core_set_mode()` on all outputs. This function cleans up the old mode and initializes the new one.
6.  **IPC Server:** `ipc.c` opens a UNIX domain socket at `$XDG_RUNTIME_DIR/wallpaper_control.sock`. When a client connects and sends a command (e.g., "faster"), it calls `renderer_core_handle_command()`, which passes the string to the *currently active* render mode's `handle_command` function.
7.  **Animation Timer:** `timer.c` creates a `timerfd` set to a high framerate (e.g., 60 FPS). This timer is only armed if the active mode reports that it `needs_redraw`. When the timer fires, the main loop calls `present_output_frame()` for all animated outputs, which triggers `renderer_core_render_frame()`.

## Credits

  * Uses the [stb\_image.h](https://www.google.com/search?q=https://github.com/nothings/stb/blob/master/stb_image.h) single-file public domain library for image loading.

<!-- end list -->

## 6\. How to Add a New Render Mode (for Developers)

The application is built around a modular "Render Mode" interface. Adding a new visualizer is a straightforward process that involves four main steps:

1.  **Create** your GLSL shaders.
2.  **Implement** the `RenderModeInterface` in a new `.c` file.
3.  **Register** your new mode in the `renderer/core.c` factory.
4.  **Add** your new `.c` file to the `meson.build` file.

Here is a more detailed guide, using a new mode called `"vortex"` as an example.

### Step 1: Create the Shaders

First, create your vertex and fragment shaders.

#### `src/renderer/shaders/vortex.vert`

The vertex shader is almost always the same. It just needs to pass the vertex positions and texture coordinates through.

```glsl
#version 300 es
precision highp float;

// Input attributes from the common VBO
in vec4 aPosition; // Vertex coords (-1..1)
in vec2 aTexCoord; // Texture coords (0..1)

// Output to fragment shader
out vec2 vTexCoord_out;

void main() {
    gl_Position = aPosition;      // Pass position to GL
    vTexCoord_out = aTexCoord;  // Pass UVs to fragment shader
}
```

#### `src/renderer/shaders/vortex.frag`

This is where your creative logic lives. The fragment shader receives the UV coordinates (`vTexCoord_out`) which range from (0,0) at the bottom-left to (1,1) at the top-right.

You can also add `uniform` variables to be controlled from your C code.

```glsl
#version 300 es
precision highp float;

// Input from vertex shader
in vec2 vTexCoord_out;

// Uniforms you will control from C
uniform float uTime;       // Current animation time in seconds
uniform vec2 uResolution; // Physical resolution of the screen

// Final output color
out vec4 FragColor;

void main() {
    // Center the coordinates
    vec2 uv = vTexCoord_out.xy - 0.5;
    uv.x *= uResolution.x / uResolution.y; // Correct for aspect ratio

    float angle = atan(uv.y, uv.x) + uTime * 0.5;
    float radius = length(uv);
    
    float r = 0.5 + 0.5 * sin(angle * 10.0 - radius * 5.0);
    float g = 0.5 + 0.5 * cos(angle * 5.0);
    float b = 0.5;

    FragColor = vec4(r, g, b, 1.0);
}
```

### Step 2: Implement the Mode Interface

Create two new files: `src/renderer/vortex.h` and `src/renderer/vortex.c`.

#### `src/renderer/vortex.h`

This file just exposes the interface variable to the rest of the program.

```c
// src/renderer/vortex.h
#ifndef MODE_VORTEX_H
#define MODE_VORTEX_H

#include "mode.h"

// Expose the interface implementation
extern const RenderModeInterface vortex_mode_interface;

#endif // MODE_VORTEX_H
```

#### `src/renderer/vortex.c`

This is the core logic. You implement the six functions required by the interface.

```c
// src/renderer/vortex.c
#include "common.h" // <--- IMPORTANT: Include for log_debug!
#include "vortex.h"
#include "./../shader_utils.h" // For create_program_from_files
#include <stdlib.h>
#include <GLES3/gl3.h>

// 1. Define shader paths
#define VORTEX_VERTEX_SHADER "src/renderer/shaders/vortex.vert"
#define VORTEX_FRAGMENT_SHADER "src/renderer/shaders/vortex.frag"

// 2. Define the mode's private state struct
typedef struct {
    GLuint shader_program;
    GLint loc_uTime;
    GLint loc_uResolution;
} VortexModeState;

// 3. Implement the interface functions (as static)

static void* vortex_init(const char* arg, GLuint common_vbo) {
    (void)arg; (void)common_vbo;
    log_debug("ModeVortex: Initializing...\n");

    VortexModeState* state = calloc(1, sizeof(VortexModeState));
    if (!state) {
        log_error("ModeVortex: calloc state failed\n");
        return NULL;
    }

    state->shader_program = create_program_from_files(
        VORTEX_VERTEX_SHADER,
        VORTEX_FRAGMENT_SHADER
    );

    if (!state->shader_program) {
        log_error("ModeVortex: Failed to create shader program.\n");
        free(state);
        return NULL;
    }

    // Get uniform locations
    state->loc_uTime = glGetUniformLocation(state->shader_program, "uTime");
    state->loc_uResolution = glGetUniformLocation(state->shader_program, "uResolution");

    log_debug("ModeVortex: Initialized (Program ID: %u).\n", state->shader_program);
    return state;
}

static void vortex_cleanup(void* mode_state) {
    VortexModeState* state = (VortexModeState*)mode_state;
    if (!state) return;
    log_debug("ModeVortex: Cleaning up...\n");
    if (state->shader_program) {
        glDeleteProgram(state->shader_program);
    }
    free(state);
}

static bool vortex_render(void* mode_state, const RenderParams* params) {
    VortexModeState* state = (VortexModeState*)mode_state;
    if (!state || !state->shader_program) return false;

    glUseProgram(state->shader_program);

    // Set uniforms
    if (state->loc_uTime != -1) {
        // params->time_ms is uint32_t, convert to float seconds
        glUniform1f(state->loc_uTime, (float)params->time_ms / 1000.0f);
    }
    if (state->loc_uResolution != -1) {
        glUniform2f(state->loc_uResolution,
            (GLfloat)params->physical_width,
            (GLfloat)params->physical_height
        );
    }

    // Bind the common VBO
    glBindBuffer(GL_ARRAY_BUFFER, params->common_vbo);
    
    // Set vertex attributes (must match common quad layout)
    GLint pos_loc = glGetAttribLocation(state->shader_program, "aPosition");
    GLint tex_loc = glGetAttribLocation(state->shader_program, "aTexCoord");
    const GLsizei stride = 4 * sizeof(GLfloat); // (pos(2) + tex(2))

    if (pos_loc >= 0) {
        glVertexAttribPointer(pos_loc, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
        glEnableVertexAttribArray(pos_loc);
    }
    if (tex_loc >= 0) {
        glVertexAttribPointer(tex_loc, 2, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(GLfloat)));
        glEnableVertexAttribArray(tex_loc);
    }

    // Draw the quad
    glDrawArrays(GL_TRIANGLES, 0, 6);

    // Clean up
    if (pos_loc >= 0) glDisableVertexAttribArray(pos_loc);
    if (tex_loc >= 0) glDisableVertexAttribArray(tex_loc);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);

    return true;
}

static bool vortex_handle_command(void* mode_state, const char* command) {
    // This mode doesn't handle IPC commands, so just return false.
    (void)mode_state; (void)command;
    return false;
}

static void vortex_resize(void* mode_state, int w, int h) {
    // The resolution is passed in `vortex_render`, so nothing to do here.
    (void)mode_state; (void)w; (void)h;
}

static bool vortex_needs_redraw(void* mode_state) {
    // It's animated (uses uTime), so it always needs to be redrawn.
    (void)mode_state;
    return true;
}

// 4. Define the public interface instance
const RenderModeInterface vortex_mode_interface = {
    .init = vortex_init,
    .cleanup = vortex_cleanup,
    .render = vortex_render,
    .handle_command = vortex_handle_command,
    .resize = vortex_resize,
    .needs_redraw = vortex_needs_redraw,
};
```

### Step 3: Register the New Mode

Open `src/renderer/core.c` and add your mode to the `populate_mode_registry` function.

```c
// src/renderer/core.c

// ... other includes
#include "grid.h"
#include "starfield.h"
#include "vortex.h" // <--- ADD THIS INCLUDE

// ...

static bool populate_mode_registry(RendererCoreState* state) {
    if (!state || !state->mode_registry) return false;
    log_debug("Core: Populating mode registry...\n");
    bool success = true;

    // ... other ht_insert calls
    success &= ht_insert(state->mode_registry, TEXTURE_MODE_KEY, &texture_mode_interface);
    success &= ht_insert(state->mode_registry, STARFIELD_MODE_KEY, &starfield_mode_interface);
    
    // v-- ADD THIS LINE --v
    success &= ht_insert(state->mode_registry, "vortex", &vortex_mode_interface);
    // ^-- ADD THIS LINE --^

    if (!success) {
        log_error("Core Error: Failed to insert one or more modes into registry!\n");
    } else {
        log_debug("Core: Mode registry populated.\n");
    }
    return success;
}
```

Now, you can activate your mode by running:
`echo "vortex" > $XDG_RUNTIME_DIR/wallpaper_command.txt`

### Step 4: Add to the Build System

Finally, open `meson.build` and add your new `.c` file to the `sources` list.

```meson
# meson.build
# ...

sources = [
  'src/main.c',
  'src/config_monitor.c',

  # mods
  'src/renderer/grid.c',
  'src/renderer/color.c',
  'src/renderer/default.c',
  'src/renderer/gradient.c',
  'src/renderer/texture.c',
  'src/renderer/starfield.c',
  'src/renderer/vortex.c', # <--- ADD THIS LINE

  'src/renderer/core.c',
  # ... rest of the files
]

# ...
```

Now, just re-compile with `meson compile -C build`, and your new mode will be included.
