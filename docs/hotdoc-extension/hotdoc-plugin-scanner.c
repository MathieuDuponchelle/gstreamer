#include <gst/gst.h>

static void
_add_element_details (GString * json, GstPluginFeature * feature)
{
  GType type;
  GstElement *element =
      gst_element_factory_create (GST_ELEMENT_FACTORY (feature), NULL);

  g_string_append_printf (json,
      "{"
      "\"name\":\"%s\","
      "\"rank\":%d,"
      "\"hiearchy\": [", GST_OBJECT_NAME (feature),
      gst_plugin_feature_get_rank (feature));

  for (type = G_OBJECT_TYPE (element);; type = g_type_parent (type)) {
    g_string_append_printf (json, "\"%s\"%c", g_type_name (type),
        type == G_TYPE_OBJECT ? ' ' : ',');

    if (type == G_TYPE_OBJECT)
      break;
  }
  g_string_append (json, "]");

  g_string_append (json, "}");
}

int
main (int argc, char *argv[])
{
  gchar *libfile;
  GError *error = NULL;
  GString *json;
  GstPlugin *plugin;
  gboolean f = TRUE;
  GList *features, *tmp;
  gint nargs = 2;
  static gchar *args[] =
      { "hotdoc-plugin-scanner", "--gst-disable-registry-fork", NULL };

  g_assert (argc == 2);

  g_unsetenv ("GST_PLUGIN_SYSTEM_PATH");
  g_unsetenv ("GST_REGISTRY");
  g_unsetenv ("GST_PLUGIN_PATH");
  g_unsetenv ("GST_PLUGIN_SYSTEM_PATH_1_0");
  g_unsetenv ("GST_PLUGIN_PATH_1_0");

  gst_init (NULL, NULL);
  libfile = argv[1];
  plugin = gst_plugin_load_file (libfile, &error);
  if (!plugin) {
    g_printerr ("%s could not be loaded as a GstPlugin: %s", libfile,
        error->message ? error->message : "no known reasons");
    g_clear_error (&error);

    return -1;
  }

  json = g_string_new (NULL);

  g_string_append_printf (json, "{\"name\":\"%s\","
      "\"description\":\"%s\","
      "\"filename\":\"%s\","
      "\"version\":\"%s\","
      "\"source\":\"%s\","
      "\"package\":\"%s\","
      "\"license\":\"%s\","
      "\"url\":\"%s\","
      "\"elements\":[",
      gst_plugin_get_name (plugin),
      gst_plugin_get_description (plugin),
      libfile,
      gst_plugin_get_version (plugin),
      gst_plugin_get_source (plugin),
      gst_plugin_get_package (plugin),
      gst_plugin_get_license (plugin), gst_plugin_get_origin (plugin));

  features =
      gst_registry_get_feature_list_by_plugin (gst_registry_get (),
      gst_plugin_get_name (plugin));

  for (tmp = features; tmp; tmp = tmp->next) {
    GstPluginFeature *feature = tmp->data;
    if (GST_IS_ELEMENT_FACTORY (feature)) {

      if (!f)
        g_string_append_printf (json, ",");
      f = FALSE;
      _add_element_details (json, feature);
    }
  }

  g_string_append_printf (json, "]}\n");
  g_print (json->str);

  return 0;
}
