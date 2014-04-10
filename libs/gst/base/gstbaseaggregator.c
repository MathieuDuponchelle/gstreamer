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

/* GstBaseAggregatorPad definitions */
#define PAD_LOCK_EVENT(pad)   G_STMT_START {                            \
  GST_INFO_OBJECT (pad, "Taking EVENT lock from thread %p",              \
        g_thread_self());                                               \
  g_mutex_lock(&pad->priv->event_lock);                                 \
  } G_STMT_END

#define PAD_UNLOCK_EVENT(pad)  G_STMT_START {                           \
  GST_INFO_OBJECT (pad, "Releasing EVENT lock from thread %p",          \
        g_thread_self());                                               \
  g_mutex_unlock(&pad->priv->event_lock);                               \
  } G_STMT_END


#define PAD_WAIT_EVENT(pad)   G_STMT_START {                            \
  GST_INFO_OBJECT (pad, "Waiting for EVENT on thread %p",               \
        g_thread_self());                                               \
  g_cond_wait(&(((GstBaseAggregatorPad* )pad)->priv->event_cond),       \
      &(pad->priv->event_lock));                                        \
  } G_STMT_END

#define PAD_BROADCAST_EVENT(pad) {                                          \
  GST_INFO_OBJECT (pad, "Signaling EVENT from thread %p",               \
        g_thread_self());                                                   \
  g_cond_broadcast(&(((GstBaseAggregatorPad* )pad)->priv->event_cond)); \
  }

struct _GstBaseAggregatorPadPrivate
{
  gboolean pending_flush_start;
  gboolean pending_flush_stop;

  GMutex event_lock;
  GCond event_cond;
};

/*************************************
 * GstBaseAggregator implementation  *
 *************************************/
static GstElementClass *parent_class = NULL;

#define AGGREGATE_LOCK(self) G_STMT_START {                             \
  GST_INFO_OBJECT (self, "Trying to take AGGREGATE in thread %p",       \
        g_thread_self());                                               \
  g_mutex_lock(&((GstBaseAggregator*)self)->priv->aggregate_lock);      \
  GST_INFO_OBJECT (self, "Got AGGREGATE in thread %p",                  \
        g_thread_self());                                               \
} G_STMT_END

#define AGGREGATE_UNLOCK(self) G_STMT_START {                           \
  g_mutex_unlock(&((GstBaseAggregator*)self)->priv->aggregate_lock);    \
  GST_INFO_OBJECT (self, "Unlocked AGGREGATATE in thread %p",           \
        g_thread_self());                                               \
} G_STMT_END

#define WAIT_FOR_AGGREGATE(agg)   G_STMT_START {                        \
  GST_INFO_OBJECT (agg, "Waiting for aggregate in thread %p",           \
        g_thread_self());                                               \
  g_cond_wait(&(agg->priv->aggregate_cond),                             \
      &(agg->priv->aggregate_lock));                                    \
  GST_INFO_OBJECT (agg, "Done waiting for aggregate in thread %p",      \
        g_thread_self());                                               \
  } G_STMT_END

#define BROADCAST_AGGREGATE(agg) {                                      \
  GST_INFO_OBJECT (agg, "signaling aggregate from thread %p",           \
        g_thread_self());                                               \
  g_cond_broadcast(&(agg->priv->aggregate_cond));                       \
  GST_INFO_OBJECT (agg, "signaled aggregate from thread %p",            \
        g_thread_self());                                               \
  } G_STMT_END

struct _GstBaseAggregatorPrivate
{
  gint padcount;

  /* Our state is >= PAUSED */
  gboolean running;

  GCond aggregate_cond;
  GMutex aggregate_lock;
  guint64 cookie;

  gboolean flush_seeking;
  gboolean pending_flush_start;
  GstFlowReturn flow_return;
  GstEvent *flush_stop_evt;
};

typedef struct
{
  GstEvent *event;
  gboolean result;
  gboolean flush;
} EventData;

typedef gboolean (*PadForeachFunc) (GstPad * pad, gpointer user_data);

static gboolean
_iterate_all_sinkpads (GstBaseAggregator * self, PadForeachFunc func,
    gpointer user_data)
{
  gboolean result = FALSE;
  GstIterator *iter;
  gboolean done = FALSE;
  GValue item = { 0, };
  GList *seen_pads = NULL;

  iter = gst_element_iterate_sink_pads (GST_ELEMENT (self));

  if (!iter)
    goto no_iter;

  while (!done) {
    switch (gst_iterator_next (iter, &item)) {
      case GST_ITERATOR_OK:
      {
        GstPad *pad;

        pad = g_value_get_object (&item);

        /* if already pushed, skip. FIXME, find something faster to tag pads */
        if (pad == NULL || g_list_find (seen_pads, pad)) {
          g_value_reset (&item);
          break;
        }

        GST_LOG_OBJECT (self, "calling function on pad %s:%s",
            GST_DEBUG_PAD_NAME (pad));
        result = func (pad, user_data);

        done = !result;

        seen_pads = g_list_prepend (seen_pads, pad);

        g_value_reset (&item);
        break;
      }
      case GST_ITERATOR_RESYNC:
        /* We don't reset the result here because we don't push the event
         * again on pads that got the event already and because we need
         * to consider the result of the previous pushes */
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        GST_ERROR_OBJECT (self,
            "Could not iterate over internally linked pads");
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  g_value_unset (&item);
  gst_iterator_free (iter);

  if (seen_pads == NULL) {
    GST_INFO_OBJECT (self, "No pad seen");
    return FALSE;
  }

  g_list_free (seen_pads);

no_iter:
  return result;
}

static gboolean
_check_all_pads_with_data_or_eos (GstBaseAggregatorPad * aggpad)
{
  return (aggpad->buffer || aggpad->eos);
}

static void
aggregate_func (GstBaseAggregator * self)
{
  GstBaseAggregatorPrivate *priv = self->priv;
  GstBaseAggregatorClass *klass = GST_BASE_AGGREGATOR_GET_CLASS (self);

  /* We always want to check if aggregate needs to be
   * called when starting */
  guint64 local_cookie = priv->cookie - 1;

  do {
    AGGREGATE_LOCK (self);

    GST_DEBUG ("I'm waiting for aggregation %lu -- %lu", local_cookie,
        priv->cookie);

    if (local_cookie == priv->cookie)
      WAIT_FOR_AGGREGATE (self);

    local_cookie++;

    if (_iterate_all_sinkpads (self,
            (PadForeachFunc) _check_all_pads_with_data_or_eos, NULL)) {
      GST_DEBUG_OBJECT (self, "Aggregating");

      priv->flow_return = klass->aggregate (self);

      if (priv->flow_return == GST_FLOW_FLUSHING &&
          g_atomic_int_get (&priv->flush_seeking))
        priv->flow_return = GST_FLOW_OK;

    } else {
      GST_DEBUG_OBJECT (self, "Not ready to aggregate");
    }

    AGGREGATE_UNLOCK (self);
  } while (priv->running);
}

static void
_start (GstBaseAggregator * self)
{
  self->priv->running = TRUE;
  gst_pad_start_task (GST_PAD (self->srcpad), (GstTaskFunction) aggregate_func,
      self, NULL);
  GST_DEBUG_OBJECT (self, "I've started running");
}

static void
_stop (GstBaseAggregator * self)
{
  GST_ERROR ("STOP");

  AGGREGATE_LOCK (self);
  self->priv->running = FALSE;
  self->priv->cookie++;
  BROADCAST_AGGREGATE (self);
  AGGREGATE_UNLOCK (self);

  gst_pad_stop_task (GST_PAD (self->srcpad));


  GST_DEBUG_OBJECT (self, "I've stopped running");
}

static void
_check_pending_flush_stop (GValue * item, gboolean * needs_forward)
{
  GstBaseAggregatorPad *pad = g_value_get_object (item);

  *needs_forward = !pad->priv->pending_flush_stop
      && !pad->priv->pending_flush_start;
}

/* GstBaseAggregator vmethods default implementations */
static gboolean
_pad_event (GstBaseAggregator * self, GstBaseAggregatorPad * aggpad,
    GstEvent * event)
{
  gboolean res = TRUE;
  GstPad *pad = GST_PAD (aggpad);
  GstBaseAggregatorPrivate *priv = self->priv;
  GstBaseAggregatorPadPrivate *padpriv = aggpad->priv;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_FLUSH_START:
    {
      GstBuffer *tmpbuf;

      g_atomic_int_set (&aggpad->flushing, TRUE);
      aggpad->eos = FALSE;

      /*  Remove pad buffer and wake up the streaming thread */
      tmpbuf = gst_base_aggregator_pad_get_buffer (aggpad);
      gst_buffer_replace (&tmpbuf, NULL);

      if (g_atomic_int_compare_and_exchange (&padpriv->pending_flush_start,
              TRUE, FALSE) == TRUE) {
        GST_DEBUG_OBJECT (aggpad, "Expecting FLUSH_STOP now");
        g_atomic_int_set (&padpriv->pending_flush_stop, TRUE);
      }

      if (g_atomic_int_get (&priv->flush_seeking)) {
        /* If flush_seeking we forward the first FLUSH_START  */
        if (g_atomic_int_compare_and_exchange (&priv->pending_flush_start,
                TRUE, FALSE) == TRUE) {
          goto forward;
        }
      }

      /* We forward only in one case: right after flush_seeking */
      goto eat;
    }
    case GST_EVENT_FLUSH_STOP:
    {
      g_atomic_int_set (&aggpad->flushing, FALSE);
      if (g_atomic_int_get (&priv->flush_seeking)) {
        gboolean needs_forward = FALSE;
        GstIterator *sinkpads;

        g_atomic_int_set (&aggpad->priv->pending_flush_stop, FALSE);
        sinkpads = gst_element_iterate_sink_pads (GST_ELEMENT (self));

        if (g_atomic_int_get (&priv->flush_seeking)) {
          /* In the case of flush_seeking we wait for FLUSH_START/FLUSH_STOP
           * to be received on all pads before fowarding the FLUSH_STOP
           * event downstream */
          while (gst_iterator_foreach (sinkpads,
                  (GstIteratorForeachFunction) _check_pending_flush_stop,
                  &needs_forward) == GST_ITERATOR_RESYNC) {
            gst_iterator_resync (sinkpads);
            needs_forward = FALSE;
          }

          if (needs_forward) {
            /* That means we received FLUSH_STOP/FLUSH_STOP on
             * all sinkpads -- Seeking is Done.*/
            g_atomic_int_set (&priv->flush_seeking, FALSE);
            goto forward;
          }
        }

        goto eat;
      } else {
        goto eat;
      }
      break;
    }
    case GST_EVENT_EOS:
    {
      GST_DEBUG_OBJECT (aggpad, "EOS");

      PAD_LOCK_EVENT (aggpad);
      while (aggpad->buffer) {
        GST_DEBUG ("Waiting for buffer to be consumed");
        PAD_WAIT_EVENT (aggpad);
      }
      PAD_UNLOCK_EVENT (aggpad);

      AGGREGATE_LOCK (self);
      aggpad->eos = TRUE;
      priv->cookie++;;
      BROADCAST_AGGREGATE (self);
      AGGREGATE_UNLOCK (self);
      goto eat;
    }
    case GST_EVENT_SEGMENT:
    {
      GstSegment seg;
      gst_event_copy_segment (event, &seg);
      aggpad->segment = seg;
      goto eat;
    }
    case GST_EVENT_STREAM_START:
      goto eat;
    default:
    {
      break;
    }
  }

forward:
  GST_DEBUG_OBJECT (pad, "Fowarding event: %" GST_PTR_FORMAT, event);
  return gst_pad_event_default (pad, GST_OBJECT (self), event);

eat:
  GST_DEBUG_OBJECT (pad, "Eating event: %" GST_PTR_FORMAT, event);
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
  GstBaseAggregator *self = GST_BASE_AGGREGATOR (element);
  GstBaseAggregatorPrivate *priv = self->priv;

  GST_DEBUG_OBJECT (pad, "Removing pad");

  if (!priv->running)
    gst_pad_set_active (pad, FALSE);

  gst_element_remove_pad (element, pad);

  AGGREGATE_LOCK (self);
  priv->cookie++;
  BROADCAST_AGGREGATE (self);
  AGGREGATE_UNLOCK (self);
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
        g_object_new (GST_BASE_AGGREGATOR_GET_CLASS (agg)->sinkpads_type,
        "name", name, "direction", GST_PAD_SINK, "template", templ, NULL);
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
_src_query (GstBaseAggregator * self, GstQuery * query)
{
  gboolean res = TRUE;

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_SEEKING:
    {
      GstFormat format;

      /* don't pass it along as some (file)sink might claim it does
       * whereas with a collectpads in between that will not likely work */
      gst_query_parse_seeking (query, &format, NULL, NULL, NULL);
      gst_query_set_seeking (query, format, FALSE, 0, -1);
      res = TRUE;

      goto discard;
    }
    default:
      break;
  }

  return gst_pad_query_default (self->srcpad, GST_OBJECT (self), query);

discard:
  return res;
}

static gboolean
event_forward_func (GstPad * pad, EventData * evdata)
{
  gboolean ret = TRUE;
  GstPad *peer = gst_pad_get_peer (pad);
  GstBaseAggregatorPadPrivate *padpriv = GST_BASE_AGGREGATOR_PAD (pad)->priv;

  if (peer) {
    ret = gst_pad_send_event (peer, gst_event_ref (evdata->event));
    gst_object_unref (peer);
  }

  if (evdata->flush) {
    padpriv->pending_flush_start = ret;
    padpriv->pending_flush_stop = FALSE;
  }

  /* Always send to all pads */
  return FALSE;
}

static gboolean
_forward_event_to_all_sinkpads (GstPad * srcpad, GstEvent * event,
    gboolean flush)
{
  EventData evdata;

  evdata.event = event;
  evdata.result = TRUE;
  evdata.flush = flush;

  gst_pad_forward (srcpad, (GstPadForwardFunction) event_forward_func, &evdata);

  gst_event_unref (event);

  return evdata.result;
}

static gboolean
_src_event (GstBaseAggregator * self, GstEvent * event)
{
  gboolean res = TRUE;
  GstBaseAggregatorPrivate *priv = self->priv;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      gboolean flush;
      GstSeekFlags flags;

      GST_INFO_OBJECT (self, "starting SEEK");

      gst_event_parse_seek (event, NULL, NULL, &flags, NULL, NULL, NULL, NULL);

      flush = flags & GST_SEEK_FLAG_FLUSH;
      if (flush) {
        g_atomic_int_set (&priv->pending_flush_start, TRUE);
        g_atomic_int_set (&priv->flush_seeking, TRUE);
      }

      /* forward the seek upstream */
      res = _forward_event_to_all_sinkpads (self->srcpad, event, flush);
      event = NULL;

      if (!res) {
        g_atomic_int_set (&priv->flush_seeking, FALSE);
        g_atomic_int_set (&priv->pending_flush_start, FALSE);
      }

      GST_INFO_OBJECT (self, "seek done, result: %d", res);

      goto done;
    }
    default:
    {
      break;
    }
  }

  return gst_pad_event_default (self->srcpad, GST_OBJECT (self), event);

done:
  return res;
}

static gboolean
src_event_func (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstBaseAggregatorClass *klass = GST_BASE_AGGREGATOR_GET_CLASS (parent);

  return klass->src_event (GST_BASE_AGGREGATOR (parent), event);
}

static gboolean
src_query_func (GstPad * pad, GstObject * parent, GstQuery * query)
{
  GstBaseAggregatorClass *klass = GST_BASE_AGGREGATOR_GET_CLASS (parent);

  return klass->src_query (GST_BASE_AGGREGATOR (parent), query);
}

static gboolean
_pad_query (GstBaseAggregator * self, GstBaseAggregatorPad * aggpad,
    GstQuery * query)
{
  gboolean res = TRUE;
  GstObject *parent;
  GstPad *pad = GST_PAD (aggpad);

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

  klass->sinkpads_type = GST_TYPE_BASE_AGGREGATOR_PAD;

  klass->pad_event = _pad_event;
  klass->pad_query = _pad_query;

  klass->src_event = _src_event;
  klass->src_query = _src_query;

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

  g_return_if_fail (klass->aggregate != NULL);

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

  gst_pad_set_event_function (self->srcpad,
      GST_DEBUG_FUNCPTR ((GstPadEventFunction) src_event_func));
  gst_pad_set_query_function (self->srcpad,
      GST_DEBUG_FUNCPTR ((GstPadQueryFunction) src_query_func));

  /* FIXME Check if we always want proxy caps... and find a nice API
   * if not */
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

static GstFlowReturn
_chain (GstPad * pad, GstObject * object, GstBuffer * buffer)
{
  GstBaseAggregator *self = GST_BASE_AGGREGATOR (object);
  GstBaseAggregatorPrivate *priv = self->priv;
  GstBaseAggregatorPad *aggpad = GST_BASE_AGGREGATOR_PAD (pad);

  GST_DEBUG_OBJECT (aggpad, "Start chaining");
  GST_DEBUG_OBJECT (aggpad, " chaining");

  PAD_LOCK_EVENT (aggpad);
  if (aggpad->buffer) {
    GST_INFO_OBJECT (aggpad, "Waiting for buffer to be consumed");
    PAD_WAIT_EVENT (aggpad);
  }
  PAD_UNLOCK_EVENT (aggpad);

  if (g_atomic_int_get (&aggpad->flushing) == TRUE)
    goto flushing;

  if (g_atomic_int_get (&aggpad->eos) == TRUE)
    goto eos;


  AGGREGATE_LOCK (self);
  priv->cookie++;
  gst_buffer_replace (&aggpad->buffer, buffer);
  GST_DEBUG_OBJECT (aggpad, "ADDED BUFFER");
  BROADCAST_AGGREGATE (self);
  AGGREGATE_UNLOCK (self);

  GST_DEBUG_OBJECT (aggpad, "Done chaining");

  return priv->flow_return;

flushing:

  GST_DEBUG_OBJECT (aggpad, "We are flushing");

  return GST_FLOW_FLUSHING;

eos:

  GST_ERROR_OBJECT (pad, "We are EOS already...");

  return GST_FLOW_EOS;
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

/***********************************
 * GstBaseAggregatorPad implementation  *
 ************************************/
G_DEFINE_TYPE (GstBaseAggregatorPad, gst_base_aggregator_pad, GST_TYPE_PAD);

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
  pad->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (pad, GST_TYPE_BASE_AGGREGATOR_PAD,
      GstBaseAggregatorPadPrivate);

  pad->buffer = NULL;
  g_mutex_init (&pad->priv->event_lock);
  g_cond_init (&pad->priv->event_cond);

}

GstBaseAggregatorPad *
gst_base_aggregator_pad_new (void)
{
  return g_object_new (GST_TYPE_BASE_AGGREGATOR_PAD, NULL);
}

/**
 * gst_base_aggregator_pad_get_buffer:
 * @pad: the pad to get buffer from
 *
 * Pop the buffer currently queued in @pad. This function should exclusively
 * be called from the aggregation thread as this is where buffer can be
 * consumed.
 *
 * Returns: (transfer full): The buffer in @pad or NULL if no buffer was
 *   queued. You should unref the buffer after usage.
 */
GstBuffer *
gst_base_aggregator_pad_get_buffer (GstBaseAggregatorPad * pad)
{
  GstBuffer *buffer = NULL;

  PAD_LOCK_EVENT (pad);
  if (pad->buffer) {
    GST_DEBUG_OBJECT (pad, "Consuming buffer");
    buffer = pad->buffer;
    pad->buffer = NULL;
    PAD_BROADCAST_EVENT (pad);
    GST_DEBUG_OBJECT (pad, "Consummed");
  }
  PAD_UNLOCK_EVENT (pad);

  return buffer;
}
