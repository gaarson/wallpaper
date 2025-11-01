project('wallpaper', 'c',
  version: '1.0.0',
  default_options: ['warning_level=3', 'c_std=gnu17']
)

# БЛОК OPTION(...) УДАЛЕН ОТСЮДА

# wl_client_dep = dependency('wayland-client', version : '>=1.18')
wl_egl_dep = dependency('wayland-egl') # Нужен для wl_egl_window
egl_dep = dependency('egl')       # EGL API
glesv2_dep = dependency('glesv2')

wayland = dependency('wayland-client')
cairo  = dependency('cairo')
m_dep = meson.get_compiler('c').find_library('m', required : true)

inc = include_directories('src/renderer/shaders')

# --- Логика флага отладки ---
# Эта часть ОСТАЕТСЯ. Она *использует* опцию из meson_options.txt
if get_option('debug_logs')
  # Если true, добавляем флаг -DENABLE_DEBUG_LOG к компилятору
  add_project_arguments('-DENABLE_DEBUG_LOG', language : 'c')
endif
# ---

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

  'src/renderer/core.c',

  'src/hash_table.c',
  'src/shader_utils.c',
  'src/buffer.c',
  'src/output.c',
  'src/wayland_setup.c',
  'src/timer.c',
  'src/ipc.c',
  'src/wlr-layer-shell-protocol.c',
  'src/xdg-shell-protocol.c',
  'src/viewporter-protocol.c',
  'src/fractional-scale-v1-protocol.c',
  'src/xdg-output-unstable-v1-client-protocol.c',
]

executable('wallpaper',
  sources,
  dependencies: [wayland, cairo, m_dep, wl_egl_dep, egl_dep, glesv2_dep],
  include_directories: inc,
  install: true
)
