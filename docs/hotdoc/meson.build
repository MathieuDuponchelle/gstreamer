hotdoc = import('hotdoc')

# hotdoc.generate_doc('gstreamer-1.0',
#   c_sources: ['../gst/gst.h', '../gst/*.h', '../gst/*.c'],
#   gi_sources: [gst_gir[0].full_path()],
#   c_source_filters: libs_excludes,
#   sitemap: 'sitemap.txt',
#   index: 'index.md',
#   gi_smart_index: true,
#   gst_c_sources: ['../plugins/elements/*.c', '../plugins/elements/*.h'],
#   gst_dl_sources: gst_elements.full_path(),
#   dependencies: [gst_dep, glib_dep, gobject_dep, gmodule_dep],
# )

hotdoc.generate_doc('gstreamer-1.0',
    sitemap: 'sitemap.txt',
    index: 'index.md',
    gst_smart_index: true,
    gst_c_sources: ['../plugins/elements/*.c', '../plugins/elements/*.h'],
    gst_dl_sources: gst_elements.full_path(),
    dependencies: [gst_dep, glib_dep, gobject_dep, gmodule_dep],
)

