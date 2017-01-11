hotdoc_plugin_scanner = executable('hotdoc-plugin-scanner',
  'hotdoc-plugin-scanner.c',
  c_args : gst_c_args,
  include_directories : [configinc],
  dependencies : [gobject_dep, gmodule_dep, glib_dep, gst_dep],
  # install_dir : helpers_install_dir,
  link_with: [printf_lib],
  # install: true,
)

hotdoc_extension_dir = meson.current_build_dir()
config_data = configuration_data()
config_data.set('SCANNER_PATH', hotdoc_plugin_scanner.full_path())
configure_file = configure_file(input: 'setup.py', output : 'setup.py',
        configuration: config_data)
subdir('hotdoc_gst_extension')
