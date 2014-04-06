/* GStreamer
 * Copyright (C) 2014 Mathieu Duponchelle <mathieu.duponchelle@oencreed.com>
 * Copyright (C) 2014 Thibault Saunier <tsaunier@opencreed.com>
 *
 * gstbase_aggregator.c:
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "gstbaseaggregator.h"

#include "../../../gst/glib-compat-private.h"

GST_DEBUG_CATEGORY_STATIC (base_aggregator_debug);
#define GST_CAT_DEFAULT base_aggregator_debug

/********************************
 * GstBaseAggregator implementation  *
 ********************************/
#define parent_class gst_base_aggregator_parent_class
G_DEFINE_ABSTRACT_TYPE (GstBaseAggregator, gst_base_aggregator,
    GST_TYPE_ELEMENT);

#define AGGEGATE_LOCK(self)   g_rec_mutex_lock(&((GstBaseAggregator*)self)->priv->aggregate_lock)
#define AGGEGATE_UNLOCK(self) g_rec_mutex_unlock(&((GstBaseAggregator*)self)->priv->aggregate_lock)

struct _GstBaseAggregatorPrivate
{
  gint padcount;

  GstTask *aggregate_task;
  GRecMutex aggregate_lock;
};

static void
gst_base_aggregator_finalize (GObject * object)
{
}

static GstPad *
gst_base_aggregator_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name, const GstCaps * caps)
{
  GstBaseAggregator *agg;
  GstBaseAggregatorPad *agg_pad;

  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);
  GstBaseAggregatorPrivate *priv = GST_BASE_AGGREGATOR (element)->priv;

  agg = GST_BASE_AGGREGATOR (element);

  if (templ == gst_element_class_get_pad_template (klass, "sink_%u")) {
    gchar *name = NULL;

    GST_OBJECT_LOCK (element);
    /* create new pad with the name */
    name = g_strdup_printf ("sink_%u", (agg->priv->padcount)++);
    agg_pad =
        g_object_new (GST_TYPE_BASE_AGGREGATOR_PAD, "name", name, "direction",
        GST_PAD_SINK, "template", templ, NULL);
    g_free (name);
    GST_OBJECT_UNLOCK (element);
  } else {
    return NULL;
  }

  GST_ERROR_OBJECT (element, "Adding pad %s", GST_PAD_NAME (agg_pad));

  if (priv->aggregate_task->running)
    gst_pad_set_active (GST_PAD (agg_pad), TRUE);
  /* add the pad to the element */
  gst_element_add_pad (element, GST_PAD (agg_pad));

  return GST_PAD (agg_pad);
}

static void
gst_base_aggregator_release_pad (GstElement * element, GstPad * pad)
{
  GstBaseAggregatorPrivate *priv = GST_BASE_AGGREGATOR (element)->priv;

  if (!priv->aggregate_task->running)
    gst_pad_set_active (pad, FALSE);
  gst_element_remove_pad (element, pad);
}

static gboolean
_check_all_pads_with_data_or_eos (GstBaseAggregator * self)
{
  GstIterator *iter;
  gboolean res = TRUE;
  gboolean done = FALSE;
  GValue data = { 0, };

  iter = gst_element_iterate_sink_pads (GST_ELEMENT (self));

  while (!done) {
    switch (gst_iterator_next (iter, &data)) {
      case GST_ITERATOR_OK:
      {
        GstBaseAggregatorPad *bpad =
            GST_BASE_AGGREGATOR_PAD (g_value_get_object (&data));

        if (!bpad->buffer)
          res = FALSE;
        g_value_reset (&data);
        break;
      }
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        res = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      case GST_ITERATOR_ERROR:
        g_assert_not_reached ();
        break;
    }
  }

  return res;
}

static gboolean
_reset_all_pads_data (GstBaseAggregator * self)
{
  GstIterator *iter;
  gboolean res = TRUE;
  gboolean done = FALSE;
  GValue data = { 0, };

  iter = gst_element_iterate_sink_pads (GST_ELEMENT (self));

  while (!done) {
    switch (gst_iterator_next (iter, &data)) {
      case GST_ITERATOR_OK:
      {
        GstBaseAggregatorPad *bpad =
            GST_BASE_AGGREGATOR_PAD (g_value_get_object (&data));

        gst_buffer_replace (&bpad->buffer, NULL);
        g_value_reset (&data);
        break;
      }
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        res = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
      case GST_ITERATOR_ERROR:
        g_assert_not_reached ();
        break;
    }
  }

  return res;
}

static void
aggregate_func (GstBaseAggregator * self)
{
  gboolean do_aggregate;
  GstBaseAggregatorClass *klass;

  GstBaseAggregatorPrivate *priv = self->priv;

  if (priv->aggregate_task->running) {
    GST_ERROR ("wait please");
    GST_TASK_WAIT (priv->aggregate_task);
  }

  do_aggregate = _check_all_pads_with_data_or_eos (self);
  GST_ERROR_OBJECT (self, "Checking for aggregation : %d", do_aggregate);
  if (do_aggregate) {
    klass = GST_BASE_AGGREGATOR_GET_CLASS (self);

    if (klass->aggregate) {
      klass->aggregate (self);
    }

    _reset_all_pads_data (self);
    GST_ERROR ("Aggregated");
  }
}

static GstStateChangeReturn
gst_base_aggregator_change_state (GstElement * element,
    GstStateChange transition)
{
  GstStateChangeReturn ret;

  GstBaseAggregatorPrivate *priv = GST_BASE_AGGREGATOR (element)->priv;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_task_set_state (priv->aggregate_task, GST_TASK_STARTED);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      GST_ERROR ("HERE");
      gst_task_set_state (priv->aggregate_task, GST_TASK_STOPPED);
      GST_TASK_BROADCAST (priv->aggregate_task);
      gst_task_join (priv->aggregate_task);
      GST_ERROR ("HERE");
      break;
    default:
      break;
  }

  ret =
      GST_ELEMENT_CLASS (gst_base_aggregator_parent_class)->change_state
      (element, transition);

  switch (transition) {
    default:
      break;
  }

  return ret;
}

static void
gst_base_aggregator_class_init (GstBaseAggregatorClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  g_type_class_add_private (klass, sizeof (GstBaseAggregatorPrivate));

  GST_DEBUG_CATEGORY_INIT (base_aggregator_debug, "baseaggregator", 0,
      "GstBaseAggregator");

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_base_aggregator_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_base_aggregator_release_pad);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_base_aggregator_change_state);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_base_aggregator_finalize);
}

static void
gst_base_aggregator_init (GstBaseAggregator * self)
{
  self->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (self, GST_TYPE_BASE_AGGREGATOR,
      GstBaseAggregatorPrivate);

  self->priv->padcount = -1;
  self->priv->aggregate_task =
      gst_task_new ((GstTaskFunction) aggregate_func, self, NULL);
  GST_ERROR ("Setting lock");
  gst_task_set_lock (self->priv->aggregate_task, &self->priv->aggregate_lock);
}

static GstFlowReturn
_chain (GstPad * pad, GstObject * object, GstBuffer * buffer)
{
  GstBaseAggregatorPrivate *priv = GST_BASE_AGGREGATOR (object)->priv;
  GstBaseAggregatorPad *bpad = GST_BASE_AGGREGATOR_PAD (pad);

  GST_ERROR ("chaining");
  gst_buffer_replace (&bpad->buffer, buffer);

  GST_TASK_SIGNAL (priv->aggregate_task);
  return GST_FLOW_OK;
}

static gboolean
_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  return gst_pad_event_default (pad, parent, event);
}

static gboolean
_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  return gst_pad_query_default (pad, parent, query);
}

/***********************************
 * GstBaseAggregatorPad implementation  *
 ************************************/
G_DEFINE_TYPE (GstBaseAggregatorPad, gst_base_aggregator_pad, GST_TYPE_PAD);

struct _GstBaseAggregatorPadPrivate
{
  gpointer nothing;
};

static void
gst_base_aggregator_pad_finalize (GObject * object)
{
}

static void
_pad_constructed (GObject * object)
{
  GstPad *pad = GST_PAD (object);

  gst_pad_set_chain_function (pad,
      GST_DEBUG_FUNCPTR ((GstPadChainFunction) _chain));
  gst_pad_set_event_function (pad,
      GST_DEBUG_FUNCPTR ((GstPadEventFunction) _event));
  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR ((GstPadQueryFunction) _query));
}

static void
gst_base_aggregator_pad_class_init (GstBaseAggregatorPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  g_type_class_add_private (klass, sizeof (GstBaseAggregatorPadPrivate));

  gobject_class->finalize =
      GST_DEBUG_FUNCPTR (gst_base_aggregator_pad_finalize);
  gobject_class->constructed = GST_DEBUG_FUNCPTR (_pad_constructed);
}

static void
gst_base_aggregator_pad_init (GstBaseAggregatorPad * pad)
{
  pad->buffer = NULL;
}

GstBaseAggregatorPad *
gst_base_aggregator_pad_new (void)
{
  return g_object_new (GST_TYPE_BASE_AGGREGATOR_PAD, NULL);
}
