project('wallpaper', 'c',
  version: '1.0.0',
  default_options: ['warning_level=3', 'c_std=gnu17']
)

wayland = dependency('wayland-client')
cairo  = dependency('cairo')
inc = include_directories('protocol')
m_dep = meson.get_compiler('c').find_library('m', required : true)

sources = [
  'src/main.c',
  'src/ipc.c',
  'src/cairo_renderer.c',
  'src/wlr-layer-shell-protocol.c',
  'src/xdg-shell-protocol.c'
]

executable('wallpaper',
  sources,
  dependencies: [wayland, cairo, m_dep],
  include_directories: inc,
  install: true
)
