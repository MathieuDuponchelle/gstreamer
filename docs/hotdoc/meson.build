subdir('hotdoc-extension')

hotdoc = import('hotdoc')
docconf = configuration_data()
docconf.set('GST_API_VERSION', apiversion)

version_entities = configure_file(input : 'version.in',
    output : 'gst_api_version.md',
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
    gst_excludes += [join_paths(meson.current_source_dir(), '..', '..', 'gst', h)]
endforeach

libs_excludes = []
foreach h: ['net/gstntppacket.*']
    libs_excludes += [join_paths(meson.current_source_dir(), '..', '..', 'libs', 'gst', h)]
endforeach

libs_doc = [hotdoc.generate_doc('libgstreamer',
    project_version: apiversion,
    gi_c_sources: ['../../gst/gst.h', '../../gst/*.[hc]'],
    gi_sources: [gst_gir[0].full_path()],
    gi_c_source_filters: gst_excludes,
    sitemap: 'gst/sitemap.txt',
    index: 'gst/index.md',
    gi_index: 'gst/index.md',
    gi_smart_index: true,
    dependencies: [gst_dep, glib_dep, gobject_dep, gmodule_dep, cfile],
    disable_incremental_build: true,
)]

libs_doc += [hotdoc.generate_doc('libgstbase',
    project_version: apiversion,
    gi_c_sources: ['../../libs/gst/base/*.[ch]'],
    gi_sources: [gst_base_gir[0].full_path()],
    sitemap: 'libs/base/sitemap.txt',
    index: 'libs/base/index.md',
    gi_index: 'libs/base/index.md',
    gi_smart_index: true,
    include_paths: join_paths(meson.current_source_dir(), 'libs/'),
    dependencies: [gst_dep, glib_dep, gobject_dep, gmodule_dep,
                    gst_base_dep, cfile],
    disable_incremental_build: true,
)]

check_source_files = []
foreach f: gst_check_sources + gst_check_headers
    check_source_files += [join_paths(meson.current_source_dir(),
        '../../libs/gst/check', f)]
endforeach

libs_doc += [hotdoc.generate_doc('libgstcheck',
    project_version: apiversion,
    gi_c_sources: ['../../libs/gst/check/*.[ch]', ],
    sitemap: 'libs/check/sitemap.txt',
    index: 'libs/check/index.md',
    gi_index: 'libs/check/index.md',
    gi_smart_index: true,
    gi_sources: [gst_check_gir[0].full_path()],
    dependencies: [gst_dep, glib_dep, gobject_dep, gmodule_dep, gst_check_dep, cfile] + gst_check_gir,
    disable_incremental_build: true,
)]

libs_doc += [hotdoc.generate_doc('libgstcontroller',
    project_version: apiversion,
    gi_c_sources: ['../../libs/gst/controller/*.[ch]'],
    gi_sources: [gst_controller_gir[0].full_path(),],
    sitemap: 'libs/controller/sitemap.txt',
    index: 'libs/controller/index.md',
    gi_index: 'libs/controller/index.md',
    gi_smart_index: true,
    dependencies: [gst_dep, glib_dep, gobject_dep, gmodule_dep,
                    gst_controller_dep, cfile,],
    disable_incremental_build: true,
)]

libs_doc += [hotdoc.generate_doc('libgstnet',
    project_version: apiversion,
    gi_c_sources: ['../../libs/gst/net/*.[ch]'],
    gi_sources: [gst_net_gir[0].full_path(),],
    sitemap: 'libs/net/sitemap.txt',
    index: 'libs/net/index.md',
    gi_index: 'libs/net/index.md',
    gi_smart_index: true,
    dependencies: [gst_dep, glib_dep, gobject_dep, gmodule_dep,
                    gst_net_dep, cfile,],
    disable_incremental_build: true,
)]

plugins_doc = [hotdoc.generate_doc('GStreamer-core-plugins',
    project_version: apiversion,
    sitemap: 'plugins/sitemap.txt',
    index: 'plugins/index.md',
    gst_index: 'plugins/index.md',
    gst_smart_index: true,
    gst_c_sources: ['../../plugins/elements/*.c', '../../plugins/elements/*.h'],
    gst_dl_sources: gst_elements.full_path(),
    dependencies: [gst_dep, glib_dep, gobject_dep, gmodule_dep, hotdoc_plugin_scanner,
        gst_elements, cfile],
    extra_extensions_paths: hotdoc_extension_dir,
    disable_incremental_build: true,
    gst_cache_file: join_paths(meson.current_source_dir(), 'plugins', 'cache.json')
)]

doc = hotdoc.generate_doc('gstreamer',
    project_version: apiversion,
    sitemap: 'sitemap.txt',
    index: 'index.md',
    install: not get_option('disable_install_doc'),
    disable_incremental_build: true,
    subprojects: libs_doc + plugins_doc,
    extra_assets: join_paths(meson.current_source_dir(), 'images'),
)
