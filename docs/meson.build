docconf = configuration_data()

docconf.set('GST_API_VERSION', apiversion)
docconf.set('PACKAGE_VERSION', gst_version)
docconf.set('PLUGINDIR', '@0@/lib/gstreamer-1.0'.format(get_option('prefix')))

version_entities = configure_file(input : 'version.entities.in',
  output : 'version.entities',
  configuration : docconf)

libs_excludes = []
foreach h: ['gettext.h', 'glib-compat-private.h', 'glib-compat.h',
            'gst-i18n-app.h', 'gst-i18n-lib.h', 'gst_private.h',
            'gstelementdetails.h', 'gstmacros.h', 'gstmarshal.h',
            'math-compat.h', 'parse/grammar.tab.h',
            'parser/grammar.tab.pre.h', 'parse/parse_lex.h', 'types.h',
            'gst-printf.h', 'printf-args.h', 'printf-extension.h',
            'printf-parse.h', 'vasnprintf.h',]
    libs_excludes += [join_paths(meson.current_source_dir(), '..', 'gst', h)]
endforeach

if hotdoc_p.found()
    subdir('hotdoc')
    subdir('hotdoc-extension')
else
    subdir('gst')
    subdir('libs')
endif
