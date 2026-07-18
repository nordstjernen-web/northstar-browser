project(
  'nordstjernen',
  ['c', 'cpp'],
  version: '1.0.20-dev',
  license: 'LicenseRef-NSL-1.0',
  default_options: [
    'c_std=gnu2x,c2x,gnu11,c11',
    'cpp_std=c++17',
    'warning_level=2',
    'quickjs-ng:warning_level=0',
    'buildtype=release',
    'optimization=3',
    'b_ndebug=true',
    'b_pie=true',
  ],
  meson_version: '>=1.0',
)

cc = meson.get_compiler('c')

common_warning_flags = [
  '-Wshadow',
  '-Wpointer-arith',
  '-Wstrict-prototypes',
  '-Wmissing-prototypes',
  '-Wmissing-declarations',
  '-Wold-style-definition',
  '-Wcast-align',
  '-Wwrite-strings',
  '-Wformat=2',
  '-Wformat-security',
  '-Wnull-dereference',
  '-Wlogical-op',
  '-Wundef',
  '-Wvla',
  '-Winit-self',
  '-Wredundant-decls',
]

suppressed_warnings = [
  '-Wno-constant-logical-operand',
]

fortify_level = '-D_FORTIFY_SOURCE=2'
if cc.has_argument('-D_FORTIFY_SOURCE=3')
  fortify_level = '-D_FORTIFY_SOURCE=3'
endif

hardening_c_flags = [
  '-fstack-protector-strong',
  '-fstack-clash-protection',
  '-fcf-protection=full',
  '-U_FORTIFY_SOURCE',
  fortify_level,
]

hardening_link_flags = [
  '-Wl,-z,relro',
  '-Wl,-z,now',
  '-Wl,-z,noexecstack',
  '-Wl,-z,separate-code',
]

add_project_arguments(
  cc.get_supported_arguments(common_warning_flags + suppressed_warnings +
                             hardening_c_flags),
  language: 'c',
)

add_project_link_arguments(
  cc.get_supported_link_arguments(hardening_link_flags),
  language: 'c',
)

ns_buildtype = get_option('buildtype')
ns_is_release = (ns_buildtype == 'release' or ns_buildtype == 'minsize')
# Every build profile gets -O3 (via default_options optimization=3) plus
# dead-section pruning. Nordstjernen leans on the optimizer to inline the
# lexbor parser walks, QuickJS interpreter dispatch, and our box-layout
# loops; -O2 left ~30% on the table for JS-heavy pages. The -O level is
# left to meson's optimization option so it isn't passed twice (which
# warns); we only add the section-GC flags here. Dev/non-release builds
# keep -g3 + frame pointers so step debugging still works under -O3.
add_project_arguments(
  cc.get_supported_arguments(['-ffunction-sections', '-fdata-sections']),
  language: 'c',
)
add_project_link_arguments(
  cc.get_supported_link_arguments(['-Wl,--gc-sections']),
  language: 'c',
)
if ns_is_release
  add_project_link_arguments(
    cc.get_supported_link_arguments(['-Wl,-s']),
    language: 'c',
  )
else
  add_project_arguments(
    cc.get_supported_arguments(['-g3', '-fno-omit-frame-pointer']),
    language: 'c',
  )
endif
message('nordstjernen build profile: ' +
        (ns_is_release ? 'release (LTO + stripped + hardened)'
                       : 'development (debug symbols + frame pointer + hardened)'))

# The GTK shell is a feature: disabling it yields a renderer-only build that
# needs no GTK 4 at all. is_mobile is retained as a constant (the Android and
# iOS mobile targets have been removed) so the desktop-only guards below read
# unchanged.
is_mobile = false
gtk_feature = get_option('gtk')

glib       = dependency('glib-2.0')
gobject    = dependency('gobject-2.0')
gio        = dependency('gio-2.0')
cairo      = dependency('cairo')
pango      = dependency('pango')
pangocairo = dependency('pangocairo')
gdkpixbuf  = dependency('gdk-pixbuf-2.0')

have_gtk = false
if not is_mobile and gtk_feature.allowed()
  gtk4_min = host_machine.system() == 'windows' ? '>=4.22.1' : '>=4.14'
  gtk4 = dependency('gtk4', version: gtk4_min, required: gtk_feature)
  if gtk4.found()
    have_gtk = true
    gtk4_dep = gtk4
    if host_machine.system() == 'windows'
      message('GTK @0@ — Windows requires the MSYS2 stock GTK (>= 4.22.1).'.format(gtk4.version()))
    elif gtk4.version().version_compare('>=4.22')
      message('GTK @0@ — preferred (>= 4.22).'.format(gtk4.version()))
    else
      warning('GTK @0@ meets the 4.14 minimum, but Nordstjernen prefers >= 4.22 for the newer GSK renderers.'.format(gtk4.version()))
    endif
  endif
endif
gmodule = dependency('gmodule-2.0')
libcurl = dependency('libcurl', version: '>=8.5')
if libcurl.version().version_compare('<8.11')
  warning('libcurl @0@ predates stable WebSocket support (curl 8.11): WebSocket will be unavailable at runtime unless this libcurl was built with the ws/wss protocols.'.format(libcurl.version()))
endif
libcrypto = dependency('libcrypto')
libm = cc.find_library('m', required: false)

libuchardet = dependency('uchardet')

libpsl = dependency('libpsl')

sqlite3 = dependency('sqlite3')

# gdk-pixbuf (image fallback decoder) and librsvg (SVG) are desktop-only; on
# Android, Wuffs handles PNG/JPEG/GIF/BMP and other formats degrade gracefully.
librsvg = dependency('librsvg-2.0', version: '>=2.54', required: not is_mobile)
have_librsvg = librsvg.found() and not is_mobile
if have_librsvg
  add_project_arguments('-DNS_HAVE_LIBRSVG=1', language: 'c')
endif

have_gdk_pixbuf = not is_mobile
if have_gdk_pixbuf
  add_project_arguments('-DNS_HAVE_GDK_PIXBUF=1', language: 'c')
endif

libfontconfig = dependency('fontconfig', required: false)
have_fontconfig = libfontconfig.found()
if have_fontconfig
  add_project_arguments('-DNS_HAVE_FONTCONFIG=1', language: 'c')
endif
libpangoft2 = dependency('pangoft2', required: false)
have_pangoft2 = libpangoft2.found()
if have_pangoft2
  add_project_arguments('-DNS_HAVE_PANGOFT2=1', language: 'c')
endif

libfreetype = dependency('freetype2', required: false)
have_freetype = libfreetype.found()
if have_freetype
  add_project_arguments('-DNS_HAVE_FREETYPE=1', language: 'c')
endif


libavif = dependency('libavif', required: not is_mobile)
have_avif = libavif.found() and not is_mobile
if have_avif
  add_project_arguments('-DNS_HAVE_AVIF=1', language: 'c')
endif

# vorbisfile is the ov_* high-level decode API over libvorbis, used by the audio
# helper (src/audio/main.c) to decode Ogg/Vorbis natively where FFmpeg is absent.
libvorbisfile = dependency('vorbisfile', required: false)
if libvorbisfile.found()
  add_project_arguments('-DNS_HAVE_VORBISFILE=1', language: 'c')
endif
# opusfile is the high-level op_* decode API over libopus + libogg, used by the
# audio helper to decode Ogg/Opus natively where FFmpeg is absent.
libopusfile = dependency('opusfile', required: false)
if libopusfile.found()
  add_project_arguments('-DNS_HAVE_OPUSFILE=1', language: 'c')
endif
# canPlayType/mediaCapabilities may honestly advertise native Ogg audio only
# where the decoder that plays it is built: the desktop (SDL2) nordstjernen-audio
# helper. On mobile audio takes a different path, so these are gated off there.
if not is_mobile and get_option('audio').allowed()
  sdl2_probe = dependency('sdl2', required: false)
  if sdl2_probe.found() and libvorbisfile.found()
    add_project_arguments('-DNS_AUDIO_NATIVE_VORBIS=1', language: 'c')
  endif
  if sdl2_probe.found() and libopusfile.found()
    add_project_arguments('-DNS_AUDIO_NATIVE_OPUS=1', language: 'c')
  endif
endif
libenchant = dependency('enchant-2', required: false)
have_enchant = libenchant.found() and not is_mobile
if have_enchant
  add_project_arguments('-DNS_HAVE_ENCHANT=1', language: 'c')
endif

libseccomp = dependency(
  'libseccomp',
  required: host_machine.system() == 'linux',
)
have_seccomp = libseccomp.found() and not is_mobile
if have_seccomp
  add_project_arguments('-DNS_HAVE_SECCOMP=1', language: 'c')
endif

libwuffs = dependency('wuffs', fallback: ['wuffs', 'wuffs_dep'])
libplmpeg = dependency('plmpeg', fallback: ['plmpeg', 'plmpeg_dep'])
zlib = dependency('zlib')

lexbor_version = '3.0.0'
add_project_arguments('-DNS_LEXBOR_VERSION="' + lexbor_version + '"',
                      language: 'c')

build_date = get_option('build_date')
if build_date == ''
  date_cmd = run_command('date', '-u', '+%Y-%m-%d', check: false)
  if date_cmd.returncode() == 0
    build_date = date_cmd.stdout().strip()
  endif
endif
add_project_arguments('-DNS_BUILD_DATE="' + build_date + '"', language: 'c')
add_project_arguments('-DNS_VERSION="' + meson.project_version() + '"', language: 'c')

cmake = import('cmake')
lexbor_opts = cmake.subproject_options()
lexbor_opts.add_cmake_defines({
  'LEXBOR_BUILD_SHARED': false,
  'LEXBOR_BUILD_STATIC': true,
  'LEXBOR_BUILD_TESTS': false,
  'LEXBOR_BUILD_EXAMPLES': false,
  'LEXBOR_BUILD_UTILS': false,
  'LEXBOR_BUILD_SEPARATELY': false,
  'CMAKE_POSITION_INDEPENDENT_CODE': true,
})
lexbor_opts.append_compile_args('c', '-w')
lexbor_subproject = cmake.subproject('lexbor', options: lexbor_opts)
liblexbor = lexbor_subproject.dependency('lexbor_static').as_system('system')

libquickjs = dependency('quickjs-ng', fallback: ['quickjs-ng', 'qjs_dep'])

wasm_unsupported_cpu = host_machine.cpu_family() == 'x86'
if get_option('wasm').enabled() and wasm_unsupported_cpu
  error('wasm (WAMR) is not supported on 32-bit x86; build with -Dwasm=disabled')
endif
wasm_feature = get_option('wasm').disable_auto_if(wasm_unsupported_cpu)
have_wasm = false
if wasm_feature.allowed()
  subdir('src/wamr')
  have_wasm = true
else
  libwamr = declare_dependency()
endif

install_data(
  'data/icons/hicolor/scalable/apps/nordstjernen.svg',
  'data/icons/hicolor/scalable/apps/nordstjernen.gif',
  'data/icons/hicolor/scalable/apps/nordstjernen-back.svg',
  'data/icons/hicolor/scalable/apps/nordstjernen-forward.svg',
  'data/icons/hicolor/scalable/apps/nordstjernen-home.svg',
  'data/icons/hicolor/scalable/apps/nordstjernen-reload.svg',
  'data/icons/hicolor/scalable/apps/nordstjernen-stop.svg',
  'data/icons/hicolor/scalable/apps/nordstjernen-go.svg',
  'data/icons/hicolor/scalable/apps/nordstjernen-bookmarks.svg',
  'data/icons/hicolor/scalable/apps/nordstjernen-console.svg',
  'data/icons/hicolor/scalable/apps/nordstjernen-settings.svg',
  'data/icons/hicolor/scalable/apps/nordstjernen-about.svg',
  'data/icons/hicolor/scalable/apps/nordstjernen-stage-connect.svg',
  'data/icons/hicolor/scalable/apps/nordstjernen-stage-fetch.svg',
  'data/icons/hicolor/scalable/apps/nordstjernen-stage-parse.svg',
  'data/icons/hicolor/scalable/apps/nordstjernen-stage-script.svg',
  'data/icons/hicolor/scalable/apps/nordstjernen-stage-render.svg',
  'data/icons/hicolor/scalable/apps/nordstjernen-stage-done.svg',
  install_dir: get_option('datadir') / 'icons' / 'hicolor' / 'scalable' / 'apps',
)

install_data(
  'License.md',
  'THIRD-PARTY-LICENSES.md',
  install_dir: get_option('datadir') / 'nordstjernen',
)

install_data(
  'data/safebrowsing.list',
  install_dir: get_option('datadir') / 'nordstjernen',
)

install_data(
  'data/i18n/am.lang',
  'data/i18n/ar.lang',
  'data/i18n/bn.lang',
  'data/i18n/de.lang',
  'data/i18n/el.lang',
  'data/i18n/en.lang',
  'data/i18n/es.lang',
  'data/i18n/fa.lang',
  'data/i18n/fr.lang',
  'data/i18n/gu.lang',
  'data/i18n/ha.lang',
  'data/i18n/hi.lang',
  'data/i18n/hu.lang',
  'data/i18n/id.lang',
  'data/i18n/it.lang',
  'data/i18n/ja.lang',
  'data/i18n/jv.lang',
  'data/i18n/kn.lang',
  'data/i18n/ko.lang',
  'data/i18n/ml.lang',
  'data/i18n/mr.lang',
  'data/i18n/my.lang',
  'data/i18n/nl.lang',
  'data/i18n/pa.lang',
  'data/i18n/pl.lang',
  'data/i18n/pt.lang',
  'data/i18n/ro.lang',
  'data/i18n/ru.lang',
  'data/i18n/su.lang',
  'data/i18n/sw.lang',
  'data/i18n/ta.lang',
  'data/i18n/te.lang',
  'data/i18n/th.lang',
  'data/i18n/tl.lang',
  'data/i18n/tr.lang',
  'data/i18n/uk.lang',
  'data/i18n/ur.lang',
  'data/i18n/vi.lang',
  'data/i18n/yo.lang',
  'data/i18n/zh.lang',
  install_dir: get_option('datadir') / 'nordstjernen' / 'i18n',
)

install_data(
  'data/nordstjernen.desktop',
  install_dir: get_option('datadir') / 'applications',
  rename: 'org.nordstjernen.WebBrowser.desktop',
)

install_data(
  'data/org.nordstjernen.WebBrowser.metainfo.xml',
  install_dir: get_option('datadir') / 'metainfo',
)

ai_feature = get_option('ai')
ai_gpu_feature = get_option('ai_gpu')
ai_deps = []
have_ai = false
ai_gpu_backend = 'none'
if not is_mobile and ai_feature.allowed()
  cmake = import('cmake')

  use_metal = false
  use_vulkan = false
  vulkan_dep = dependency('', required: false)
  if ai_gpu_feature.allowed()
    if host_machine.system() == 'darwin'
      use_metal = true
    else
      vulkan_dep = dependency('vulkan', required: false)
      glsl_compiler = find_program('glslc', 'glslangValidator', required: false)
      spirv_headers = dependency('SPIRV-Headers', method: 'cmake', required: false)
      if vulkan_dep.found() and glsl_compiler.found() and spirv_headers.found()
        use_vulkan = true
      endif
    endif
  endif
  if ai_gpu_feature.enabled() and not (use_metal or use_vulkan)
    error('ai_gpu was requested but no GPU toolchain (Metal/Vulkan + glslc) was found.')
  endif

  llama_opts = cmake.subproject_options()
  llama_opts.add_cmake_defines({
    'BUILD_SHARED_LIBS': false,
    'LLAMA_CURL': false,
    'GGML_NATIVE': false,
    'GGML_OPENMP': false,
    'GGML_METAL': use_metal,
    'GGML_METAL_EMBED_LIBRARY': false,
    'GGML_VULKAN': use_vulkan,
    'GGML_ACCELERATE': false,
    'GGML_BLAS': false,
    'LLAMA_BUILD_TESTS': false,
    'LLAMA_BUILD_EXAMPLES': false,
    'LLAMA_BUILD_SERVER': false,
    'LLAMA_BUILD_TOOLS': false,
    'CMAKE_POSITION_INDEPENDENT_CODE': true,
  })
  llama_opts.set_install(false)
  llama_sp = cmake.subproject('llama.cpp', options: llama_opts,
                              required: ai_feature)
  if llama_sp.found()
    ai_deps = [
      declare_dependency(compile_args: ['-DNS_HAVE_AI=1']),
      llama_sp.dependency('llama'),
      llama_sp.dependency('ggml'),
      llama_sp.dependency('ggml-cpu'),
      llama_sp.dependency('ggml-base'),
    ]
    if use_metal
      ai_deps += llama_sp.dependency('ggml-metal')
      ai_gpu_backend = 'metal'
    endif
    if use_vulkan
      ai_deps += llama_sp.dependency('ggml-vulkan')
      ai_deps += vulkan_dep
      ai_gpu_backend = 'vulkan'
    endif
    have_ai = true
  endif
endif
summary({'WebAssembly (WAMR)': have_wasm}, section: 'Features')
summary({'AI chat': have_ai, 'AI GPU backend': ai_gpu_backend}, section: 'Features')
summary({'GTK 4 shell': have_gtk}, section: 'Shells')

subdir('src')
