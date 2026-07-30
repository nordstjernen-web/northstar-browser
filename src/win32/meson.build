northstar_win32_sources = files(
  '../appmain.c',
  'winview.c',
  'winwindow.c',
  '../libnorthstar.c',
  '../renderer_serve.c',
  '../rproc_http.c',
  '../rproc_inproc.c',
  '../ipc_http.c',
  '../threaddump.c',
)

northstar_win32_deps = northstar_deps + [
  cc.find_library('comctl32'),
  cc.find_library('comdlg32'),
  cc.find_library('gdi32'),
]

executable(
  'northstar',
  northstar_engine_sources + northstar_app_sources +
    northstar_win32_sources + northstar_audio_sources +
    northstar_win_resources + [polyfills_h, wpt_hook_h],
  dependencies: northstar_win32_deps + northstar_audio_deps,
  link_with: northstar_audio_libs,
  c_args: [
    '-DNS_HAVE_WIN32_SHELL=1',
    '-DUNICODE',
    '-D_UNICODE',
    '-DWINVER=0x0A00',
    '-D_WIN32_WINNT=0x0A00',
  ],
  include_directories: include_directories('..', '.'),
  pie: false,
  win_subsystem: 'windows',
  link_args: northstar_exe_link_args,
  install: true,
)
