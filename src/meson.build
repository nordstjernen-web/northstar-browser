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

nordstjernen_engine_sources = files(
  'ai.c',
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
  'mobile.c',
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
nordstjernen_app_sources = files(
  'headless.c',
  'watchdog.c',
)

if have_avif
  nordstjernen_engine_sources += files('image_avif.c')
endif

nordstjernen_win_resources = []
if host_machine.system() == 'windows'
  windows = import('windows')
  nordstjernen_win_resources = windows.compile_resources(
    'nordstjernen.rc',
    depend_files: files('../data/icons/nordstjernen.ico'),
  )
endif

if is_mobile
  nordstjernen_deps = [
    glib,
    gobject,
    gio,
    gmodule,
    cairo,
    pango,
    pangocairo,
    libcurl,
    libcrypto,
    libm,
    libquickjs,
    libwuffs,
    libplmpeg,
    libuchardet,
    liblexbor,
    libwamr,
    sqlite3,
    zlib,
  ]
else
  nordstjernen_deps = [
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
    libplmpeg,
    libuchardet,
    liblexbor,
    libwamr,
    librsvg,
    sqlite3,
    zlib,
  ]

  if have_seccomp
    nordstjernen_deps += [libseccomp]
  endif

  if have_avif
    nordstjernen_deps += [libavif]
  endif

  if have_enchant
    nordstjernen_deps += [libenchant]
  endif
endif

if have_fontconfig
  nordstjernen_deps += [libfontconfig]
endif

if have_pangoft2
  nordstjernen_deps += [libpangoft2]
endif

if have_freetype
  nordstjernen_deps += [libfreetype]
endif

nordstjernen_deps += [libpsl]

sdl2_dep = dependency('sdl2', required: get_option('audio'))
ns_lib_c_args = []
ns_sdl_deps = []
if sdl2_dep.found()
  ns_sdl_deps += [sdl2_dep]
  ns_lib_c_args += ['-DNS_HAVE_SDL']
endif

if have_ai
  cpp_stdlib_name = host_machine.system() == 'darwin' ? 'c++' : 'stdc++'
  nordstjernen_deps += ai_deps
  nordstjernen_deps += [
    meson.get_compiler('cpp').find_library(cpp_stdlib_name, required: true),
    cc.find_library('dl', required: false),
    dependency('threads'),
  ]
endif

nordstjernen_win_subsystem = (
  host_machine.system() == 'windows' ? 'windows' : 'console'
)

nordstjernen_link_args = []
if host_machine.system() == 'windows'
  nordstjernen_link_args += cc.get_supported_link_arguments(
    ['-Wl,--stack,8388608'],
  )
  nordstjernen_link_args += ['-lbcrypt', '-ladvapi32', '-lshell32', '-luser32']
endif

# NetBSD pkgsrc gnutls (pulled in transitively) carries an undefined
# reference to libc's mbrtoc32, which the hardened --no-undefined link
# rejects even though the dynamic linker resolves it at runtime. Allow
# undefined symbols originating from shared libraries on NetBSD only; our
# own objects are still checked.
if host_machine.system() == 'netbsd'
  nordstjernen_link_args += cc.get_supported_link_arguments(
    ['-Wl,--allow-shlib-undefined'],
  )
endif

# The gmodule-2.0 dependency injects -Wl,--export-dynamic, which dumps every
# internal engine symbol (~6000: ns_*, lexbor, quickjs) into the executables'
# .dynsym. Combined with -Wl,-z,now that bloats the dynamic-linker symbol
# resolution done on every process start — and the renderer is forked+exec'd
# afresh, so it pays that cost on each cold start. A version script localises
# the executables' own symbols (it overrides --export-dynamic regardless of
# link order); the only runtime dlsym we need (RAND_bytes for RNG warmup)
# resolves through a dependency library, whose exports this leaves untouched.
# Applies to the executables only; the shared libnordstjernen keeps its API.
nordstjernen_exe_link_args = nordstjernen_link_args + \
  cc.get_supported_link_arguments(
    ['-Wl,--version-script=' + meson.current_source_dir() / 'exe-local.map'])

if host_machine.system() == 'windows'
  executable(
    'nordstjernen-launcher',
    files('win_launcher.c') + nordstjernen_win_resources,
    win_subsystem: 'windows',
    link_args: ['-luser32', '-lshell32'],
  )
endif

# Optional helper executables (the ipc-http benchmark and the audio helper).
if not is_mobile
  rproc_deps = []
  if host_machine.system() != 'windows'
    rproc_deps += [cc.find_library('rt', required: false)]
  endif

  if get_option('ipc_experiment') and host_machine.system() != 'windows'
    executable(
      'ipc-http-demo',
      files('ipc_http_demo.c', 'rproc_http.c', 'ipc_http.c'),
      include_directories: include_directories('.'),
      dependencies: rproc_deps,
      install: false,
    )
  endif

  sdl2_dep = dependency('sdl2', required: get_option('audio'))
  if sdl2_dep.found()
    libminimp3 = static_library(
      'minimp3',
      files('audio/minimp3_impl.c'),
      include_directories: include_directories('audio'),
      c_args: cc.get_supported_arguments(['-w']),
      install: false,
    )

    audio_deps = [dependency('libcurl'), sdl2_dep, libplmpeg]
    if libvorbisfile.found()
      audio_deps += [libvorbisfile]
    endif
    if libopusfile.found()
      audio_deps += [libopusfile]
    endif
    # libm is needed by pl_mpeg / minimp3 on Linux and the BSDs; it is part of
    # the C runtime on macOS and Windows, where find_library('m') is a harmless
    # no-op.
    audio_lib_deps = [cc.find_library('m', required: false)]

    # On Linux the helper confines itself with the same Landlock + seccomp
    # profile as the renderer once its audio device and network stack are up,
    # so attacker-controlled codec bytes are decoded inside the sandbox.
    audio_sandbox_sources = []
    if host_machine.system() == 'linux'
      audio_sandbox_sources += files('security.c')
      audio_deps += [glib]
      if have_seccomp
        audio_deps += [libseccomp]
      endif
    endif

    executable(
      'nordstjernen-audio',
      files('audio/main.c') + audio_sandbox_sources,
      include_directories: include_directories('audio', '.'),
      link_with: libminimp3,
      dependencies: audio_deps + audio_lib_deps,
      win_subsystem: 'windows',
      install: true,
    )
  else
    message('SDL2 not found — audio playback helper (nordstjernen-audio) disabled')
  endif

endif

if not is_mobile and have_gtk
  subdir('gtk')
endif

if not is_mobile and not have_gtk
  message('GTK 4 shell disabled — building the engine, out-of-process renderer' +
          ' and embeddable library without GTK. WebGL is unavailable in this' +
          ' configuration (its GL context is GTK-backed).')
endif
