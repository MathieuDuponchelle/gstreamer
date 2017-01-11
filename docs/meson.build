docconf = configuration_data()

docconf.set('GST_API_VERSION', apiversion)
docconf.set('PACKAGE_VERSION', gst_version)
docconf.set('PLUGINDIR', '@0@/lib/gstreamer-1.0'.format(get_option('prefix')))

version_entities = configure_file(input : 'version.entities.in',
  output : 'version.entities',
  configuration : docconf)

excludes_c = []
foreach h: ['gettext.h', 'glib-compat-private.h', 'glib-compat.h',
            'gst-i18n-app.h', 'gst-i18n-lib.h', 'gst_private.h',
            'gstelementdetails.h', 'gstmacros.h', 'gstmarshal.h',
            'math-compat.h', 'parse/grammar.tab.h',
            'parser/grammar.tab.pre.h', 'parse/parse_lex.h', 'types.h',
            'gst-printf.h', 'printf-args.h', 'printf-extension.h',
            'printf-parse.h', 'vasnprintf.h',]
    excludes_c += [join_paths(meson.current_source_dir(), '..', 'gst', h)]
endforeach

if hotdoc_p.found()
    hotdoc = import('hotdoc')

    hotdoc.generate_doc('gstreamer-1.0',
      c_sources: ['../gst/gst.h', '../gst/*.h', '../gst/*.c'],
      gi_sources: [gst_gir[0].full_path()],
      c_source_filters: excludes_c,
      sitemap: 'sitemap.txt',
      index: 'index.md',
      gi_smart_index: true,
      fatal_warnings: true,
      dependencies: [gst_dep, glib_dep, gobject_dep, gmodule_dep],
    )
else
    subdir('gst')
    subdir('libs')
endif
