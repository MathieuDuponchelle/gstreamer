from hotdoc.core.extension import Extension
from hotdoc.core.symbols import ClassSymbol
from hotdoc.parsers.gtk_doc import GtkDocParser
from hotdoc_c_extension.c_comment_scanner.c_comment_scanner import extract_comments
from hotdoc_c_extension.c_formatter import CFormatter
import subprocess
import json

DESCRIPTION=\
        """
Extract gstreamer plugin documentation from sources and
built plugins.
"""

class GstExtension(Extension):
    extension_name = 'gst-extension'
    argument_prefix = 'gst'
    smart_index = False
    c_sources = set()
    dl_sources = set()

    def __init__(self, doc_repo):
        super().__init__(doc_repo)
        self.__raw_comment_parser = GtkDocParser(doc_repo)
        self.formatters['html'] = CFormatter()

    def setup(self):
        stale_c, unlisted_c = self.get_stale_files(GstExtension.c_sources,
            'gst-c')
        stale_dl, unlisted_dl = self.get_stale_files(GstExtension.dl_sources,
            'gst-dl')

        comments = []

        for source_file in stale_c:
            print("C is %s" % source_file)
            comments.extend(extract_comments(source_file))

        for c in comments:
            print(c)
            block = self.__raw_comment_parser.parse_comment(*c)
            if block is not None:
                self.doc_repo.doc_database.add_comment(block)

        for dl in stale_dl:
            try:
                data = subprocess.check_output(['/home/thiblahute/devel/gstreamer/gst-build/gstreamer/build/docs/hotdoc-extension/hotdoc-plugin-scanner',
                                                dl])
                self.__parse_plugin(dl, data)
            except subprocess.CalledProcessError as _:
                print(_)
                continue

    def _get_smart_index_title(self):
        return 'GStreamer plugins documentation'

    def _get_user_index_path(self):
        return GstExtension.index

    def _get_all_sources(self):
        print("Yers")
        return ["identity"]

    @staticmethod
    def add_arguments (parser):
        group = parser.add_argument_group('Gst extension', DESCRIPTION)
        GstExtension.add_index_argument(group)
        GstExtension.add_sources_argument(group, prefix='gst-c')
        GstExtension.add_sources_argument(group, prefix='gst-dl')

    @staticmethod
    def parse_config(doc_repo, config):
        print("Parsing config! %s" % config)
        GstExtension.parse_standard_config(config)
        GstExtension.c_sources = config.get_sources('gst-c_')
        GstExtension.dl_sources = config.get_sources('gst-dl_')

    def __smart_filter(self, *args, **kwargs):
        name = kwargs['display_name']
        type_ = args[0]

    def get_or_create_symbol(self, *args, **kwargs):
        print("Creating %s %s" % (args, kwargs))
        kwargs['language'] = 'c'
        if GstExtension.smart_index:
            return self.__smart_filter(*args, **kwargs)

        return super().get_or_create_symbol(*args, **kwargs)

    def __parse_plugin(self, dl_path, data):
        plugin = json.loads(data.decode())
        for element in plugin.get('elements') or []:
            self.get_or_create_symbol(ClassSymbol, display_name=element['name'],
                unique_name='%s::%s' % (element['name'], element['name']),
                    filename=dl_path)

def get_extension_classes():
    print("Got %s" % GstExtension)
    return [GstExtension]
