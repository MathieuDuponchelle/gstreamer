#include <gst/gst.h>

static gboolean
gtype_needs_ptr_marker (GType type)
{
  if (type == G_TYPE_POINTER)
    return FALSE;

  if (G_TYPE_FUNDAMENTAL (type) == G_TYPE_POINTER || G_TYPE_IS_BOXED (type)
      || G_TYPE_IS_OBJECT (type))
    return TRUE;

  return FALSE;
}

static gboolean
has_sometimes_template (GstElement * element)
{
  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);
  GList *l;

  for (l = klass->padtemplates; l != NULL; l = l->next) {
    if (GST_PAD_TEMPLATE (l->data)->presence == GST_PAD_SOMETIMES)
      return TRUE;
  }

  return FALSE;
}

static void
_add_element_signals (GString * json, GstElement * element)
{
  gboolean opened = FALSE;
  guint *signals;
  guint nsignals;
  gint i = 0, j, k;
  GSignalQuery *query = NULL;
  GType type;
  GSList *found_signals, *l;

  for (k = 0; k < 2; k++) {
    found_signals = NULL;

    /* For elements that have sometimes pads, also list a few useful GstElement
     * signals. Put these first, so element-specific ones come later. */
    if (k == 0 && has_sometimes_template (element)) {
      query = g_new0 (GSignalQuery, 1);
      g_signal_query (g_signal_lookup ("pad-added", GST_TYPE_ELEMENT), query);
      found_signals = g_slist_append (found_signals, query);
      query = g_new0 (GSignalQuery, 1);
      g_signal_query (g_signal_lookup ("pad-removed", GST_TYPE_ELEMENT), query);
      found_signals = g_slist_append (found_signals, query);
      query = g_new0 (GSignalQuery, 1);
      g_signal_query (g_signal_lookup ("no-more-pads", GST_TYPE_ELEMENT),
          query);
      found_signals = g_slist_append (found_signals, query);
    }

    for (type = G_OBJECT_TYPE (element); type; type = g_type_parent (type)) {
      if (type == GST_TYPE_ELEMENT || type == GST_TYPE_OBJECT)
        break;

      if (type == GST_TYPE_BIN && G_OBJECT_TYPE (element) != GST_TYPE_BIN)
        continue;

      signals = g_signal_list_ids (type, &nsignals);
      for (i = 0; i < nsignals; i++) {
        query = g_new0 (GSignalQuery, 1);
        g_signal_query (signals[i], query);

        if ((k == 0 && !(query->signal_flags & G_SIGNAL_ACTION)) ||
            (k == 1 && (query->signal_flags & G_SIGNAL_ACTION)))
          found_signals = g_slist_append (found_signals, query);
        else
          g_free (query);
      }
      g_free (signals);
      signals = NULL;
    }

    if (!found_signals)
      continue;

    for (l = found_signals; l; l = l->next) {
      query = (GSignalQuery *) l->data;

      g_string_append_printf (json,
          "%s{\"name\": \"%s\","
          "\"retval\": \"%s\","
          "\"args\": [",
          opened ? "," : ",\"signals\": [",
          query->signal_name, g_type_name (query->return_type));

      opened = TRUE;
      for (j = 0; j < query->n_params; j++) {
        g_string_append_printf (json, "%s\"%s%s\"",
            j ? "," : "",
            g_type_name (query->param_types[j]),
            gtype_needs_ptr_marker (query->param_types[j]) ? " *" : "");
      }
      g_string_append (json, "]}");

    }

    g_slist_foreach (found_signals, (GFunc) g_free, NULL);
    g_slist_free (found_signals);
    opened = TRUE;
  }

  if (opened)
    g_string_append (json, "]");
}

static void
_add_element_properties (GString * json, GstElement * element)
{
  guint i, n_props;
  gboolean opened = FALSE;
  GParamSpec **specs, *spec;
  GObjectClass *klass = G_OBJECT_GET_CLASS (element);

  specs = g_object_class_list_properties (klass, &n_props);

  for (i = 0; i < n_props; i++) {
    spec = specs[i];

    if (spec->owner_type != G_OBJECT_TYPE (element))
      continue;
    if (!opened)
      g_string_append (json, "\"properties\": [");

    g_string_append_printf (json, "%s{ \"name\": \"%s\","
        "\"construct-only\": %s,"
        "\"construct\": %s,"
        "\"writable\": %s,"
        "\"type-name\": \"%s%s\"}",
        opened ? "," : "",
        spec->name,
        spec->flags & G_PARAM_CONSTRUCT_ONLY ? "true" : "false",
        spec->flags & G_PARAM_CONSTRUCT ? "true" : "false",
        spec->flags & G_PARAM_WRITABLE ? "true" : "false",
        g_type_name (G_PARAM_SPEC_VALUE_TYPE (spec)),
        gtype_needs_ptr_marker (spec->value_type) ? " *" : "");

    opened = TRUE;
  }

  if (opened)
    g_string_append (json, "]");

}

static void
_add_element_pad_templates (GString * json, GstElementFactory * factory)
{
  gboolean opened = FALSE;
  const GList *pads;
  GstStaticPadTemplate *padtemplate;
  GRegex *re = g_regex_new ("%", 0, 0, NULL);

  pads = gst_element_factory_get_static_pad_templates (factory);
  while (pads) {
    gchar *name, *caps;
    padtemplate = (GstStaticPadTemplate *) (pads->data);
    pads = g_list_next (pads);

    name = g_regex_replace (re, padtemplate->name_template,
        -1, 0, "%%", 0, NULL);;
    caps = gst_caps_to_string (gst_static_caps_get (&padtemplate->static_caps));
    g_string_append_printf (json, "%s"
        "{\"name\": \"%s\","
        "\"caps\": \"%s\","
        "\"direction\": \"%s\","
        "\"presence\": \"%s\"}",
        opened ? "," : ",\"pad-templates\": [",
        name, caps,
        padtemplate->direction ==
        GST_PAD_SRC ? "src" : padtemplate->direction ==
        GST_PAD_SINK ? "sink" : "unknown",
        padtemplate->presence ==
        GST_PAD_ALWAYS ? "always" : padtemplate->presence ==
        GST_PAD_SOMETIMES ? "sometimes" : padtemplate->presence ==
        GST_PAD_REQUEST ? "request" : "unknown");
    opened = TRUE;
    g_free (name);
  }
  if (opened)
    g_string_append (json, "]");

  g_regex_unref (re);
}

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
      "\"hierarchy\": [", GST_OBJECT_NAME (feature),
      gst_plugin_feature_get_rank (feature));

  for (type = G_OBJECT_TYPE (element);; type = g_type_parent (type)) {
    g_string_append_printf (json, "\"%s\"%c", g_type_name (type),
        type == G_TYPE_OBJECT ? ' ' : ',');

    if (type == G_TYPE_OBJECT)
      break;
  }
  g_string_append (json, "],");
  _add_element_properties (json, element);
  _add_element_signals (json, element);
  _add_element_pad_templates (json, GST_ELEMENT_FACTORY (feature));

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
