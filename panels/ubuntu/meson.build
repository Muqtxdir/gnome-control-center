panels_list += cappletname
desktop = 'gnome-@0@-panel.desktop'.format(cappletname)

desktop_in = configure_file(
  input: desktop + '.in.in',
  output: desktop + '.in',
  configuration: desktop_conf
)

i18n.merge_file(
  type: 'desktop',
  input: desktop_in,
  output: desktop,
  po_dir: po_dir,
  install: true,
  install_dir: control_center_desktopdir
)

sources = files(
  'cc-ubuntu-panel.c',
  'cc-ubuntu-dock-dialog.c',
)

resource_data = files(
  'appearance.css',
  'assets/theme-dark.svg',
  'assets/theme-light.svg',
  'assets/colors/orange.svg',
  'assets/colors/aqua.svg',
  'assets/colors/blue.svg',
  'assets/colors/green.svg',
  'assets/colors/purple.svg',
  'assets/colors/pink.svg',
  'assets/colors/red.svg',
  'assets/colors/yellow.svg',
  'cc-ubuntu-panel.ui',
  'cc-ubuntu-dock-dialog.ui',
)

sources += gnome.compile_resources(
  'cc-' + cappletname + '-resources',
  cappletname + '.gresource.xml',
  c_name: 'cc_' + cappletname,
  dependencies: resource_data,
  export: true
)

cflags += '-DGNOMELOCALEDIR="@0@"'.format(control_center_localedir)

panels_libs += static_library(
  cappletname,
  sources: sources,
  include_directories: [top_inc, common_inc],
  dependencies: common_deps,
  c_args: cflags
)
