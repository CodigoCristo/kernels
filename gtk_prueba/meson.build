project(
  'sprite-bench',
  'c',
  version: '1.0.0',
  license: 'GPL-3.0-or-later',
  meson_version: '>= 1.0',
  default_options: [
    'c_std=gnu17',
    'warning_level=2',
    'buildtype=release',
  ],
)

# ─── Dependencias ────────────────────────────────────────────────
cc = meson.get_compiler('c')

gtk4_dep    = dependency('gtk4',         required: true)
adwaita_dep = dependency('libadwaita-1', required: true)
m_dep       = cc.find_library('m',       required: true)

deps = [gtk4_dep, adwaita_dep, m_dep]

# ─── Flags comunes ───────────────────────────────────────────────
common_flags = cc.get_supported_arguments([
  '-Wno-unused-parameter',
  '-Wno-missing-field-initializers',
])

# ─── Opción: nivel de optimización vectorial ─────────────────────
opt_level = get_option('vector_level')   # 'base' | 'v3' | 'v4'

if opt_level == 'v4'
  arch_flags = [
    '-march=x86-64-v4',
    '-O3',
    '-ffast-math',
    '-funroll-loops',
    '-ftree-vectorize',
    '-fopt-info-vec-optimized',
  ]
  message('▶ Vector level: x86-64-v4  (AVX-512F/BW/VL/DQ)')

elif opt_level == 'v3'
  arch_flags = [
    '-march=x86-64-v3',
    '-O3',
    '-ffast-math',
    '-funroll-loops',
    '-ftree-vectorize',
  ]
  message('▶ Vector level: x86-64-v3  (AVX2 + FMA)')

else
  arch_flags = ['-O2']
  message('▶ Vector level: base  (genérico, sin SIMD extendido)')
endif

# Filtrar solo los flags que el compilador actual acepta
supported_arch_flags = cc.get_supported_arguments(arch_flags)

# ─── Ejecutable ──────────────────────────────────────────────────
executable(
  'sprite-bench',
  sources: ['main.c'],
  dependencies: deps,
  c_args: common_flags + supported_arch_flags,
  install: true,
)

# ─── Resumen de configuración ────────────────────────────────────
summary({
  'Build type'   : get_option('buildtype'),
  'Vector level' : opt_level,
  'Arch flags'   : ' '.join(supported_arch_flags),
  'GTK4 version' : gtk4_dep.version(),
  'Adwaita ver'  : adwaita_dep.version(),
}, section: 'Sprite Bench')
