#include <string.h>
#include <gst/gst.h>

static GRegex *cleanup_caps_field = NULL;

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

static gchar *
json_strescape (const gchar * str)
{
  const gchar *p;
  const gchar *end;
  GString *output;
  gsize len;

  if (!str)
    return g_strdup ("NULL");

  len = strlen (str);
  end = str + len;
  output = g_string_sized_new (len);

  for (p = str; p < end; p++) {
    if (*p == '\\' || *p == '"') {
      g_string_append_c (output, '\\');
      g_string_append_c (output, *p);
    } else if (*p == '%') {
      g_string_append_c (output, '%');
      g_string_append_c (output, *p);
    } else if ((*p > 0 && *p < 0x1f) || *p == 0x7f) {
      switch (*p) {
        case '\b':
          g_string_append (output, "\\b");
          break;
        case '\f':
          g_string_append (output, "\\f");
          break;
        case '\n':
          g_string_append (output, "\\n");
          break;
        case '\r':
          g_string_append (output, "\\r");
          break;
        case '\t':
          g_string_append (output, "\\t");
          break;
        default:
          g_string_append_printf (output, "\\u00%02x", (guint) * p);
          break;
      }
    } else {
      g_string_append_c (output, *p);
    }
  }

  return g_string_free (output, FALSE);
}

static gchar *
flags_to_string (GFlagsValue * values, guint flags)
{
  GString *s = NULL;
  guint flags_left, i;

  /* first look for an exact match and count the number of values */
  for (i = 0; values[i].value_name != NULL; ++i) {
    if (values[i].value == flags)
      return g_strdup (values[i].value_nick);
  }

  s = g_string_new (NULL);

  /* we assume the values are sorted from lowest to highest value */
  flags_left = flags;
  while (i > 0) {
    --i;
    if (values[i].value != 0
        && (flags_left & values[i].value) == values[i].value) {
      if (s->len > 0)
        g_string_append_c (s, '+');
      g_string_append (s, values[i].value_nick);
      flags_left -= values[i].value;
      if (flags_left == 0)
        break;
    }
  }

  if (s->len == 0)
    g_string_assign (s, "(none)");

  return g_string_free (s, FALSE);
}


static void
_serialize_flags (GString * json, const gchar * key_name, GType gtype,
    GValue * value)
{
  GFlagsValue *values = G_FLAGS_CLASS (g_type_class_ref (gtype))->values;

  if (value) {
    gchar *cur;

    cur = flags_to_string (values, g_value_get_flags (value));
    g_string_append_printf (json, ",\"default\": \"%s\",", cur);
    g_free (cur);
  }

  g_string_append_printf (json, "\"%s\": [", key_name);

  while (values[0].value_name) {
    gchar *value_name = json_strescape (values[0].value_name);
    g_string_append_printf (json, "{\"name\": \"%s\","
        "\"value\": \"0x%08x\","
        "\"desc\": \"%s\"}", values[0].value_nick, values[0].value, value_name);
    ++values;

    if (values[0].value_name)
      g_string_append_c (json, ',');
  }
  g_string_append_c (json, ']');
}

static void
_serialize_enum (GString * json, const gchar * key_name, GType gtype,
    GValue * value)
{
  GEnumValue *values;
  guint j = 0;
  gint enum_value;
  const gchar *value_nick = "";

  values = G_ENUM_CLASS (g_type_class_ref (gtype))->values;

  if (value) {
    enum_value = g_value_get_enum (value);
    while (values[j].value_name) {
      if (values[j].value == enum_value)
        value_nick = values[j].value_nick;
      j++;
    }
    g_string_append_printf (json, ",\"default\": \"%s (%d)\","
        "\"enum\": true,", value_nick, enum_value);;
  }

  g_string_append_printf (json, "\"%s\": [", key_name);

  j = 0;
  while (values[j].value_name) {
    gchar *value_name = json_strescape (values[j].value_name);

    g_string_append_printf (json, "{\"name\": \"%s\","
        "\"value\": \"0x%08x\","
        "\"desc\": \"%s\"}", values[j].value_nick, values[j].value, value_name);
    j++;
    if (values[j].value_name)
      g_string_append_c (json, ',');
  }

  g_string_append_c (json, ']');
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
          "%s\"%s\" : {"
          "\"retval\": \"%s\","
          "\"args\": [",
          opened ? "," : ",\"signals\": {",
          query->signal_name, g_type_name (query->return_type));

      opened = TRUE;
      for (j = 0; j < query->n_params; j++) {
        g_string_append_printf (json, "%s\"%s%s\"",
            j ? "," : "",
            g_type_name (query->param_types[j]),
            gtype_needs_ptr_marker (query->param_types[j]) ? " *" : "");
      }
      g_string_append_c (json, ']');

      if (g_type_is_a (query->return_type, G_TYPE_ENUM)) {
        g_string_append_c (json, ',');
        _serialize_enum (json, "return-values", query->return_type, NULL);
      } else if (g_type_is_a (query->return_type, G_TYPE_FLAGS)) {
        g_string_append_c (json, ',');
        _serialize_flags (json, "return-values", query->return_type, NULL);
      }

      g_string_append_c (json, '}');
    }

    g_slist_foreach (found_signals, (GFunc) g_free, NULL);
    g_slist_free (found_signals);
    opened = TRUE;
  }

  if (opened)
    g_string_append (json, "}");
}

static void
_add_element_properties (GString * json, GstElement * element)
{
  gchar *tmpstr;
  guint i, n_props;
  gboolean opened = FALSE;
  GParamSpec **specs, *spec;
  GObjectClass *klass = G_OBJECT_GET_CLASS (element);

  specs = g_object_class_list_properties (klass, &n_props);

  for (i = 0; i < n_props; i++) {
    GValue value = { 0, };
    spec = specs[i];

    g_value_init (&value, spec->value_type);
    if (spec->flags & G_PARAM_READABLE) {
      g_object_get_property (G_OBJECT (element), spec->name, &value);
    } else {
      /* if we can't read the property value, assume it's set to the default
       * (which might not be entirely true for sub-classes, but that's an
       * unlikely corner-case anyway) */
      g_param_value_set_default (spec, &value);
    }

    if (!opened)
      g_string_append (json, ",\"properties\": {");

    tmpstr = json_strescape (g_param_spec_get_blurb (spec));
    g_string_append_printf (json,
        "%s"
        "\"%s\": {"
        "\"construct-only\": %s,"
        "\"construct\": %s,"
        "\"writable\": %s,"
        "\"blurb\": \"%s\","
        "\"type-name\": \"%s%s\"",
        opened ? "," : "",
        spec->name,
        spec->flags & G_PARAM_CONSTRUCT_ONLY ? "true" : "false",
        spec->flags & G_PARAM_CONSTRUCT ? "true" : "false",
        spec->flags & G_PARAM_WRITABLE ? "true" : "false",
        tmpstr,
        g_type_name (G_PARAM_SPEC_VALUE_TYPE (spec)),
        gtype_needs_ptr_marker (spec->value_type) ? " *" : "");
    g_free (tmpstr);

    switch (G_VALUE_TYPE (&value)) {
      case G_TYPE_STRING:
      {
        const char *string_val = g_value_get_string (&value);
        gchar *tmpstr = json_strescape (string_val);

        g_string_append_printf (json, ",\"default\": \"%s\"", tmpstr);;
        g_free (tmpstr);
        break;
      }
      case G_TYPE_BOOLEAN:
      {
        gboolean bool_val = g_value_get_boolean (&value);

        g_string_append_printf (json, ",\"default\": \"%s\"",
            bool_val ? "true" : "false");
        break;
      }
      case G_TYPE_ULONG:
      {
        GParamSpecULong *pulong = G_PARAM_SPEC_ULONG (spec);

        g_string_append_printf (json,
            ",\"default\": \"%lu\""
            ",\"min\": \"%lu\""
            ",\"max\": \"%lu\"",
            g_value_get_ulong (&value), pulong->minimum, pulong->maximum);

        GST_ERROR ("%s: property '%s' of type ulong: consider changing to "
            "uint/uint64", GST_OBJECT_NAME (element),
            g_param_spec_get_name (spec));
        break;
      }
      case G_TYPE_LONG:
      {
        GParamSpecLong *plong = G_PARAM_SPEC_LONG (spec);

        g_string_append_printf (json,
            ",\"default\": \"%ld\""
            ",\"min\": \"%ld\""
            ",\"max\": \"%ld\"",
            g_value_get_long (&value), plong->minimum, plong->maximum);

        GST_ERROR ("%s: property '%s' of type long: consider changing to "
            "int/int64", GST_OBJECT_NAME (element),
            g_param_spec_get_name (spec));
        break;
      }
      case G_TYPE_UINT:
      {
        GParamSpecUInt *puint = G_PARAM_SPEC_UINT (spec);

        g_string_append_printf (json,
            ",\"default\": \"%d\""
            ",\"min\": \"%d\""
            ",\"max\": \"%d\"",
            g_value_get_uint (&value), puint->minimum, puint->maximum);
        break;
      }
      case G_TYPE_INT:
      {
        GParamSpecInt *pint = G_PARAM_SPEC_INT (spec);

        g_string_append_printf (json,
            ",\"default\": \"%d\""
            ",\"min\": \"%d\""
            ",\"max\": \"%d\"",
            g_value_get_int (&value), pint->minimum, pint->maximum);
        break;
      }
      case G_TYPE_UINT64:
      {
        GParamSpecUInt64 *puint64 = G_PARAM_SPEC_UINT64 (spec);

        g_string_append_printf (json,
            ",\"default\": \"%" G_GUINT64_FORMAT
            "\",\"min\": \"%" G_GUINT64_FORMAT
            "\",\"max\": \"%" G_GUINT64_FORMAT "\"",
            g_value_get_uint64 (&value), puint64->minimum, puint64->maximum);
        break;
      }
      case G_TYPE_INT64:
      {
        GParamSpecInt64 *pint64 = G_PARAM_SPEC_INT64 (spec);

        g_string_append_printf (json,
            ",\"default\": \"%" G_GUINT64_FORMAT
            "\",\"min\": \"%" G_GINT64_FORMAT
            "\",\"max\": \"%" G_GINT64_FORMAT "\"",
            g_value_get_int64 (&value), pint64->minimum, pint64->maximum);
        break;
      }
      case G_TYPE_FLOAT:
      {
        GParamSpecFloat *pfloat = G_PARAM_SPEC_FLOAT (spec);

        g_string_append_printf (json,
            ",\"default\": \"%g\""
            ",\"min\": \"%g\""
            ",\"max\": \"%g\"",
            g_value_get_float (&value), pfloat->minimum, pfloat->maximum);
        break;
      }
      case G_TYPE_DOUBLE:
      {
        GParamSpecDouble *pdouble = G_PARAM_SPEC_DOUBLE (spec);

        g_string_append_printf (json,
            ",\"default\": \"%g\""
            ",\"min\": \"%g\""
            ",\"max\": \"%g\"",
            g_value_get_double (&value), pdouble->minimum, pdouble->maximum);
        break;
      }
      case G_TYPE_CHAR:
      case G_TYPE_UCHAR:
        GST_ERROR ("%s: property '%s' of type char: consider changing to "
            "int/string", GST_OBJECT_NAME (element),
            g_param_spec_get_name (spec));
        /* fall through */
      default:
        if (spec->value_type == GST_TYPE_CAPS) {
          const GstCaps *caps = gst_value_get_caps (&value);

          if (caps) {
            gchar *capsstr = gst_caps_to_string (caps);
            gchar *tmpcapsstr = json_strescape (capsstr);

            g_string_append_printf (json, ",\"default\": \"%s\"", tmpcapsstr);
            g_free (capsstr);
            g_free (tmpcapsstr);
          }
        } else if (G_IS_PARAM_SPEC_ENUM (spec)) {
          _serialize_enum (json, "values", spec->value_type, &value);
        } else if (G_IS_PARAM_SPEC_FLAGS (spec)) {
          _serialize_flags (json, "values", spec->value_type, &value);
        } else if (G_IS_PARAM_SPEC_BOXED (spec)) {
          if (spec->value_type == GST_TYPE_STRUCTURE) {
            const GstStructure *s = gst_value_get_structure (&value);
            if (s) {
              gchar *str = gst_structure_to_string (s);
              gchar *tmpstr = json_strescape (str);

              g_string_append_printf (json, ",\"default\": \"%s\"", tmpstr);
              g_free (str);
              g_free (tmpstr);
            }
          }
        } else if (GST_IS_PARAM_SPEC_FRACTION (spec)) {
          GstParamSpecFraction *pfraction = GST_PARAM_SPEC_FRACTION (spec);

          g_string_append_printf (json,
              ",\"default\": \"%d/%d\""
              ",\"min\": \"%d/%d\""
              ",\"max\": \"%d/%d\"",
              gst_value_get_fraction_numerator (&value),
              gst_value_get_fraction_denominator (&value),
              pfraction->min_num, pfraction->min_den,
              pfraction->max_num, pfraction->max_den);
        }
        break;
    }

    g_string_append_c (json, '}');


    opened = TRUE;
  }

  if (opened)
    g_string_append (json, "}");

}

static gboolean
print_field (GQuark field, const GValue * value, GString * jcaps)
{
  gint n;
  gchar *tmp, *i, *str = gst_value_serialize (value);

  if (!g_strcmp0 (g_quark_to_string (field), "format") ||
      !g_strcmp0 (g_quark_to_string (field), "rate")) {
    if (!cleanup_caps_field)
      cleanup_caps_field = g_regex_new ("\\(string\\)|\\(rate\\)", 0, 0, NULL);

    tmp = str;
    str = g_regex_replace (cleanup_caps_field, str, -1, 0, "", 0, NULL);;
    g_free (tmp);
  }

  g_string_append_printf (jcaps, "%10s: ", g_quark_to_string (field));
  tmp = str;
  for (i = tmp, n = 0; *i != '\0'; i++, n++) {
    if (*i == ' ') {
      g_string_append_len (jcaps, tmp, n);
      g_string_append_printf (jcaps, "\n%8s", "");
      tmp = i;
      n = 0;
    }
  }
  g_string_append_printf (jcaps, "%s\n", tmp);

  g_free (str);
  return TRUE;
}

static gchar *
_build_caps (const GstCaps * caps)
{
  guint i;
  gchar *res;
  GString *jcaps = g_string_new (NULL);

  if (gst_caps_is_any (caps)) {
    g_string_append (jcaps, "ANY");
    return g_string_free (jcaps, FALSE);
  }

  if (gst_caps_is_empty (caps)) {
    g_string_append (jcaps, "EMPTY");
    return g_string_free (jcaps, FALSE);
  }

  for (i = 0; i < gst_caps_get_size (caps); i++) {
    GstStructure *structure = gst_caps_get_structure (caps, i);
    GstCapsFeatures *features = gst_caps_get_features (caps, i);

    if (features && (gst_caps_features_is_any (features) ||
            !gst_caps_features_is_equal (features,
                GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY))) {
      gchar *features_string = gst_caps_features_to_string (features);

      g_string_append_printf (jcaps, "%s%s(%s)\n",
          i ? "\n" : "", gst_structure_get_name (structure), features_string);
      g_free (features_string);
    } else {
      g_string_append_printf (jcaps, "%s\n",
          gst_structure_get_name (structure));
    }
    gst_structure_foreach (structure, (GstStructureForeachFunc) print_field,
        jcaps);
  }

  res = json_strescape (jcaps->str);
  g_string_free (jcaps, TRUE);

  return res;
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
    caps = _build_caps (gst_static_caps_get (&padtemplate->static_caps));
    g_string_append_printf (json, "%s"
        "\"%s\": {"
        "\"caps\": \"%s\","
        "\"direction\": \"%s\","
        "\"presence\": \"%s\"}",
        opened ? "," : ",\"pad-templates\": {",
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
    g_string_append (json, "}");

  g_regex_unref (re);
}

static void
_add_element_details (GString * json, GstPluginFeature * feature)
{
  GType type;
  GstElement *element =
      gst_element_factory_create (GST_ELEMENT_FACTORY (feature), NULL);
  GstElementClass *eklass = GST_ELEMENT_GET_CLASS (element);
  gchar *authors = json_strescape (gst_element_class_get_metadata (eklass,
          GST_ELEMENT_METADATA_AUTHOR));
  gchar *desc = json_strescape (gst_element_class_get_metadata (eklass,
          GST_ELEMENT_METADATA_DESCRIPTION));

  g_assert (element);

  g_string_append_printf (json,
      "\"%s\": {"
      "\"rank\":%d,"
      "\"classification\":\"%s\","
      "\"author\":\"%s\","
      "\"description\":\"%s\","
      "\"hierarchy\": [", GST_OBJECT_NAME (feature),
      gst_plugin_feature_get_rank (feature),
      gst_element_class_get_metadata (eklass, GST_ELEMENT_METADATA_KLASS),
      authors, desc);
  g_free (authors);
  g_free (desc);

  for (type = G_OBJECT_TYPE (element);; type = g_type_parent (type)) {
    g_string_append_printf (json, "\"%s\"%c", g_type_name (type),
        type == G_TYPE_OBJECT ? ' ' : ',');

    if (type == G_TYPE_OBJECT)
      break;
  }
  g_string_append (json, "]");
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
  gint i;
  gboolean first = TRUE;

  g_assert (argc >= 2);

  g_setenv ("GST_REGISTRY_FORK", "no", TRUE);
  gst_init (NULL, NULL);

  json = g_string_new ("{");
  for (i = 1; i < argc; i++) {
    gchar *basename;
    libfile = argv[i];
    plugin = gst_plugin_load_file (libfile, &error);
    if (!plugin) {
      g_printerr ("%s could not be loaded as a GstPlugin: %s", libfile,
          error->message ? error->message : "no known reasons");
      g_clear_error (&error);

      continue;
    }

    basename = g_filename_display_basename (libfile);
    g_string_append_printf (json,
        "%s\"%s\": {"
        "\"description\":\"%s\","
        "\"filename\":\"%s\","
        "\"source\":\"%s\","
        "\"package\":\"%s\","
        "\"license\":\"%s\","
        "\"url\":\"%s\","
        "\"elements\":{",
        first ? "" : ",",
        gst_plugin_get_name (plugin),
        gst_plugin_get_description (plugin),
        basename,
        gst_plugin_get_source (plugin),
        gst_plugin_get_package (plugin),
        gst_plugin_get_license (plugin), gst_plugin_get_origin (plugin));
    g_free (basename);
    first = FALSE;

    features =
        gst_registry_get_feature_list_by_plugin (gst_registry_get (),
        gst_plugin_get_name (plugin));

    f = TRUE;
    for (tmp = features; tmp; tmp = tmp->next) {
      GstPluginFeature *feature = tmp->data;
      if (GST_IS_ELEMENT_FACTORY (feature)) {

        if (!f)
          g_string_append_printf (json, ",");
        f = FALSE;
        _add_element_details (json, feature);
      }
    }
    g_string_append (json, "}}");
  }

  g_string_append_c (json, '}');
  g_print ("%s", json->str);

  return 0;
}
