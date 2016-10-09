gst_plugin_scanner = executable('hotdoc-plugin-scanner',
  'hotdoc-plugin-scanner.c',
  c_args : gst_c_args,
  include_directories : [configinc],
  dependencies : [gobject_dep, gmodule_dep, glib_dep, gst_dep],
  # install_dir : helpers_install_dir,
  link_with: [printf_lib],
  # install: true,
)

