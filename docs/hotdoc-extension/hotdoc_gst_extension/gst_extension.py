import sys
from gi.repository import Gst
from gi.repository import GObject
Gst.init([])
from hotdoc.core.base_extension import BaseExtension
from hotdoc.parsers.gtk_doc_parser import GtkDocParser
from hotdoc_c_extension.c_comment_scanner.c_comment_scanner import get_comments

DESCRIPTION=\
        """
Extract gstreamer plugin documentation from sources and
built plugins.
"""

class GstExtension(BaseExtension):
    EXTENSION_NAME = 'gst-extension'
    argument_prefix = 'gst'
    c_sources = set()
    dl_sources = set()

    def __init__(self, doc_repo):
        BaseExtension.__init__(self, doc_repo)
        self.__raw_comment_parser = GtkDocParser(doc_repo)

    def setup(self):
        stale_c, unlisted_c = self.get_stale_files(GstExtension.c_sources,
            'gst-c')
        stale_dl, unlisted_dl = self.get_stale_files(GstExtension.dl_sources,
            'gst-dl')

        comments = []

        for source_file in stale_c:
            comments.extend(get_comments(source_file))

        for c in comments:
            block = self.__raw_comment_parser.parse_comment(*c)
            if block is not None:
                print block.name
                self.doc_repo.doc_database.add_comment(block)

    @staticmethod
    def add_arguments (parser):
        group = parser.add_argument_group('Gst extension', DESCRIPTION)
        GstExtension.add_index_argument(group)
        GstExtension.add_sources_argument(group, prefix='gst-c')
        GstExtension.add_sources_argument(group, prefix='gst-dl')

    @staticmethod
    def parse_config(doc_repo, config):
        GstExtension.parse_standard_config(config)
        GstExtension.c_sources = config.get_sources('gst_c_')

        GstExtension.dl_sources = config.get_sources('gst_dl_')

class PluginScanner(object):
    def __init__(self, doc_repo):
        self.registry = Gst.Registry.get()
        self.__raw_comment_parser = GtkDocParser(doc_repo)

    def print_plugin_info(self, plugin):
        print plugin.get_name()
        print plugin.get_description()
        print plugin.get_version()
        print plugin.get_license()
        print plugin.get_source()
        print plugin.get_filename()
        print plugin.get_release_date_string()
        print plugin.get_package()
        print plugin.get_origin()

    def print_factory_details_info(self, factory):
        print factory.get_rank()
        for key in factory.get_metadata_keys():
            print factory.get_metadata(key)

    def print_hierarchy(self, element):
        type_ = type(element)
        while (type_):
            print GObject.type_name(type_)
            try:
                type_ = GObject.type_parent(type_)
            except RuntimeError:
                type_ = None

    def print_interfaces(self, element):
        # TODO, try with an object that has interfaces
        interfaces = GObject.type_interfaces(type(element))

    def print_pad_templates_info(self, element, factory):
        if factory.get_num_pad_templates() == 0:
            return
        pads = factory.get_static_pad_templates()
        for pad in pads:
            print pad
            print pad.direction
            print pad.presence
            print pad.get_caps().to_string()

    def print_implementation_info(self, element):
        print type(element).do_change_state

    def print_clocking_info (self, element):
        print bool(element.flags & Gst.ElementFlags.REQUIRE_CLOCK)
        print bool(element.flags & Gst.ElementFlags.PROVIDE_CLOCK)

        if element.flags & Gst.ElementFlags.PROVIDE_CLOCK:
            print element.get_clock()

    def print_element_properties(self, element):
        for prop in GObject.list_properties(element):
            print dir(prop)
            print prop.name, prop.nick
            print prop.blurb
            print prop.flags
            print prop.value_type.name
            print dir(prop.value_type)
            # TODO: the stanky leg

    def print_plugin_feature(self, feature):
        print GObject.type_name(feature)
        if GObject.type_name(feature) == "GstElementFactory":
            #print feature.get_metadata(Gst.ELEMENT_METADATA_LONGNAME)
            #self.print_factory_details_info(feature)
            element = feature.create(None)
            #self.print_hierarchy(element)
            #self.print_interfaces(element)
            #self.print_pad_templates_info(element, feature)
            #self.print_implementation_info(element)
            #self.print_clocking_info(element)
            #self.print_element_properties(element)

    def scan(self, source_files, library):
        plugin = Gst.Plugin.load_file(library)
        #self.print_plugin_info(plugin)
        comments = []
        for source_file in source_files:
            comments.extend(get_comments(source_file))

        for c in comments:
            block = self.__raw_comment_parser.parse_comment(*c)
            if block is not None:
                continue
                print block.name

        features = self.registry.get_feature_list_by_plugin(plugin.get_name())
        for feature in features:
            self.print_plugin_feature(feature)

        return plugin

def get_extension_classes():
    return [GstExtension]
