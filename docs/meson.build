hotdoc_p = find_program('hotdoc', required : false)

has_hotdoc_extensions = false
if hotdoc_p.found()
    cres = run_command(hotdoc_p, '--has-extension', 'gi-extension')
    if cres.returncode() == 0
        has_hotdoc_extensions = true
    else
        message('Hotdoc has extension output: ' + cres.stdout() + cres.stderr())
    endif
endif

build_hotdoc = false
if build_machine.system() == 'windows'
    message('Disabling documentation while building on Windows')
elif get_option('disable_doc')
    message('Documentation is disabled via options')
elif not hotdoc_p.found()
    message('Hotdoc not found, not building the documentation')
elif not build_gir
    message('GObject introspection not built, not building documentation')
elif not has_hotdoc_extensions
    message('Hotdoc GStreamer plugin scanner extension not found, not building' +
        'the documentation')
else
  build_hotdoc = true
  subdir('hotdoc')
endif
