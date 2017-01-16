import cchardet
from hotdoc.core.extension import Extension
from hotdoc.core.symbols import ClassSymbol
from hotdoc.parsers.gtk_doc import GtkDocParser
from hotdoc_c_extension.c_extension import extract_comments
from hotdoc_c_extension.c_comment_scanner.c_comment_scanner import extract_comments
from hotdoc_c_extension.c_formatter import CFormatter
import subprocess
import json

SCANNER_PATH = r"@SCANNER_PATH@"
DESCRIPTION=\
        """
Extract gstreamer plugin documentation from sources and
built plugins.
"""

def force_unicode(data):
    encoding = cchardet.detect(data)['encoding']
    return data.decode(encoding, errors='replace')

class GstExtension(Extension):
    extension_name = 'gst-extension'
    argument_prefix = 'gst'
    smart_index = False
    c_sources = set()
    dl_sources = set()

    def __init__(self, doc_repo):
        super().__init__(doc_repo)
        self.__elements = {}
        self.__raw_comment_parser = GtkDocParser(doc_repo)
        self.formatters['html'] = CFormatter()

    def setup(self):
        stale_c, unlisted_c = self.get_stale_files(GstExtension.c_sources,
            'gst-c')
        stale_dl, unlisted_dl = self.get_stale_files(GstExtension.dl_sources,
            'gst-dl')

        comments = []

        for dl in stale_dl:
            try:
                data = subprocess.check_output([SCANNER_PATH, dl])
                self.__parse_plugin(dl, data)
            except subprocess.CalledProcessError as _:
                print(_)
                continue

        for source_file in stale_c:
            with open (source_file, 'rb') as f:
                lines = [force_unicode(l) for l in f.readlines()]
                comments = extract_comments(''.join(lines))

                current_element = None
                for c in comments:
                    if c[3]:
                        line = lines[c[1] - 1]

                        comment = (len(line) - len(line.lstrip(' '))) * ' ' + c[0]
                        block = self.__raw_comment_parser.parse_comment(comment,
                            source_file, c[1], c[2], self.project.include_paths)

                        if block is not None:
                            # Handle Element description SECTION
                            if block.name.startswith("element-"):
                                element_name = block.name[8:]
                                element = self.__elements.get(element_name)
                                if element:
                                    block.name = element_name
                            self.project.database.add_comment(block)

    def _get_smart_index_title(self):
        return 'GStreamer plugins documentation'

    @staticmethod
    def add_arguments (parser):
        group = parser.add_argument_group('Gst extension', DESCRIPTION)
        GstExtension.add_index_argument(group)
        GstExtension.add_sources_argument(group, prefix='gst-c')
        GstExtension.add_sources_argument(group, prefix='gst-dl')

    @staticmethod
    def parse_config(doc_repo, config):
        GstExtension.parse_standard_config(config)
        GstExtension.c_sources = config.get_sources('gst-c_')
        GstExtension.dl_sources = config.get_sources('gst-dl_')

    def _get_smart_key(self, symbol):
        return symbol.extra.get('gst-element-name')

    def __parse_plugin(self, dl_path, data):
        plugin = json.loads(data.decode())
        for element in plugin.get('elements') or []:
            self.__elements[element['name']] = self.get_or_create_symbol(
                ClassSymbol, display_name=element['name'],
                unique_name='%s::%s' % (element['name'], element['name']),
                filename=dl_path, extra={'gst-element-name': element['name']})

def get_extension_classes():
    return [GstExtension]
