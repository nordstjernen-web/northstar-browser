project(
  'quickjs',
  'c',
  version: files('VERSION'),
  license: 'MIT',
  default_options: [
    'c_std=gnu11',
    'warning_level=0',
    'werror=false',
  ],
  meson_version: '>=1.0',
)

cc = meson.get_compiler('c')

quickjs_c_args = [
  '-D_GNU_SOURCE',
  '-DCONFIG_VERSION="@0@"'.format(meson.project_version()),
]
quickjs_c_args += cc.get_supported_arguments(['-fwrapv', '-w'])
if host_machine.system() == 'windows'
  quickjs_c_args += ['-D__USE_MINGW_ANSI_STDIO']
endif

quickjs_deps = [
  cc.find_library('m', required: false),
  dependency('threads'),
]

quickjs_lib = static_library(
  'quickjs',
  'quickjs.c',
  'dtoa.c',
  'libregexp.c',
  'libunicode.c',
  'cutils.c',
  c_args: quickjs_c_args,
  dependencies: quickjs_deps,
)

quickjs_dep = declare_dependency(
  link_with: quickjs_lib,
  include_directories: include_directories('.', is_system: true),
  dependencies: quickjs_deps,
)

meson.override_dependency('quickjs', quickjs_dep)
