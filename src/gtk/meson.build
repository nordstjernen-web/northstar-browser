northstar_gtk_sources = files(
  '../appmain.c',
  'procview.c',
  'procwindow.c',
  '../libnorthstar.c',
  '../renderer_serve.c',
  '../rproc_http.c',
  '../rproc_inproc.c',
  '../ipc_http.c',
  '../threaddump.c',
)

northstar_gtk_deps = northstar_deps + [gtk4_dep]
if host_machine.system() != 'windows'
  northstar_gtk_deps += [cc.find_library('rt', required: false)]
endif

northstar_gtk_platform_sources = []
northstar_gtk_platform_link_args = []

gnome = import('gnome')
northstar_icon_resources = gnome.compile_resources(
  'northstar-icon-resources',
  meson.project_source_root() / 'data' / 'icons' / 'northstar-icons.gresource.xml',
  source_dir: meson.project_source_root() / 'data' / 'icons',
  c_name: 'northstar_icons',
)

executable(
  'northstar',
  northstar_engine_sources + northstar_app_sources +
    northstar_gtk_sources + northstar_audio_sources +
    northstar_gtk_platform_sources +
    northstar_icon_resources +
    northstar_win_resources + [polyfills_h, wpt_hook_h],
  dependencies: northstar_gtk_deps + northstar_audio_deps,
  link_with: northstar_audio_libs,
  c_args: ['-DNS_HAVE_GTK=1'],
  include_directories: include_directories('..', '.'),
  pie: host_machine.system() != 'windows',
  win_subsystem: northstar_win_subsystem,
  link_args: northstar_exe_link_args + northstar_gtk_platform_link_args,
  install: true,
)
