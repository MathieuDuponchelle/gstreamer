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

/*************************************
 * GstBaseAggregator implementation  *
 *************************************/
static GstElementClass *parent_class = NULL;

#define AGGREGATE_LOCK(self) g_mutex_lock(&((GstBaseAggregator*)self)->priv->aggregate_lock)
#define AGGREGATE_UNLOCK(self) g_mutex_unlock(&((GstBaseAggregator*)self)->priv->aggregate_lock)

struct _GstBaseAggregatorPrivate
{
  gint padcount;

  /* Our state is >= PAUSED */
  gboolean running;

  GThread *aggregate_thread;
  GCond aggregate_cond;
  GMutex aggregate_lock;
  guint64 cookie;

  gboolean seeking;
  gboolean pending_flush_start;
  gboolean pending_flush_stop;

  GstFlowReturn flow_return;
};

typedef struct
{
  GstEvent *event;
  gboolean result;
} EventData;

#define WAIT_FOR_AGGREGATE(agg)   G_STMT_START {                        \
  GST_INFO_OBJECT (agg, "Waiting for aggregate in thread %p",           \
        g_thread_self());                                               \
  g_cond_wait(&(agg->priv->aggregate_cond),                             \
      &(agg->priv->aggregate_lock));                                   \
  } G_STMT_END

#define BROADCAST_AGGREGATE(agg) {                                         \
  GST_INFO_OBJECT (agg, "signaling aggregate from thread %p",           \
        g_thread_self());                                               \
  g_cond_broadcast(&(agg->priv->aggregate_cond));                          \
  }

static gboolean
_check_all_pads_with_data_or_eos (GstBaseAggregator * self)
{
  GstIterator *iter;
  gboolean res = TRUE;
  gboolean done = FALSE;
  GValue data = { 0, };
  gint numpads = 0;

  iter = gst_element_iterate_sink_pads (GST_ELEMENT (self));

  while (!done) {
    switch (gst_iterator_next (iter, &data)) {
      case GST_ITERATOR_OK:
      {
        GstBaseAggregatorPad *aggpad =
            GST_BASE_AGGREGATOR_PAD (g_value_get_object (&data));

        if (!aggpad->buffer && !aggpad->eos) {
          GST_DEBUG_OBJECT (aggpad, "BUffer %p, EOS %d", aggpad->buffer,
              aggpad->eos);
          res = FALSE;
        }
        numpads++;
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

  if (!numpads)
    res = FALSE;

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
        GstBaseAggregatorPad *aggpad =
            GST_BASE_AGGREGATOR_PAD (g_value_get_object (&data));

        gst_buffer_replace (&aggpad->buffer, NULL);
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

static gpointer
aggregate_func (GstBaseAggregator * self)
{
  GstBaseAggregatorPrivate *priv = self->priv;

  /* We always want to check if aggregate needs to be
   * called when starting */
  guint64 local_cookie = priv->cookie - 1;

  do {
    gboolean do_aggregate;
    GstBaseAggregatorClass *klass;

    AGGREGATE_LOCK (self);
    GST_DEBUG ("I'm waiting for aggregation");

    if (local_cookie == priv->cookie)
      WAIT_FOR_AGGREGATE (self);
    local_cookie++;

    GST_DEBUG ("I want the lock in check");
    do_aggregate = _check_all_pads_with_data_or_eos (self);
    GST_DEBUG_OBJECT (self, "Checking for aggregation : %d", do_aggregate);
    if (do_aggregate) {
      klass = GST_BASE_AGGREGATOR_GET_CLASS (self);

      if (klass->aggregate) {
        priv->flow_return = klass->aggregate (self);
      }

      _reset_all_pads_data (self);
    }
    GST_DEBUG ("releasing the lock in check");

    BROADCAST_AGGREGATE (self);
    AGGREGATE_UNLOCK (self);
  } while (priv->running);

  return NULL;
}

static void
_start (GstBaseAggregator * self)
{
  self->priv->running = TRUE;
  self->priv->aggregate_thread =
      g_thread_new ("aggregate_thread", (GThreadFunc) aggregate_func, self);
  GST_DEBUG_OBJECT (self, "I've started running");
}

static void
_stop (GstBaseAggregator * self)
{
  AGGREGATE_LOCK (self);
  self->priv->running = FALSE;
  self->priv->cookie++;
  BROADCAST_AGGREGATE (self);
  AGGREGATE_UNLOCK (self);
  g_thread_join (self->priv->aggregate_thread);
  GST_DEBUG_OBJECT (self, "I've stopped running");
}

/* GstBaseAggregator vmethods default implementations */
static gboolean
_event (GstBaseAggregator * self, GstBaseAggregatorPad * aggpad,
    GstEvent * event)
{
  gboolean res = TRUE;
  GstPad *pad = GST_PAD (aggpad);
  GstBaseAggregatorPrivate *priv = self->priv;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
    {
      g_atomic_int_set (&aggpad->flushing, TRUE);
      if (g_atomic_int_get (&priv->seeking)) {
        if (g_atomic_int_compare_and_exchange (&priv->pending_flush_start,
                TRUE, FALSE) == FALSE) {
          goto eat;
        } else {
          g_atomic_int_set (&priv->pending_flush_stop, TRUE);
          goto forward;
        }
      } else
        goto forward;
    }
    case GST_EVENT_FLUSH_STOP:
    {
      g_atomic_int_set (&aggpad->flushing, FALSE);
      if (g_atomic_int_get (&priv->seeking)) {
        if (g_atomic_int_compare_and_exchange (&priv->pending_flush_stop,
                TRUE, FALSE))
          goto forward;
        else
          goto eat;
      } else {
        goto forward;
      }
      break;
    }
    case GST_EVENT_EOS:
    {
      GST_DEBUG ("EOS");

      AGGREGATE_LOCK (self);
      while (aggpad->buffer) {
        GST_DEBUG ("Waiting for buffer to be consumed");
        WAIT_FOR_AGGREGATE (self);
      }

      aggpad->eos = TRUE;
      priv->cookie++;;
      BROADCAST_AGGREGATE (self);
      AGGREGATE_UNLOCK (self);
      goto eat;
    }
    default:
    {
      break;
    }
  }

forward:
  return gst_pad_event_default (pad, GST_OBJECT (self), event);

eat:
  GST_DEBUG_OBJECT (self, "Eating event: %" GST_PTR_FORMAT, event);
  if (event)
    gst_event_unref (event);

  return res;
}

/* GstElement vmethods implementations */
static GstStateChangeReturn
_change_state (GstElement * element, GstStateChange transition)
{
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      _start (GST_BASE_AGGREGATOR (element));
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      _stop (GST_BASE_AGGREGATOR (element));
      break;
    default:
      break;
  }

  ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

  switch (transition) {
    default:
      break;
  }

  return ret;
}

static void
_release_pad (GstElement * element, GstPad * pad)
{
  GstBaseAggregatorPrivate *priv = GST_BASE_AGGREGATOR (element)->priv;

  if (!priv->running)
    gst_pad_set_active (pad, FALSE);
  gst_element_remove_pad (element, pad);
}

static GstPad *
_request_new_pad (GstElement * element,
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

  GST_DEBUG_OBJECT (element, "Adding pad %s", GST_PAD_NAME (agg_pad));

  if (priv->running)
    gst_pad_set_active (GST_PAD (agg_pad), TRUE);

  /* add the pad to the element */
  gst_element_add_pad (element, GST_PAD (agg_pad));

  return GST_PAD (agg_pad);
}

static gboolean
_query (GstBaseAggregator * self, GstBaseAggregatorPad * bpad, GstQuery * query)
{
  gboolean res = TRUE;
  GstObject *parent;
  GstPad *pad = GST_PAD (bpad);

  parent = GST_OBJECT_PARENT (pad);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_SEEKING:
    {
      GstFormat format;

      /* don't pass it along as some (file)sink might claim it does
       * whereas with a collectpads in between that will not likely work */
      gst_query_parse_seeking (query, &format, NULL, NULL, NULL);
      gst_query_set_seeking (query, format, FALSE, 0, -1);
      res = TRUE;
      query = NULL;
      break;
    }
    default:
      break;
  }

  if (query)
    return gst_pad_query_default (pad, parent, query);
  else
    return res;
}

/* GObject vmethods implementations */
static void
gst_base_aggregator_finalize (GObject * object)
{
}


static void
gst_base_aggregator_class_init (GstBaseAggregatorClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  parent_class = g_type_class_peek_parent (klass);
  g_type_class_add_private (klass, sizeof (GstBaseAggregatorPrivate));

  GST_DEBUG_CATEGORY_INIT (base_aggregator_debug, "baseaggregator", 0,
      "GstBaseAggregator");

  klass->pad_event = _event;
  klass->pad_query = _query;

  gstelement_class->request_new_pad = GST_DEBUG_FUNCPTR (_request_new_pad);
  gstelement_class->release_pad = GST_DEBUG_FUNCPTR (_release_pad);
  gstelement_class->change_state = GST_DEBUG_FUNCPTR (_change_state);

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_base_aggregator_finalize);
}

static void
gst_base_aggregator_init (GstBaseAggregator * self,
    GstBaseAggregatorClass * klass)
{
  GstPadTemplate *pad_template;

  self->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (self, GST_TYPE_BASE_AGGREGATOR,
      GstBaseAggregatorPrivate);

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "src");
  g_return_if_fail (pad_template != NULL);

  self->priv->padcount = -1;
  self->priv->cookie = 0;
  g_mutex_init (&self->priv->aggregate_lock);

  self->srcpad = gst_pad_new_from_template (pad_template, "src");
  GST_PAD_SET_PROXY_CAPS (self->srcpad);
  gst_element_add_pad (GST_ELEMENT (self), self->srcpad);
}

/* we can't use G_DEFINE_ABSTRACT_TYPE because we need the klass in the _init
 * method to get to the padtemplates */
GType
gst_base_aggregator_get_type (void)
{
  static volatile gsize type = 0;

  if (g_once_init_enter (&type)) {
    GType _type;
    static const GTypeInfo info = {
      sizeof (GstBaseAggregatorClass),
      NULL,
      NULL,
      (GClassInitFunc) gst_base_aggregator_class_init,
      NULL,
      NULL,
      sizeof (GstBaseAggregator),
      0,
      (GInstanceInitFunc) gst_base_aggregator_init,
    };

    _type = g_type_register_static (GST_TYPE_ELEMENT,
        "GstBaseAggregator", &info, G_TYPE_FLAG_ABSTRACT);
    g_once_init_leave (&type, _type);
  }
  return type;
}

static gboolean
event_forward_func (GstPad * pad, EventData * data)
{
  gboolean ret = TRUE;
  GstPad *peer = gst_pad_get_peer (pad);

  if (peer) {
    ret = gst_pad_send_event (peer, gst_event_ref (data->event));
    gst_object_unref (peer);
  }

  data->result &= ret;
  /* Always send to all pads */
  return FALSE;
}

static gboolean
_forward_event_to_all_sinkpads (GstPad * srcpad, GstEvent * event)
{
  EventData data;

  data.event = event;
  data.result = TRUE;

  gst_pad_forward (srcpad, (GstPadForwardFunction) event_forward_func, &data);

  gst_event_unref (event);

  return data.result;
}

static GstFlowReturn
_chain (GstPad * pad, GstObject * object, GstBuffer * buffer)
{
  GstBaseAggregator *self = GST_BASE_AGGREGATOR (object);
  GstBaseAggregatorPrivate *priv = self->priv;
  GstBaseAggregatorPad *aggpad = GST_BASE_AGGREGATOR_PAD (pad);

  GST_DEBUG ("Start chaining");
  AGGREGATE_LOCK (self);
  GST_DEBUG (" chaining");

  if (g_atomic_int_get (&aggpad->flushing) == TRUE)
    goto flushing;

  while (aggpad->buffer) {
    WAIT_FOR_AGGREGATE (self);
    GST_DEBUG ("Waiting for buffer to be consumed");
  }

  priv->cookie++;
  gst_buffer_replace (&aggpad->buffer, buffer);
  GST_DEBUG ("ADDED BUFFER");
  BROADCAST_AGGREGATE (self);
  AGGREGATE_UNLOCK (self);
  GST_DEBUG ("Done chaining");

  return priv->flow_return;

flushing:

  GST_DEBUG ("We are flushing");
  AGGREGATE_UNLOCK (self);

  return GST_FLOW_FLUSHING;
}

static gboolean
pad_query_func (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstBaseAggregatorClass *klass = GST_BASE_AGGREGATOR_GET_CLASS (parent);

  return klass->pad_query (GST_BASE_AGGREGATOR (parent),
      GST_BASE_AGGREGATOR_PAD (pad), query);
}

static gboolean
pad_event_func (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstBaseAggregatorClass *klass = GST_BASE_AGGREGATOR_GET_CLASS (parent);

  return klass->pad_event (GST_BASE_AGGREGATOR (parent),
      GST_BASE_AGGREGATOR_PAD (pad), event);
}

gboolean
gst_base_aggregator_src_event_default (GstElement * element,
    GstPad * pad, GstEvent * event)
{
  gboolean res = TRUE;
  GstBaseAggregatorPrivate *priv = GST_BASE_AGGREGATOR (element)->priv;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      GstSeekFlags flags;

      GST_INFO_OBJECT (element, "starting seek");

      gst_event_parse_seek (event, NULL, NULL, &flags, NULL, NULL, NULL, NULL);

      g_atomic_int_set (&priv->seeking, TRUE);
      if (flags & GST_SEEK_FLAG_FLUSH)
        g_atomic_int_set (&priv->pending_flush_start, TRUE);

      /* forward the seek upstream */
      AGGREGATE_LOCK (element);
      res = _forward_event_to_all_sinkpads (pad, event);
      AGGREGATE_UNLOCK (element);
      event = NULL;

      if (!res) {
        g_atomic_int_set (&priv->seeking, FALSE);
        g_atomic_int_set (&priv->pending_flush_start, FALSE);
      }

      GST_INFO_OBJECT (element, "seek done, result: %d", res);

      goto done;
    }
    default:
    {
      break;
    }
  }

  return gst_pad_event_default (pad, GST_OBJECT (element), event);

done:
  return res;
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
      GST_DEBUG_FUNCPTR ((GstPadEventFunction) pad_event_func));
  gst_pad_set_query_function (pad,
      GST_DEBUG_FUNCPTR ((GstPadQueryFunction) pad_query_func));
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
