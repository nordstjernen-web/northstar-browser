python = import('python').find_installation()

qjsc_prog = find_program('qjsc', required: false)
qjs_prog  = find_program('qjs',  required: false)

if not meson.is_cross_build() and qjsc_prog.found() and qjs_prog.found()
  # files() so the script path is absolute — a raw '../scripts/...' string would
  # be resolved relative to the build dir and break out-of-tree builds.
  verify_polyfills_py = files('../scripts/verify-polyfills.py')
  polyfills_verify = custom_target(
    'polyfills-verify',
    input: ['../data/js/polyfills.js', '../scripts/verify-polyfills.js'],
    output: 'polyfills.verified',
    command: [
      python, verify_polyfills_py,
      qjsc_prog, qjs_prog, '@INPUT0@', '@INPUT1@', '@OUTPUT@',
    ],
    build_by_default: true,
  )
endif

polyfills_h = custom_target(
  'polyfills.h',
  input: ['../scripts/embed-text.py', '../data/js/polyfills.js'],
  output: 'polyfills.h',
  command: [python, '@INPUT0@', '@INPUT1@', '@OUTPUT@',
            'ns_js_polyfills_src'],
)

wpt_hook_h = custom_target(
  'wpt_hook.h',
  input: ['../scripts/embed-text.py', '../data/js/wpt-hook.js'],
  output: 'wpt_hook.h',
  command: [python, '@INPUT0@', '@INPUT1@', '@OUTPUT@',
            'ns_wpt_hook_src'],
)

northstar_engine_sources = files(
  'anim.c',
  'bytecode_cache.c',
  'bookmarks.c',
  'cache.c',
  'config.c',
  'csp.c',
  'css.c',
  'datetime.c',
  'debuglog.c',
  'dom.c',
  'engine.c',
  'eventsource.c',
  'ext.c',
  'forms.c',
  'camera.c',
  'mic.c',
  'font.c',
  'history.c',
  'html.c',
  'i18n.c',
  'image.c',
  'image_ico.c',
  'image_wuffs.c',
  'idb.c',
  'js.c',
  'js_canvas.c',
  'js_intl.c',
  'js_date.c',
  'js_perf.c',
  'js_realm.c',
  'quickjs_compat.c',
  'layout.c',
  'mathml.c',
  'net.c',
  'netutil.c',
  'paint.c',
  'render.c',
  'safebrowsing.c',
  'secretbox.c',
  'security.c',
  'selection.c',
  'spellcheck.c',
  'texture.c',
  'wasm.c',
  'webcrypto.c',
  'ws.c',
  'html_lexbor.c',
  'xml.c',
)

# GTK-free helpers used by the desktop executable but not the engine UI:
# the headless render driver and the crash watchdog. Kept in src/ as common
# code; the GTK frontend itself lives in src/gtk/.
northstar_app_sources = files(
  'headless.c',
  'watchdog.c',
)

if have_avif
  northstar_engine_sources += files('image_avif.c')
endif

northstar_win_resources = []
if host_machine.system() == 'windows'
  windows = import('windows')
  northstar_win_resources = windows.compile_resources(
    'northstar.rc',
    depend_files: files('../data/icons/northstar.ico'),
  )
endif

northstar_deps = [
  glib,
  gobject,
  gio,
  cairo,
  pango,
  pangocairo,
  gdkpixbuf,
  gmodule,
  libcurl,
  libcrypto,
  libm,
  libquickjs,
  libwuffs,
  libuchardet,
  liblexbor,
  libwamr,
  librsvg,
  sqlite3,
  zlib,
]

if have_seccomp
  northstar_deps += [libseccomp]
endif

if have_avif
  northstar_deps += [libavif]
endif

if have_enchant
  northstar_deps += [libenchant]
endif

if have_fontconfig
  northstar_deps += [libfontconfig]
endif

if have_pangoft2
  northstar_deps += [libpangoft2]
endif

if have_freetype
  northstar_deps += [libfreetype]
endif

northstar_deps += [libpsl]

northstar_win_subsystem = (
  host_machine.system() == 'windows' ? 'windows' : 'console'
)

northstar_link_args = []
if host_machine.system() == 'windows'
  northstar_link_args += cc.get_supported_link_arguments(
    ['-Wl,--stack,8388608'],
  )
  northstar_link_args += ['-lbcrypt', '-ladvapi32', '-lshell32', '-luser32']
endif

# NetBSD pkgsrc gnutls (pulled in transitively) carries an undefined
# reference to libc's mbrtoc32, which the hardened --no-undefined link
# rejects even though the dynamic linker resolves it at runtime. Allow
# undefined symbols originating from shared libraries on NetBSD only; our
# own objects are still checked.
if host_machine.system() == 'netbsd'
  northstar_link_args += cc.get_supported_link_arguments(
    ['-Wl,--allow-shlib-undefined'],
  )
endif

# The gmodule-2.0 dependency injects -Wl,--export-dynamic, which dumps every
# internal engine symbol (~6000: ns_*, lexbor, quickjs) into the executables'
# .dynsym. Combined with -Wl,-z,now that bloats dynamic-linker symbol
# resolution at process start. A version script localises
# the executables' own symbols (it overrides --export-dynamic regardless of
# link order); the only runtime dlsym we need (RAND_bytes for RNG warmup)
# resolves through a dependency library, whose exports this leaves untouched.
# Applies to the executable, including its internal renderer interface.
northstar_exe_link_args = northstar_link_args + \
  cc.get_supported_link_arguments(
    ['-Wl,--version-script=' + meson.current_source_dir() / 'exe-local.map'])

if host_machine.system() == 'windows'
  executable(
    'northstar-launcher',
    files('win_launcher.c') + northstar_win_resources,
    win_subsystem: 'windows',
    link_args: ['-luser32', '-lshell32'],
  )
endif

# Optional in-process audio playback.
northstar_audio_sources = files('audio/stub.c')
northstar_audio_deps = []
northstar_audio_libs = []
sdl2_dep = dependency('sdl2', required: get_option('audio'))
if sdl2_dep.found()
  libminimp3 = static_library(
    'minimp3',
    files('audio/minimp3_impl.c'),
    include_directories: include_directories('audio'),
    c_args: cc.get_supported_arguments(['-w']),
    install: false,
  )

  northstar_audio_sources = files('audio/audio.c')
  northstar_audio_deps = [dependency('libcurl'), sdl2_dep, libplmpeg]
  northstar_audio_libs = [libminimp3]
  if libvorbisfile.found()
    northstar_audio_deps += [libvorbisfile]
  endif
  if libopusfile.found()
    northstar_audio_deps += [libopusfile]
  endif
  # libm is needed by pl_mpeg / minimp3 on Linux and the BSDs; it is part of
  # the C runtime on Windows, where find_library('m') is a harmless no-op.
  northstar_audio_deps += [cc.find_library('m', required: false)]
else
  message('SDL2 not found — in-process audio playback disabled')
endif

if have_gtk
  subdir('gtk')
endif

if not have_gtk
  message('GTK 4 shell disabled — building the engine without GTK.')
endif
