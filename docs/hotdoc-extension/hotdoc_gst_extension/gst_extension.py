import cchardet
import os
import hotdoc_c_extension
from hotdoc.core.links import Link
from hotdoc.utils.loggable import warn, Logger
from hotdoc.core.extension import Extension
from hotdoc.core.symbols import ClassSymbol, QualifiedSymbol, PropertySymbol, SignalSymbol, ReturnItemSymbol, ParameterSymbol
from hotdoc.parsers.gtk_doc import GtkDocParser
from hotdoc_c_extension.c_extension import extract_comments
from hotdoc.core.exceptions import HotdocSourceException
from hotdoc_c_extension.c_comment_scanner.c_comment_scanner import extract_comments
from hotdoc_c_extension.c_formatter import CFormatter, Formatter
from hotdoc_c_extension.gi_extension import WritableFlag, ReadableFlag, ConstructFlag, ConstructOnlyFlag

import subprocess
import json

Logger.register_warning_code('signal-arguments-mismatch', HotdocSourceException,
                             'gst-extension')

SCANNER_PATH = r"@SCANNER_PATH@"
DESCRIPTION=\
        """
Extract gstreamer plugin documentation from sources and
built plugins.
"""

FUNDAMENTAL_TYPES = {
    "gchararray *": "gchar *"
}

def force_unicode(data):
    encoding = cchardet.detect(data)['encoding']
    return data.decode(encoding, errors='replace')


class GstFormatter(CFormatter):
    def __init__(self):
        c_extension_path = hotdoc_c_extension.__path__[0]
        searchpath = [os.path.join(c_extension_path, "templates")]
        Formatter.__init__(self, searchpath)

    def _format_flags (self, flags):
        template = self.engine.get_template('gi_flags.html')
        out = template.render ({'flags': flags})
        return out

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
        self.formatters["html"] = GstFormatter()
        self.__pending_signals = {}

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
                            signal, element_name = self.__pending_signals.pop(block.name, (None, None))
                            if signal:
                                self.__create_signal_symbol(block.name, element_name, signal, block)
                            self.project.database.add_comment(block)

        for unique_name, (signal, element_name) in self.__pending_signals.items():
            self.__create_signal_symbol(unique_name, element_name, signal)

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

    def __create_hierarchy(self, element_dict):
        hierarchy = []

        for klass_name in element_dict["hierarchy"][1:]:
            link = Link(None, klass_name, klass_name)
            sym = QualifiedSymbol(type_tokens=[link])
            hierarchy.append(sym)

        hierarchy.reverse()
        return hierarchy

    def __create_signal_symbol(self, unique_name, element_name, signal, block=None):
        atypes = signal['args']
        instance_type = unique_name.split("::")[0]
        name = unique_name.split("::")[1]

        args_type_names = []
        if block:
            param_names = [p for p in block.params.keys()]
            if len(param_names) == len(atypes) + 1:
                args_type_names = [(instance_type + " *", param_names[0])]
                for i, _type in enumerate(atypes):
                    args_type_names.append((_type, param_names[i + 1]))
            else:
                warn('signal-arguments-mismatch', "Expected arguments with types '%s', got '%s'"
                     % (signal['args'],  param_names))

        if not args_type_names:
            args_type_names = [(instance_type + " *", 'param_0')]
            for i in range(len(atypes)):
                args_type_names.append((atypes[i], 'param_%s' % (i + 1)))

        args_type_names.append(("gpointer", "udata"))
        params = []
        for _type, argname in args_type_names:
            params.append(ParameterSymbol (argname=argname, type_tokens=_type))
        retval = [ReturnItemSymbol (type_tokens=signal['retval'])]

        self.get_or_create_symbol(SignalSymbol,
            parameters=params, return_value=retval,
            display_name=name, unique_name=unique_name,
             extra={'gst-element-name': element_name})

    def __create_signal_symbols(self, element):
        for signal in element.get('signals', []):
            self.__pending_signals["%s::%s" % (element['hierarchy'][0], signal['name'])] = signal, element['name']

    def __create_property_symbols(self, element):
        for prop in element.get('properties', []):
            name = prop['name']
            unique_name = '%s:%s' % (element['hierarchy'][0], name)
            flags = [ReadableFlag()]
            if prop['writable']:
                flags += [WritableFlag()]
            if prop['construct-only']:
                flags += [ConstructOnlyFlag()]
            elif prop['construct']:
                flags += [ConstructFlag()]

            type_name = FUNDAMENTAL_TYPES.get(prop['type-name'], prop['type-name'])
            type_ = QualifiedSymbol (type_tokens=type_name)
            res = self.get_or_create_symbol(PropertySymbol,
                    prop_type=type_,
                    display_name=name, unique_name=unique_name,
                    extra={'gst-element-name': element['name']})

            # FIXME This is incorrect, it's not yet format time (from gi_extension)
            extra_content = self.get_formatter(self.project.output_format)._format_flags (flags)
            res.extension_contents['Flags'] = extra_content

    def __parse_plugin(self, dl_path, data):
        plugin = json.loads(data.decode())
        for element in plugin.get('elements') or []:
            self.__elements[element['name']] = self.get_or_create_symbol(
                ClassSymbol, display_name=element['name'],
                hierarchy=self.__create_hierarchy(element),
                unique_name=element['hierarchy'][0],
                filename=dl_path, extra={'gst-element-name': element['name']})
            self.__create_property_symbols(element)
            self.__create_signal_symbols(element)


def get_extension_classes():
    return [GstExtension]
