/* GStreamer
 * Copyright (C) 2013 Stefan Sauer <ensonic@users.sf.net>
 *
 * gsttracer.h: tracing subsystem
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:gsttracer
 * @short_description: Tracing subsystem
 *
 * The tracing subsystem provides hooks in the core library and API for modules
 * to attach to them.
 *
 * Tracing modules will subclass #GstTracer and register through
 * gst_tracer_register(). Modules can attach to various hook-types - see
 * #GstTracerHook. When invoked they receive hook specific contextual data, 
 * which they must not modify.
 *
 * The user can activate tracers by setting the environment variable GST_TRACE
 * to a ';' separated list of tracers.
 */

#include "gst_private.h"
#include "gstenumtypes.h"
#include "gstregistry.h"
#include "gsttracer.h"
#include "gsttracerfactory.h"
#include "gstutils.h"

#ifndef GST_DISABLE_GST_DEBUG

GST_DEBUG_CATEGORY_EXTERN (tracer_debug);
#define GST_CAT_DEFAULT tracer_debug

/* tracing plugins base class */

enum
{
  PROP_0,
  PROP_PARAMS,
  PROP_MASK,
  PROP_LAST
};

static GParamSpec *properties[PROP_LAST];

static void gst_tracer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_tracer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

struct _GstTracerPrivate
{
  const gchar *params;
  GstTracerHook mask;
};

#define gst_tracer_parent_class parent_class
G_DEFINE_ABSTRACT_TYPE (GstTracer, gst_tracer, GST_TYPE_OBJECT);

static void
gst_tracer_class_init (GstTracerClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

  gobject_class->set_property = gst_tracer_set_property;
  gobject_class->get_property = gst_tracer_get_property;

  properties[PROP_PARAMS] =
      g_param_spec_string ("params", "Params", "Extra configuration parameters",
      NULL, G_PARAM_READWRITE | G_PARAM_CONSTRUCT | G_PARAM_STATIC_STRINGS);

  properties[PROP_MASK] =
      g_param_spec_flags ("mask", "Mask", "Event mask", GST_TYPE_TRACER_HOOK,
      GST_TRACER_HOOK_NONE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS);

  g_object_class_install_properties (gobject_class, PROP_LAST, properties);
  g_type_class_add_private (klass, sizeof (GstTracerPrivate));
}

static void
gst_tracer_init (GstTracer * tracer)
{
  tracer->priv = G_TYPE_INSTANCE_GET_PRIVATE (tracer, GST_TYPE_TRACER,
      GstTracerPrivate);
}

static void
gst_tracer_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstTracer *self = GST_TRACER_CAST (object);

  switch (prop_id) {
    case PROP_PARAMS:
      self->priv->params = g_value_get_string (value);
      break;
    case PROP_MASK:
      self->priv->mask = g_value_get_flags (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_tracer_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstTracer *self = GST_TRACER_CAST (object);

  switch (prop_id) {
    case PROP_PARAMS:
      g_value_set_string (value, self->priv->params);
      break;
    case PROP_MASK:
      g_value_set_flags (value, self->priv->mask);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_tracer_invoke (GstTracer * self, GstTracerHookId hid,
    GstTracerMessageId mid, va_list var_args)
{
  GstTracerClass *klass = GST_TRACER_GET_CLASS (self);

  g_return_if_fail (klass->invoke);

  klass->invoke (self, hid, mid, var_args);
}

/* tracing modules */

gboolean
gst_tracer_register (GstPlugin * plugin, const gchar * name, GType type)
{
  GstPluginFeature *existing_feature;
  GstRegistry *registry;
  GstTracerFactory *factory;

  g_return_val_if_fail (name != NULL, FALSE);
  g_return_val_if_fail (g_type_is_a (type, GST_TYPE_TRACER), FALSE);

  registry = gst_registry_get ();
  /* check if feature already exists, if it exists there is no need to update it
   * when the registry is getting updated, outdated plugins and all their
   * features are removed and readded.
   */
  existing_feature = gst_registry_lookup_feature (registry, name);
  if (existing_feature) {
    GST_DEBUG_OBJECT (registry, "update existing feature %p (%s)",
        existing_feature, name);
    factory = GST_TRACER_FACTORY_CAST (existing_feature);
    factory->type = type;
    existing_feature->loaded = TRUE;
    //g_type_set_qdata (type, __gst_elementclass_factory, factory);
    gst_object_unref (existing_feature);
    return TRUE;
  }

  factory = g_object_newv (GST_TYPE_TRACER_FACTORY, 0, NULL);
  GST_DEBUG_OBJECT (factory, "new tracer factory for %s", name);

  gst_plugin_feature_set_name (GST_PLUGIN_FEATURE_CAST (factory), name);
  gst_plugin_feature_set_rank (GST_PLUGIN_FEATURE_CAST (factory),
      GST_RANK_NONE);

  factory->type = type;
  GST_DEBUG_OBJECT (factory, "tracer factory for %u:%s",
      (guint) type, g_type_name (type));

  if (plugin && plugin->desc.name) {
    GST_PLUGIN_FEATURE_CAST (factory)->plugin_name = plugin->desc.name; /* interned string */
    GST_PLUGIN_FEATURE_CAST (factory)->plugin = plugin;
    g_object_add_weak_pointer ((GObject *) plugin,
        (gpointer *) & GST_PLUGIN_FEATURE_CAST (factory)->plugin);
  } else {
    GST_PLUGIN_FEATURE_CAST (factory)->plugin_name = "NULL";
    GST_PLUGIN_FEATURE_CAST (factory)->plugin = NULL;
  }
  GST_PLUGIN_FEATURE_CAST (factory)->loaded = TRUE;

  gst_registry_add_feature (gst_registry_get (),
      GST_PLUGIN_FEATURE_CAST (factory));

  return TRUE;
}

/* tracing helpers */

gboolean _priv_tracer_enabled = FALSE;
/* TODO(ensonic): use GPtrArray ? */
GList *_priv_tracers[GST_TRACER_HOOK_ID_LAST] = { NULL, };

/* Initialize the debugging system */
void
_priv_gst_tracer_init (void)
{
  const gchar *env = g_getenv ("GST_TRACE");

  if (env != NULL && *env != '\0') {
    GstRegistry *registry = gst_registry_get ();
    GstPluginFeature *feature;
    GstTracerFactory *factory;
    GstTracerHook mask;
    GstTracer *tracer;
    gchar **t = g_strsplit_set (env, ";", 0);
    gint i = 0, j;
    gchar *params;

    GST_INFO ("enabling tracers: '%s'", env);

    while (t[i]) {
      // check t[i] for params
      if ((params = strchr (t[i], '('))) {
        gchar *end = strchr (&params[1], ')');
        *params = '\0';
        params++;
        if (end)
          *end = '\0';
      } else {
        params = NULL;
      }

      GST_INFO ("checking tracer: '%s'", t[i]);

      if ((feature = gst_registry_lookup_feature (registry, t[i]))) {
        factory = GST_TRACER_FACTORY (gst_plugin_feature_load (feature));
        if (factory) {
          GST_INFO_OBJECT (factory, "creating tracer: type-id=%u",
              (guint) factory->type);

          tracer = g_object_new (factory->type, "params", params, NULL);
          g_object_get (tracer, "mask", &mask, NULL);

          if (mask) {
            /* add to lists according to mask */
            j = 0;
            while (mask && (j < GST_TRACER_HOOK_ID_LAST)) {
              if (mask & 1) {
                _priv_tracers[j] = g_list_prepend (_priv_tracers[j],
                    gst_object_ref (tracer));
                GST_WARNING_OBJECT (tracer, "added tracer to hook %d", j);
              }
              mask >>= 1;
              j++;
            }

            _priv_tracer_enabled = TRUE;
          } else {
            GST_WARNING_OBJECT (tracer,
                "tracer with zero mask won't have any effect");
          }
          gst_object_unref (tracer);
        } else {
          GST_WARNING_OBJECT (feature,
              "loading plugin containing feature %s failed!", t[i]);
        }
      } else {
        GST_WARNING ("no tracer named '%s'", t[i]);
      }
      i++;
    }
    g_strfreev (t);
  }
}

void
_priv_gst_tracer_deinit (void)
{
  gint i;
  GList *node;

  /* shutdown tracers for final reports */
  for (i = 0; i < GST_TRACER_HOOK_ID_LAST; i++) {
    for (node = _priv_tracers[i]; node; node = g_list_next (node)) {
      gst_object_unref (node->data);
    }
    g_list_free (_priv_tracers[i]);
    _priv_tracers[i] = NULL;
  }
  _priv_tracer_enabled = FALSE;
}

void
gst_tracer_dispatch (GstTracerHookId hid, GstTracerMessageId mid, ...)
{
  va_list var_args;
  GList *node;

  for (node = _priv_tracers[hid]; node; node = g_list_next (node)) {
    va_start (var_args, mid);
    gst_tracer_invoke (node->data, hid, mid, var_args);
    va_end (var_args);
  }
}

/* tracing module helpers */

void
gst_tracer_log_trace (GstStructure * s)
{
  gchar *data;

  // TODO(ensonic): use a GVariant?
  data = gst_structure_to_string (s);
  GST_TRACE ("%s", data);
  g_free (data);
  gst_structure_free (s);
}

#endif /* GST_DISABLE_GST_DEBUG */
