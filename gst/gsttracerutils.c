/* GStreamer
 * Copyright (C) 2013 Stefan Sauer <ensonic@users.sf.net>
 *
 * gsttracerutils.c: tracing subsystem
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
 * SECTION:gsttracerutils
 * @short_description: Tracing subsystem
 *
 * The tracing subsystem provides hooks in the core library and API for modules
 * to attach to them.
 *
 * The user can activate tracers by setting the environment variable GST_TRACE
 * to a ';' separated list of tracers.
 */

#define GST_USE_UNSTABLE_API

#include "gst_private.h"
#include "gsttracer.h"
#include "gsttracerfactory.h"
#include "gsttracerutils.h"

#ifndef GST_DISABLE_GST_DEBUG

/* tracer quarks */

/* These strings must match order and number declared in the GstTracerQuarkId
 * enum in gsttracerutils.h! */
static const gchar *_quark_strings[] = {
  "pad-push-pre", "pad-push-post", "pad-push-list-pre", "pad-push-list-post",
  "pad-pull-range-pre", "pad-pull-range-post", "pad-push-event-pre",
  "pad-push-event-post", "element-post-message-pre",
  "element-post-message-post", "element-query-pre", "element-query-post",
  "element-add-pad-pre", "element-add-pad-post"
};

GQuark _priv_gst_tracer_quark_table[GST_TRACER_QUARK_MAX];

/* tracing helpers */

gboolean _priv_tracer_enabled = FALSE;
GHashTable *_priv_tracers = NULL;

/**
 * gst_tracer_factory_create:
 * @factory: factory to instantiate
 * @params: (allow-none): params for new tracer, or %NULL
 *
 * Create a new tracer of the type defined by the given tracerfactory.
 * It will be given the parameters supplied.
 *
 * Returns: (transfer floating) (nullable): new #GstTracer or %NULL
 *     if the tracer couldn't be created
 */
GstTracer *
gst_tracer_factory_create (GstTracerFactory * factory, const gchar * params)
{
  GstTracer *tracer;
  GstTracerFactory *newfactory;

  g_return_val_if_fail (factory != NULL, NULL);

  newfactory =
      GST_TRACER_FACTORY (gst_plugin_feature_load (GST_PLUGIN_FEATURE
          (factory)));

  if (newfactory == NULL)
    goto load_failed;

  factory = newfactory;

  if (params)
    GST_INFO ("creating tracer \"%s\" with params \"%s\"",
        GST_OBJECT_NAME (factory), GST_STR_NULL (params));
  else
    GST_INFO ("creating element \"%s\"", GST_OBJECT_NAME (factory));

  if (factory->type == 0)
    goto no_type;

  /* create an instance of the element, cast so we don't assert on NULL
   * also set params as early as we can
   */
  tracer =
      GST_TRACER_CAST (g_object_new (factory->type, "params", params, NULL));
  if (G_UNLIKELY (tracer == NULL))
    goto no_tracer;

  GST_DEBUG ("created tracer \"%s\"", GST_OBJECT_NAME (factory));

  return tracer;

  /* ERRORS */
load_failed:
  {
    GST_WARNING_OBJECT (factory, "loading plugin returned NULL!");
    return NULL;
  }
no_type:
  {
    GST_WARNING_OBJECT (factory, "factory has no type");
    gst_object_unref (factory);
    return NULL;
  }
no_tracer:
  {
    GST_WARNING_OBJECT (factory, "could not create tracer");
    gst_object_unref (factory);
    return NULL;
  }
}

/**
 * gst_tracer_factory_find:
 * @name: name of factory to find
 *
 * Search for a tracer factory of the given name. Refs the returned
 * tracer factory; caller is responsible for unreffing.
 *
 * Returns: (transfer full) (nullable): #GstTracerFactory if found,
 * %NULL otherwise
 */
GstTracerFactory *
gst_tracer_factory_find (const gchar * name)
{
  GstPluginFeature *feature;

  g_return_val_if_fail (name != NULL, NULL);

  feature = gst_registry_find_feature (gst_registry_get (), name,
      GST_TYPE_TRACER_FACTORY);
  if (feature)
    return GST_TRACER_FACTORY (feature);

  /* this isn't an error, for instance when you query if an element factory is
   * present */
  GST_LOG ("no such element factory \"%s\"", name);
  return NULL;
}

/**
 * gst_tracer_factory_make:
 * @tracername: a named factory to instantiate
 * @params: (allow-none): params for the new tracer, that will be set at
 * construction time.
 *
 * Create a new tracer of the type defined by the given tracer factory name.
 *
 * Returns: (transfer floating) (nullable): new #GstTracer or %NULL
 * if unable to create tracer
 */
GstTracer *
gst_tracer_factory_make (const gchar * factoryname, const gchar * name)
{
  GstTracerFactory *factory;
  GstTracer *tracer;

  g_return_val_if_fail (factoryname != NULL, NULL);

  GST_LOG ("gsttracerfactory: make \"%s\" \"%s\"",
      factoryname, GST_STR_NULL (name));

  factory = gst_tracer_factory_find (factoryname);
  if (factory == NULL)
    goto no_factory;

  GST_LOG_OBJECT (factory, "found factory %p", factory);
  tracer = gst_tracer_factory_create (factory, name);
  if (tracer == NULL)
    goto create_failed;

  gst_object_unref (factory);
  return tracer;

  /* ERRORS */
no_factory:
  {
    GST_INFO ("no such tracer factory \"%s\"!", factoryname);
    return NULL;
  }
create_failed:
  {
    GST_INFO_OBJECT (factory, "couldn't create instance!");
    gst_object_unref (factory);
    return NULL;
  }
}

/* Initialize the tracing system */
void
_priv_gst_tracing_init (void)
{
  const gchar *env = g_getenv ("GST_TRACE");
  GstTracer *tracer;
  gint i = 0;

  _priv_tracers = g_hash_table_new (NULL, NULL);
  if (G_N_ELEMENTS (_quark_strings) != GST_TRACER_QUARK_MAX)
    g_warning ("the quark table is not consistent! %d != %d",
        (gint) G_N_ELEMENTS (_quark_strings), GST_TRACER_QUARK_MAX);

  for (i = 0; i < GST_TRACER_QUARK_MAX; i++) {
    _priv_gst_tracer_quark_table[i] =
        g_quark_from_static_string (_quark_strings[i]);
  }

  if (env != NULL && *env != '\0') {
    gchar **t = g_strsplit_set (env, ";", 0);
    gchar *params;

    GST_INFO ("enabling tracers: '%s'", env);


    i = 0;
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

      tracer = gst_tracer_factory_make (t[i], params);
      if (tracer) {
        /* tracers register them self to the hooks */
        gst_object_unref (tracer);
      } else {
        GST_WARNING ("no tracer named '%s'", t[i]);
      }
      i++;
    }
    g_strfreev (t);
  }
}

void
_priv_gst_tracing_deinit (void)
{
  GList *h_list, *h_node, *t_node;
  GstTracerHook *hook;

  _priv_tracer_enabled = FALSE;
  if (!_priv_tracers)
    return;

  /* shutdown tracers for final reports */
  h_list = g_hash_table_get_values (_priv_tracers);
  for (h_node = h_list; h_node; h_node = g_list_next (h_node)) {
    for (t_node = h_node->data; t_node; t_node = g_list_next (t_node)) {
      hook = (GstTracerHook *) t_node->data;
      gst_object_unref (hook->tracer);
      g_slice_free (GstTracerHook, hook);
    }
    g_list_free (h_node->data);
  }
  g_list_free (h_list);
  g_hash_table_destroy (_priv_tracers);
  _priv_tracers = NULL;
}

static void
gst_tracing_register_hook_full (GstTracer * tracer, GQuark detail,
    GCallback func, gpointer target)
{
  gpointer key = GINT_TO_POINTER (detail);
  GList *list = g_hash_table_lookup (_priv_tracers, key);
  GstTracerHook *hook = g_slice_new0 (GstTracerHook);
  hook->tracer = gst_object_ref (tracer);
  hook->func = func;
  hook->target = target;

  list = g_list_prepend (list, hook);
  g_hash_table_replace (_priv_tracers, key, list);
  GST_DEBUG ("registering tracer for '%s', list.len=%d",
      (detail ? g_quark_to_string (detail) : "*"), g_list_length (list));
  _priv_tracer_enabled = TRUE;
}

/**
 * gst_tracing_register_hook_id:
 * @tracer: the tracer
 * @detail: the detailed hook
 * @func: (scope async): the callback
 *
 * Register @func to be called when the trace hook @detail is getting invoked.
 */
void
gst_tracing_register_hook_id (GstTracer * tracer, GQuark detail, GCallback func)
{
  gst_tracing_register_hook_full (tracer, detail, func, NULL);
}

/**
 * gst_tracing_register_hook_id_for_target:
 * @tracer: the tracer
 * @detail: the detailed hook
 * @func: (scope async): the callback
 * @target: the object that triggers the hook, for example for pad-push-pre, the
 * hook will be invoked only if the pad is the target.
 *
 * Register @func to be called when the trace hook @detail is getting invoked.
 */
void
gst_tracing_register_hook_id_for_target (GstTracer * tracer, GQuark detail,
    GCallback func, gpointer target)
{
  gst_tracing_register_hook_full (tracer, detail, func, target);
}

/**
 * gst_tracing_register_hook:
 * @tracer: the tracer
 * @detail: the detailed hook
 * @func: (scope async): the callback
 *
 * Register @func to be called when the trace hook @detail is getting invoked.
 */
void
gst_tracing_register_hook (GstTracer * tracer, const gchar * detail,
    GCallback func)
{
  gst_tracing_register_hook_id (tracer, g_quark_try_string (detail), func);
}

/**
 * gst_tracing_register_hook:
 * @tracer: the tracer
 * @detail: the detailed hook
 * @func: (scope async): the callback
 * @target: the object that triggers the hook, for example for pad-push-pre, the
 * hook will be invoked only if the pad is the target.
 *
 * Register @func to be called when the trace hook @detail is getting invoked.
 */
void
gst_tracing_register_hook_for_target (GstTracer * tracer, const gchar * detail,
    GCallback func, gpointer target)
{
  gst_tracing_register_hook_id_for_target (tracer, g_quark_try_string (detail),
      func, target);
}

#endif /* GST_DISABLE_GST_DEBUG */
