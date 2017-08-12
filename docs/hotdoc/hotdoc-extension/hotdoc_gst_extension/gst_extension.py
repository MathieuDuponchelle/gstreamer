import tempfile
import os
from collections import Mapping
from collections import OrderedDict

import hotdoc_c_extension
from sqlalchemy import Column, ForeignKey, Integer, PickleType, String
from hotdoc_c_extension.gi_extension import GIExtension
from hotdoc.core.links import Link
from hotdoc.utils.loggable import warn, info, Logger, error
from hotdoc.core.extension import Extension
from hotdoc.core.symbols import ClassSymbol, QualifiedSymbol, PropertySymbol, SignalSymbol, ReturnItemSymbol, ParameterSymbol, Symbol, EnumSymbol
from hotdoc.parsers.gtk_doc import GtkDocParser
from hotdoc_c_extension.utils.utils import CCommentExtractor
from hotdoc.core.exceptions import HotdocSourceException
from hotdoc.core.formatter import Formatter
from hotdoc.core.comment import Comment
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
    "gchararray *": "gchar *",
    "gchararray": "gchar *"
}


def dict_recursive_update(d, u):
    for k, v in u.items():
        if isinstance(v, Mapping):
            r = dict_recursive_update(d.get(k, {}), v)
            d[k] = r
        else:
            d[k] = u[k]
    return d


class GstPluginsSymbol(Symbol):
    TEMPLATE = """
        @require(symbol)
        <div class="base_symbol_container">
        <table class="table table-striped table-hover">
            <tbody>
                <tr>
                    <th><b>Plugin name</b></th>
                    <th><b>License</b></th>
                    <th><b>Element name</b></th>
                    <th><b>Element description</b></th>
                </tr>
                @for plugin in symbol.plugins:
                    @for elem in plugin.elements:
                        <tr>
                            <td class="min">@plugin.display_name</td>
                            <td>@plugin.license</td>
                            <td>@elem.rendered_link</td>
                            <td>@elem.desc</td>
                        </tr>
                    @end
                @end
            </tbody>
        </table>
        </div>
        @end
        """
    __tablename__ = 'gst_plugins'
    id_ = Column(Integer, ForeignKey('symbols.id_'), primary_key=True)
    __mapper_args__ = {
        'polymorphic_identity': 'gst_plugins',
    }
    name = Column(String)
    description = Column(String)
    plugins = Column(PickleType)

    def get_children_symbols(self):
        return self.plugins

    @classmethod
    def get_plural_name(cls):
        return ""


class GstElementSymbol(ClassSymbol):
    TEMPLATE = """
        @require(symbol, hierarchy, desc)
        @desc

        @if hierarchy:
        <h2>Hierarchy</h2>
        <div class="hierarchy_container">
            @hierarchy
        </div>
        @end
        <h2 class="symbol_section">Factory details</h2>
        <div class="base_symbol_container">
        <table class="table table-striped table-hover">
            <tbody>
                <tr>
                    <th>Authors:</th>
                    <td>@symbol.author</td>
                </tr>
                <tr>
                    <th>Classification:</th>
                    <td>@symbol.classification</td>
                </tr>
                <tr>
                    <th>Rank:</th>
                    <td>@symbol.rank</td>
                </tr>
                <tr>
                    <th>Plugin:</th>
                    <td>@symbol.plugin</td>
                </tr>
            </tbody>
        </table>
        </div>
        @end
        """
    __tablename__ = 'gst_element'
    id_ = Column(Integer, ForeignKey('classes.id_'), primary_key=True)
    __mapper_args__ = {
        'polymorphic_identity': 'gst_element',
    }
    classification = Column(String)
    rank = Column(String)
    author = Column(String)
    plugin = Column(String)

    @classmethod
    def get_plural_name(cls):
        return ""

class GstPluginSymbol(Symbol):
    TEMPLATE = """
        @require(symbol)
        <h1>@symbol.display_name</h1>
        <i>(from @symbol.package)</i>
        <div class="base_symbol_container">
        @symbol.formatted_doc
        <table class="table table-striped table-hover">
            <tbody>
                <tr>
                    <th><b>License</b></th>
                    <th><b>Element name</b></th>
                    <th><b>Element description</b></th>
                </tr>
                @for elem in symbol.elements:
                    <tr>
                        <td>@symbol.license</td>
                        <td>@elem.rendered_link</td>
                        <td>@elem.desc</td>
                    </tr>
                @end
            </tbody>
        </table>
        </div>
        @end
        """
    __tablename__ = 'gst_plugin'
    id_ = Column(Integer, ForeignKey('symbols.id_'), primary_key=True)
    __mapper_args__ = {
        'polymorphic_identity': 'gst_plugin',
    }
    name = Column(String)
    license = Column(String)
    description = Column(String)
    package = Column(String)
    elements = Column(PickleType)

    def get_children_symbols(self):
        return self.elements

    @classmethod
    def get_plural_name(cls):
        return ""


class GstPadTemplateSymbol(Symbol):
    TEMPLATE = """
        @extends('base_symbol.html')
        @require(symbol)

        @def header():
        <h4>
            @symbol.name
        </h4>
        @end
        @def content():
        <table class="table table-striped">
            <tbody>
                <tr><td><b>Presence:</b></td><td>@symbol.presence</td></tr>
                <tr><td><b>Direction:</b></td><td>@symbol.direction</td></tr>
                <tr><td><b>Capabilities:</b></td><td><pre><code>@symbol.caps</code></pre></td></tr>
            </tbody>
        </table>
        @end
        """

    __tablename__ = 'pad_templates'
    id_ = Column(Integer, ForeignKey('symbols.id_'), primary_key=True)
    __mapper_args__ = {
        'polymorphic_identity': 'pad_templates',
    }
    qtype = Column(PickleType)
    name = Column(String)
    direction = Column(String)
    presence = Column(String)
    caps = Column(String)

    def __init__(self, **kwargs):
        self.qtype = None
        # self.name = kwargs.pop("name")
        # self.direction = kwargs.pop("direction")
        # self.presence = kwargs.pop("presence")
        Symbol.__init__(self, **kwargs)

    def get_children_symbols(self):
        return [self.qtype]

    # pylint: disable=no-self-use
    def get_type_name(self):
        """
        Banana banana
        """
        return "GstPadTemplate"


ENUM_TEMPLATE = """
@require(values)
<table class="table table-striped">
    <tbody>
        <tr>
            <td><b>Name</b></td>
            <td><b>Description</b></td>
            <td><b>Value</b></td>
        </tr>
        @for value in values:
        <tr>
            <td>@value['name']</td>
            <td>@value['desc']</td>
            <td>@value['value']</td>
        </tr>
        @end
    </tbody>
</table>
@end
"""

class GstFormatter(Formatter):
    def __init__(self, extension):
        c_extension_path = hotdoc_c_extension.__path__[0]
        searchpath = [os.path.join(c_extension_path, "templates")]

        self.__tmpdir = tempfile.TemporaryDirectory()
        with open(os.path.join(self.__tmpdir.name, "padtemplate.html"), "w") as f:
            f.write(GstPadTemplateSymbol.TEMPLATE)
        with open(os.path.join(self.__tmpdir.name, "enumtemplate.html"), "w") as f:
            f.write(ENUM_TEMPLATE)
        with open(os.path.join(self.__tmpdir.name, "plugins.html"), "w") as f:
            f.write(GstPluginsSymbol.TEMPLATE)
        with open(os.path.join(self.__tmpdir.name, "plugin.html"), "w") as f:
            f.write(GstPluginSymbol.TEMPLATE)
        with open(os.path.join(self.__tmpdir.name, "element.html"), "w") as f:
            f.write(GstElementSymbol.TEMPLATE)
        searchpath.append(self.__tmpdir.name)
        Formatter.__init__(self, extension, searchpath)
        self._ordering.insert(0, GstPluginSymbol)
        self._ordering.insert(1, GstElementSymbol)
        self._ordering.insert(self._ordering.index(ClassSymbol) + 1, GstPadTemplateSymbol)
        self._ordering.insert(self._ordering.index(GstPadTemplateSymbol) + 1,
                              GstPluginsSymbol)

    def __del__(self):
        self.__tmpdir.cleanup()

    def __populate_plugin_infos(self, plugin):
        if not plugin.description:
            comment = self.extension.app.database.get_comment(plugin.unique_name)
            plugin.description = comment.description if comment else ''
        for element in plugin.elements:
            comment = self.extension.app.database.get_comment(
                'element-' + element.unique_name);
            element.rendered_link = self._format_linked_symbol(element)
            if not comment:
                element.desc  = "%s element" % element.display_name
                continue

            if not comment.short_description:
                desc = "%s element" % (element.display_name)
            else:
                desc = comment.short_description.description
            element.desc = desc

    def _format_symbol(self, symbol):
        if isinstance(symbol, GstPluginsSymbol):
            for plugin in symbol.plugins:
                self.__populate_plugin_infos(plugin)
            template = self.engine.get_template('plugins.html')
            return (template.render ({'symbol': symbol}), False)
        elif isinstance(symbol, GstPluginSymbol):
            self.__populate_plugin_infos(symbol)
            template = self.engine.get_template('plugin.html')
            return (template.render ({'symbol': symbol}), False)
        elif type(symbol) == GstPadTemplateSymbol:
            template = self.engine.get_template('padtemplate.html')
            return (template.render ({'symbol': symbol}), False)
        elif type(symbol) == GstElementSymbol:
            hierarchy = self._format_hierarchy(symbol)

            desc = ''
            if self.extension.has_unique_feature:
                desc = symbol.formatted_doc

            template = self.engine.get_template('element.html')
            return (template.render({'symbol': symbol,
                                     'hierarchy': hierarchy,
                                     'desc': desc}),
                    False)
        else:
            return super()._format_symbol(symbol)

    def _format_enum(self, symbol):
        template = self.engine.get_template('enumtemplate.html')
        symbol.formatted_doc = template.render ({'values': symbol.values})
        return super()._format_enum(symbol)

    def _format_flags (self, flags):
        template = self.engine.get_template('gi_flags.html')
        out = template.render ({'flags': flags})
        return out

class GstExtension(Extension):
    extension_name = 'gst-extension'
    argument_prefix = 'gst'
    __dual_links = {} # Maps myelement:XXX to GstMyElement:XXX
    __parsed_cfiles = set()
    __caches = {} # cachefile -> dict
    __apps_sigs = set()

    def __init__(self, app, project):
        super().__init__(app, project)
        self.__elements = {}
        self.__raw_comment_parser = GtkDocParser(project, section_file_matching=False)
        self.__plugins = None
        self.__list_all_plugins = False
        # If we have a plugin with only one element, we render it on the plugin
        # page.
        self.has_unique_feature = False
        self.__on_index_symbols = []

    def __main_project_done_cb(self, app):
        with open(self.cache_file, 'w') as f:
            json.dump(self.cache, f, indent=4, sort_keys=True)

    def _make_formatter(self):
        return GstFormatter(self)

    def get_or_create_symbol(self, *args, **kwargs):
        sym = super().get_or_create_symbol(*args, **kwargs)
        if self.has_unique_feature:
            self.__on_index_symbols.append(sym)

        return sym

    def setup(self):
        stale_c, unlisted_c = self.get_stale_files(self.c_sources,
            'gst_c')
        stale_dl, unlisted_dl = self.get_stale_files(self.dl_sources,
            'gst_dl')
        self.project.tree.update_signal.connect(self.__update_tree_cb)


        # Make sure the cache file is save when the whole project
        # is done.
        if self.cache_file not in GstExtension.__apps_sigs and self.cache_file:
            GstExtension.__apps_sigs.add(self.cache_file)
            self.app.formatted_signal.connect(self.__main_project_done_cb)

        cmd = [SCANNER_PATH]
        for dl in list(stale_dl):
            for ext in ['.dll', '.so', '.dylib']:
                if dl.endswith(ext):
                    cmd.append(dl)
                    break

        if not self.dl_sources:
            if self.__list_all_plugins:
                super().setup()
            return

        comment_parser = GtkDocParser(self.project, False)
        stale_c = set(stale_c) - GstExtension.__parsed_cfiles

        CCommentExtractor(self, comment_parser).parse_comments(stale_c)
        GstExtension.__parsed_cfiles.update(stale_c)

        subenv = os.environ.copy()
        if subenv.get('GST_REGISTRY_UPDATE') != 'no':
            data = subprocess.check_output(cmd, env=subenv)
            try:
                plugins = json.loads(data.decode(), object_pairs_hook=OrderedDict)
            except json.decoder.JSONDecodeError:
                print("Could not decode:\n%s" % data.decode())
                raise

            self.cache = dict_recursive_update(self.cache, plugins)

        plugins = []
        if self.plugin:
            pname = self.plugin
            dot_idx = pname.rfind('.')
            if dot_idx > 0:
                pname = self.plugin[:dot_idx]
            if pname.startswith('libgst'):
                pname = pname[6:]
            elif pname.startswith('gst'):
                pname = pname[3:]
            try:
                plugin_node = {pname: self.cache[pname]}
            except KeyError:
                self.__main_project_done_cb(None) # Save the cache
                error('setup-issue', "Plugin %s not found" % pname)
        else:
            plugin_node = self.cache

        for libfile, plugin in plugin_node.items():
            plugins.append(self.__parse_plugin(libfile, plugin))

        if not self.plugin:
            self.__plugins = self.get_or_create_symbol(
                    GstPluginsSymbol,
                    display_name=self.project.project_name.replace("-", " ").title(),
                    unique_name=self.project.project_name + "-gst-plugins",
                    plugins=plugins)

        super().setup()

    def __update_tree_cb(self, tree, unlisted_sym_names):
        if not self.smart_index:
            return

        index = tree.get_pages().get('gst-index')
        if index is None:
            return

        index.render_subpages = False

        if not self.__list_all_plugins:
            index.symbol_names.add(self.__plugins.unique_name)
            for sym in self.__on_index_symbols:
                index.symbol_names.add(sym.unique_name)
        else:
            index.symbol_names |= self.project.database.list_symbols(GstPluginsSymbol)

    def _get_smart_index_title(self):
        if self.plugin:
            return self.__plugins.display_name
        return 'GStreamer plugins documentation'

    @staticmethod
    def add_arguments(parser):
        group = parser.add_argument_group('Gst extension', DESCRIPTION)
        GstExtension.add_index_argument(group)
        # GstExtension.add_order_generated_subpages(group)
        GstExtension.add_sources_argument(group, prefix='gst-c')
        GstExtension.add_sources_argument(group, prefix='gst-dl')
        group.add_argument('--gst-cache-file', default=None)
        group.add_argument('--gst-list-all-plugins', action='store_true',
                           default=False)
        group.add_argument('--gst-plugin-name', default=None)

    def parse_config(self, config):
        self.c_sources = config.get_sources('gst_c')
        self.dl_sources = config.get_sources('gst_dl')
        self.cache_file = config.get('gst_cache_file')
        self.plugin = config.get('gst_plugin_name')
        self.__list_all_plugins = config.get('gst_list_all_plugins', False)
        info('Parsing config!')

        self.cache = {}
        if self.cache_file:
            self.cache = GstExtension.__caches.get(self.cache_file)
            if not self.cache:
                try:
                    with open(self.cache_file) as f:
                        self.cache = json.load(f)
                except FileNotFoundError:
                    pass

                if self.cache is None:
                    self.cache = {}
                GstExtension.__caches[self.cache_file] = self.cache

        super().parse_config(config)

    def _get_smart_key(self, symbol):
        if self.has_unique_feature:
            return None

        if isinstance(symbol, GstPluginSymbol):
            # PluginSymbol are rendered on the index page
            return None
        res = symbol.extra.get('gst-element-name')
        if res:
            res = res.replace("element-", "")

        return res

    def __create_hierarchy(self, element_dict):
        hierarchy = []

        for klass_name in element_dict["hierarchy"][1:]:
            link = Link(None, klass_name, klass_name)
            sym = QualifiedSymbol(type_tokens=[link])
            hierarchy.append(sym)

        hierarchy.reverse()
        return hierarchy

    def __create_signal_symbol(self, element, name, signal, block=None):
        atypes = signal['args']
        instance_type = element['hierarchy'][0]
        element_name = element['name']
        unique_name = "%s::%s" % (instance_type, name)
        aliases = ["%s::%s" % (element_name, name)]

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

        for comment_name in [unique_name] + aliases:
            comment = self.app.database.get_comment(comment_name)
            if comment:
                for i, argname in enumerate(comment.params.keys()):
                    args_type_names[i] = (args_type_names[i][0], argname)

        for _type, argname in args_type_names:
            params.append(ParameterSymbol (argname=argname, type_tokens=_type))

        type_name = signal['retval']
        tokens = type_name.split(" ")
        tokens[0] = Link (None, tokens[0], tokens[0])
        type_ = QualifiedSymbol(type_tokens=tokens)

        enum = signal.get('return-values')
        if enum:
            self.__create_enum_symbol(type_name, enum, element['name'])

        retval = [ReturnItemSymbol(type_tokens=tokens)]

        self.get_or_create_symbol(SignalSymbol,
            parameters=params, return_value=retval,
            display_name=name, unique_name=unique_name,
             extra={'gst-element-name': 'element-' + element_name},
            aliases=aliases, parent_name=element_name)

    def __create_signal_symbols(self, element):
        signals = element.get('signals', {})
        if not signals:
            return

        for name, signal in signals.items():
            self.__create_signal_symbol(element, name, signal)

    def __create_property_symbols(self, element):
        properties = element.get('properties', [])
        if not properties:
            return

        for name, prop in properties.items():
            unique_name = '%s:%s' % (element['hierarchy'][0], name)
            flags = [ReadableFlag()]
            if prop['writable']:
                flags += [WritableFlag()]
            if prop['construct-only']:
                flags += [ConstructOnlyFlag()]
            elif prop['construct']:
                flags += [ConstructFlag()]

            type_name = FUNDAMENTAL_TYPES.get(prop['type-name'], prop['type-name'])

            tokens = type_name.split(" ")
            tokens[0] = Link (None, tokens[0], tokens[0])
            type_ = QualifiedSymbol(type_tokens=tokens)

            default = prop.get('default')
            enum = prop.get('values')
            if enum:
                type_ = self.__create_enum_symbol(prop['type-name'], enum, element['name'])

            aliases = ['%s:%s' % (element['name'], name)]
            res = self.get_or_create_symbol(
                PropertySymbol,
                prop_type=type_,
                display_name=name, unique_name=unique_name,
                aliases=aliases, parent_name=element['name'],
                extra={'gst-element-name': 'element-' + element['name']},
            )

            if not self.app.database.get_comment(unique_name):
                comment = Comment(unique_name, Comment(name=name),
                    description=prop['blurb'])
                self.app.database.add_comment(comment)

            # FIXME This is incorrect, it's not yet format time (from gi_extension)
            extra_content = self.formatter._format_flags (flags)
            res.extension_contents['Flags'] = extra_content
            if default:
                res.extension_contents['Default value'] = default

    def __create_enum_symbol(self, type_name, enum, element_name):
        symbol = self.get_or_create_symbol(
            EnumSymbol, anonymous=False,
            raw_text=None, display_name=type_name,
            unique_name=type_name, parent_name=element_name,
            extra={'gst-element-name': 'element-' + element_name})
        symbol.values = enum
        return symbol

    def __create_pad_template_symbols(self, element):
        templates = element.get('pad-templates', {})
        if not templates:
            return

        for tname, template in templates.items():
            name = tname.replace("%%", "%")
            unique_name = '%s->%s' % (element['hierarchy'][0], name)
            self.get_or_create_symbol(GstPadTemplateSymbol,
                name=name,
                direction=template["direction"],
                presence=template["presence"],
                caps=template["caps"], parent_name=element['name'],
                display_name=name, unique_name=unique_name,
                extra={'gst-element-name': 'element-' + element['name']})

    def __parse_plugin(self, plugin_name, plugin):
        elements = []
        if self.plugin and len(plugin.get('elements', {}).items()) == 1:
            self.has_unique_feature = True

        for ename, element in plugin.get('elements', {}).items():
            comment = None
            element['name'] = ename
            for comment_name in ['element-' + element['name'],
                                 element['name'], element['hierarchy'][0]]:
                comment = self.app.database.get_comment(comment_name)

            if not comment:
                comment = Comment(
                    'element-' + element['name'],
                    Comment(description=element['name']),
                    description=element['description'],
                    short_description=Comment(description=element['description']))
                self.app.database.add_comment(comment)
            elif not comment.short_description:
                comment.short_description = Comment(description=element['description'])

            sym = self.__elements[element['name']] = self.get_or_create_symbol(
                GstElementSymbol, display_name=element['name'],
                hierarchy=self.__create_hierarchy(element),
                unique_name=element['name'],
                filename=plugin_name,
                extra={'gst-element-name': 'element-' + element['name']},
                rank=str(element['rank']), author=element['author'],
                classification=element['classification'], plugin=plugin['filename'],
                aliases=['element-' + element['name'], element['hierarchy'][0]])
            self.__create_property_symbols(element)
            self.__create_signal_symbols(element)
            self.__create_pad_template_symbols(element)
            elements.append(sym)

        plugin = self.get_or_create_symbol(
                GstPluginSymbol,
                description=plugin['description'],
                display_name=plugin_name,
                unique_name='plugin-' + plugin['filename'],
                license=plugin['license'],
                package=plugin['package'],
                filename=plugin['filename'],
                elements=elements,
                extra= {'gst-plugins': 'plugins-' + plugin['filename']})

        if self.plugin:
            self.__plugins = plugin
        return plugin

    def __get_link_cb(self, resolver, name):
        link = self.__dual_links.get(name)
        if link:
            # Upsert link on the first run
            if isinstance(link, str):
                sym = self.app.database.get_symbol(link)
                link = sym.link

        if link:
            return link

        return (resolver, name)

    def format_page(self, page, link_resolver, output):
        link_resolver.get_link_signal.connect(GIExtension.search_online_links)
        super().format_page (page, link_resolver, output)
        link_resolver.get_link_signal.disconnect(GIExtension.search_online_links)

def get_extension_classes():
    return [GstExtension]
