docconf = configuration_data()

docconf.set('GST_API_VERSION', apiversion)
docconf.set('PACKAGE_VERSION', gst_version)
docconf.set('PLUGINDIR', '@0@/lib/gstreamer-1.0'.format(get_option('prefix')))

version_entities = configure_file(input : 'version.entities.in',
  output : 'version.entities',
  configuration : docconf)

gst_excludes = []
foreach h: ['gettext.h', 'glib-compat-private.h', 'glib-compat.h',
            'gst-i18n-app.h', 'gst-i18n-lib.h', 'gst_private.h',
            'gstelementdetails.h', 'gstmacros.h', 'gstmarshal.h',
            'math-compat.h', 'parse/grammar.tab.h',
            'parser/grammar.tab.pre.h', 'parse/parse_lex.h', 'types.h',
            'gst-printf.h', 'printf-args.h', 'printf-extension.h',
            'printf-parse.h', 'vasnprintf.h', 'gstregistrybinary.c',
            'gstregistrybinary.h', 'gstpluginloader.h', 'gstpluginloader.c']
    gst_excludes += [join_paths(meson.current_source_dir(), '..', 'gst', h)]
endforeach

libs_excludes = []
foreach h: ['net/gstntppacket.*']
    libs_excludes += [join_paths(meson.current_source_dir(), '..', 'libs', 'gst', h)]
endforeach

if hotdoc_p.found()
    subdir('hotdoc-extension')

    hotdoc = import('hotdoc')

    hotdoc.generate_doc('gstreamer-' + apiversion,
        c_sources: ['../gst/gst.h', '../gst/*.[hc]'],
        gi_sources: [gst_gir[0].full_path()],
        c_source_filters: gst_excludes,
        sitemap: 'gst-sitemap.txt',
        index: 'gst-index.md',
        gi_smart_index: true,
        dependencies: [gst_dep, glib_dep, gobject_dep, gmodule_dep],
        install: true,
    )

    hotdoc.generate_doc('gstreamer-libs-' + apiversion,
        c_sources: ['../libs/gst/base/*.[ch]',
                    '../libs/gst/controller/*.[ch]',
                    '../libs/gst/net/*.[ch]',
                    '../libs/gst/check/*.[ch]',
                   ],
        gi_sources: [gst_base_gir[0].full_path(),
                     gst_controller_gir[0].full_path(),
                     gst_net_gir[0].full_path(),
                    ],
        c_source_filters: libs_excludes,
        sitemap: 'gst-libs-sitemap.txt',
        index: 'gst-libs-index.md',
        gi_smart_index: true,
        dependencies: [gst_dep, glib_dep, gobject_dep, gmodule_dep,
                       gst_base_dep, gst_net_dep, gst_controller_dep,
                       gst_check_dep],
        install: true,
    )

    hotdoc.generate_doc('gstreamer-plugins-' + apiversion,
        sitemap: 'plugins-sitemap.txt',
        index: 'plugins-index.md',
        gst_smart_index: true,
        gst_c_sources: ['../plugins/elements/*.c', '../plugins/elements/*.h'],
        gst_dl_sources: gst_elements.full_path(),
        dependencies: [gst_dep, glib_dep, gobject_dep, gmodule_dep, hotdoc_plugin_scanner,
            gst_elements],
        extra_extensions_paths: hotdoc_extension_dir,
        install: true,
    )
else
    subdir('gst')
    subdir('libs')
endif
