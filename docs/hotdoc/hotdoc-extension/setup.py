from setuptools import setup, find_packages

setup(
    name = "hotdoc_gst_extension",
    version = "0.8",
    keywords = "gstreamer hotdoc",
    author_email = 'mathieu.duponchelle@opencreed.com',
    license = 'LGPL',
    description = "An extension for hotdoc that documents gstreamer plugins",
    author = "Mathieu Duponchelle",
    entry_points = {'hotdoc.extensions': 'get_extension_classes = hotdoc_gst_extension.gst_extension:get_extension_classes'},
)
