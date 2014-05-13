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

#include <string.h>

GST_DEBUG_CATEGORY_STATIC (base_aggregator_debug);
#define GST_CAT_DEFAULT base_aggregator_debug

/* GstBaseAggregatorPad definitions */
#define PAD_LOCK_EVENT(pad)   G_STMT_START {                            \
  GST_LOG_OBJECT (pad, "Taking EVENT lock from thread %p",              \
        g_thread_self());                                               \
  g_mutex_lock(&pad->priv->event_lock);                                 \
  GST_LOG_OBJECT (pad, "Took EVENT lock from thread %p",              \
        g_thread_self());                                               \
  } G_STMT_END

#define PAD_UNLOCK_EVENT(pad)  G_STMT_START {                           \
  GST_LOG_OBJECT (pad, "Releasing EVENT lock from thread %p",          \
        g_thread_self());                                               \
  g_mutex_unlock(&pad->priv->event_lock);                               \
  GST_LOG_OBJECT (pad, "Release EVENT lock from thread %p",          \
        g_thread_self());                                               \
  } G_STMT_END


#define PAD_WAIT_EVENT(pad)   G_STMT_START {                            \
  GST_LOG_OBJECT (pad, "Waiting for EVENT on thread %p",               \
        g_thread_self());                                               \
  g_cond_wait(&(((GstBaseAggregatorPad* )pad)->priv->event_cond),       \
      &(pad->priv->event_lock));                                        \
  GST_LOG_OBJECT (pad, "DONE Waiting for EVENT on thread %p",               \
        g_thread_self());                                               \
  } G_STMT_END

#define PAD_BROADCAST_EVENT(pad) {                                          \
  GST_LOG_OBJECT (pad, "Signaling EVENT from thread %p",               \
        g_thread_self());                                                   \
  g_cond_broadcast(&(((GstBaseAggregatorPad* )pad)->priv->event_cond)); \
  }

struct _GstBaseAggregatorPadPrivate
{
  gboolean pending_flush_start;
  gboolean pending_flush_stop;
  gboolean pending_eos;

  GMutex event_lock;
  GCond event_cond;
};

static gboolean
_aggpad_flush (GstBaseAggregatorPad * aggpad, GstBaseAggregator * agg)
{
  GstBaseAggregatorPadClass *klass = GST_BASE_AGGREGATOR_PAD_GET_CLASS (agg);

  aggpad->eos = FALSE;
  aggpad->flushing = FALSE;

  if (klass->flush)
    return klass->flush (aggpad, agg);

  return TRUE;
}

/*************************************
 * GstBaseAggregator implementation  *
 *************************************/
static GstElementClass *parent_class = NULL;

#define MAIN_CONTEXT_LOCK(self) G_STMT_START {                       \
  GST_LOG_OBJECT (self, "Getting MAIN_CONTEXT_LOCK in thread %p",    \
        g_thread_self());                                            \
  g_mutex_lock(&((GstBaseAggregator*)self)->priv->mcontext_lock);    \
  GST_LOG_OBJECT (self, "Got MAIN_CONTEXT_LOCK in thread %p",        \
        g_thread_self());                                            \
} G_STMT_END

#define MAIN_CONTEXT_UNLOCK(self) G_STMT_START {                     \
  g_mutex_unlock(&((GstBaseAggregator*)self)->priv->mcontext_lock);  \
  GST_LOG_OBJECT (self, "Unlocked MAIN_CONTEXT_LOCK in thread %p",   \
        g_thread_self());                                            \
} G_STMT_END

struct _GstBaseAggregatorPrivate
{
  gint padcount;

  GMainContext *mcontext;

  /* Our state is >= PAUSED */
  gboolean running;

  /* Unsure that when we remove all sources from the maincontext
   * we can not add any source, avoiding:
   * "g_source_attach: assertion '!SOURCE_DESTROYED (source)' failed" */
  GMutex mcontext_lock;

  gboolean send_stream_start;
  gboolean send_segment;
  gboolean flush_seeking;
  gboolean pending_flush_start;
  GstFlowReturn flow_return;

  GstCaps *srccaps;
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
    GST_DEBUG_OBJECT (self, "No pad seen");
    return FALSE;
  }

  g_list_free (seen_pads);

no_iter:
  return result;
}

static inline gboolean
_check_all_pads_with_data_or_eos (GstBaseAggregatorPad * aggpad)
{
  if (aggpad->buffer || aggpad->eos) {
    return TRUE;
  }

  GST_LOG_OBJECT (aggpad, "Not ready to be aggregated");

  return FALSE;
}

void
gst_base_aggregator_set_src_caps (GstBaseAggregator * self, GstCaps * caps)
{
  self->priv->srccaps = caps;
}

static void
_reset_flow_values (GstBaseAggregator * self)
{
  self->priv->send_stream_start = TRUE;
  self->priv->send_segment = TRUE;
  gst_segment_init (&self->segment, GST_FORMAT_TIME);
}

GstFlowReturn
gst_base_aggregator_finish_buffer (GstBaseAggregator * self, GstBuffer * buf)
{
  if (g_atomic_int_get (&self->priv->send_stream_start)) {
    gchar s_id[32];

    GST_INFO_OBJECT (self, "pushing stream start");
    /* stream-start (FIXME: create id based on input ids) */
    g_snprintf (s_id, sizeof (s_id), "agg-%08x", g_random_int ());
    if (!gst_pad_push_event (self->srcpad, gst_event_new_stream_start (s_id))) {
      GST_WARNING_OBJECT (self->srcpad, "Sending stream start event failed");
    }
    g_atomic_int_set (&self->priv->send_stream_start, FALSE);
  }

  if (self->priv->srccaps) {
    if (!gst_pad_push_event (self->srcpad,
            gst_event_new_caps (self->priv->srccaps))) {
      GST_WARNING_OBJECT (self->srcpad, "Sending caps event failed");
    }
    self->priv->srccaps = NULL;
  }

  if (g_atomic_int_get (&self->priv->send_segment)) {
    if (!g_atomic_int_get (&self->priv->flush_seeking)) {
      GST_INFO_OBJECT (self, "pushing segment");
      gst_pad_push_event (self->srcpad, gst_event_new_segment (&self->segment));
      g_atomic_int_set (&self->priv->send_segment, FALSE);
    }
  }
  if (!g_atomic_int_get (&self->priv->flush_seeking) &&
      gst_pad_is_active (self->srcpad)) {
    GST_LOG_OBJECT (self, "pushing buffer");
    return gst_pad_push (self->srcpad, buf);
  } else {

    return GST_FLOW_OK;
  }
}


static gboolean
aggregate_func (GstBaseAggregator * self)
{
  GstBaseAggregatorPrivate *priv = self->priv;
  GstBaseAggregatorClass *klass = GST_BASE_AGGREGATOR_GET_CLASS (self);

  GST_LOG_OBJECT (self, "Checking aggregate");
  while (_iterate_all_sinkpads (self,
          (PadForeachFunc) _check_all_pads_with_data_or_eos, NULL)) {
    GST_DEBUG_OBJECT (self, "Actually aggregating!");

    priv->flow_return = klass->aggregate (self);

    if (priv->flow_return == GST_FLOW_FLUSHING &&
        g_atomic_int_get (&priv->flush_seeking))
      priv->flow_return = GST_FLOW_OK;

    GST_LOG_OBJECT (self, "flow return is %s",
        gst_flow_get_name (priv->flow_return));

    if (priv->flow_return == GST_FLOW_EOS)
      break;
  }

  return G_SOURCE_REMOVE;
}


static void
_remove_all_sources (GstBaseAggregator * self)
{
  GSource *source;

  MAIN_CONTEXT_LOCK (self);
  while ((source =
          g_main_context_find_source_by_user_data (self->priv->mcontext,
              self))) {
    g_source_destroy (source);
  }
  MAIN_CONTEXT_UNLOCK (self);
}

static void
iterate_main_context_func (GstBaseAggregator * self)
{
  if (self->priv->running == FALSE) {
    GST_DEBUG_OBJECT (self, "Not running anymore");

    return;
  }

  g_main_context_iteration (self->priv->mcontext, TRUE);
}

static void
_start (GstBaseAggregator * self)
{
  self->priv->running = TRUE;
  self->priv->send_stream_start = TRUE;
  self->priv->send_segment = TRUE;
  self->priv->srccaps = NULL;
}

static gboolean
_check_pending_flush_stop (GstBaseAggregatorPad * pad)
{
  return (!pad->priv->pending_flush_stop && !pad->priv->pending_flush_start);
}

static gboolean
_stop_srcpad_task (GstBaseAggregator * self, GstEvent * flush_start)
{
  gboolean res = TRUE;

  GST_INFO_OBJECT (self, "%s srcpad task",
      flush_start ? "Pausing" : "Stopping");

  self->priv->running = FALSE;

  /*  Clean the stack of GSource set on the MainContext */
  g_main_context_wakeup (self->priv->mcontext);
  _remove_all_sources (self);
  if (flush_start) {
    res = gst_pad_push_event (self->srcpad, flush_start);
    gst_pad_pause_task (self->srcpad);
  } else {
    gst_pad_stop_task (self->srcpad);
  }

  return res;
}

static void
_start_srcpad_task (GstBaseAggregator * self)
{
  GST_INFO_OBJECT (self, "Starting srcpad task");

  self->priv->running = TRUE;
  gst_pad_start_task (GST_PAD (self->srcpad),
      (GstTaskFunction) iterate_main_context_func, self, NULL);
}

static inline void
_add_aggregate_gsource (GstBaseAggregator * self)
{
  MAIN_CONTEXT_LOCK (self);
  g_main_context_invoke (self->priv->mcontext, (GSourceFunc) aggregate_func,
      self);
  MAIN_CONTEXT_UNLOCK (self);
}

static GstFlowReturn
_flush (GstBaseAggregator * self)
{
  GstFlowReturn ret = GST_FLOW_OK;
  GstBaseAggregatorPrivate *priv = self->priv;
  GstBaseAggregatorClass *klass = GST_BASE_AGGREGATOR_GET_CLASS (self);

  GST_DEBUG_OBJECT (self, "Flushing everything");
  g_atomic_int_set (&priv->send_segment, TRUE);
  g_atomic_int_set (&priv->flush_seeking, FALSE);
  if (klass->flush)
    ret = klass->flush (self);

  return ret;
}

static gboolean
_all_flush_stop_received (GstBaseAggregator * self)
{
  GList *tmp;
  GstBaseAggregatorPad *tmppad;

  GST_OBJECT_LOCK (self);
  for (tmp = GST_ELEMENT (self)->sinkpads; tmp; tmp = tmp->next) {
    tmppad = (GstBaseAggregatorPad *) tmp->data;

    if (_check_pending_flush_stop (tmppad) == FALSE) {
      GST_DEBUG_OBJECT (tmppad, "Is not last %i -- %i",
          tmppad->priv->pending_flush_start, tmppad->priv->pending_flush_stop);
      GST_OBJECT_UNLOCK (self);
      return FALSE;
    }
  }
  GST_OBJECT_UNLOCK (self);

  return TRUE;
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
      /*  Remove pad buffer and wake up the streaming thread */
      tmpbuf = gst_base_aggregator_pad_get_buffer (aggpad);
      gst_buffer_replace (&tmpbuf, NULL);
      if (g_atomic_int_compare_and_exchange (&padpriv->pending_flush_start,
              TRUE, FALSE) == TRUE) {
        GST_DEBUG_OBJECT (aggpad, "Expecting FLUSH_STOP now");
        g_atomic_int_set (&padpriv->pending_flush_stop, TRUE);
      }

      if (g_atomic_int_get (&priv->flush_seeking)) {
        /* If flush_seeking we forward the first FLUSH_START */
        if (g_atomic_int_compare_and_exchange (&priv->pending_flush_start,
                TRUE, FALSE) == TRUE) {

          GST_DEBUG_OBJECT (self, "Flushing, pausing srcpad task");
          _stop_srcpad_task (self, event);

          GST_INFO_OBJECT (self, "Getting STREAM_LOCK while seeking");
          GST_PAD_STREAM_LOCK (self->srcpad);
          GST_LOG_OBJECT (self, "GOT STREAM_LOCK");
          event = NULL;
          goto eat;
        }
      }

      /* We forward only in one case: right after flush_seeking */
      goto eat;
    }
    case GST_EVENT_FLUSH_STOP:
    {
      GST_DEBUG_OBJECT (aggpad, "Got FLUSH_STOP");

      _aggpad_flush (aggpad, self);
      if (g_atomic_int_get (&priv->flush_seeking)) {
        g_atomic_int_set (&aggpad->priv->pending_flush_stop, FALSE);

        if (g_atomic_int_get (&priv->flush_seeking)) {
          if (_all_flush_stop_received (self)) {
            /* That means we received FLUSH_STOP/FLUSH_STOP on
             * all sinkpads -- Seeking is Done... sending FLUSH_STOP */
            _flush (self);
            gst_pad_push_event (self->srcpad, event);
            event = NULL;
            _add_aggregate_gsource (self);

            GST_INFO_OBJECT (self, "Releasing source pad STREAM_LOCK");
            GST_PAD_STREAM_UNLOCK (self->srcpad);
            _start_srcpad_task (self);
          }
        }
      }

      /* We never forward the event */
      goto eat;
    }
    case GST_EVENT_EOS:
    {
      GST_DEBUG_OBJECT (aggpad, "EOS");

      /* We still have a buffer, and we don't want the subclass to have to
       * check for it. Mark pending_eos, eos will be set when get_buffer is
       * called
       */
      PAD_LOCK_EVENT (aggpad);
      if (!aggpad->buffer) {
        aggpad->eos = TRUE;
      } else {
        aggpad->priv->pending_eos = TRUE;
      }
      PAD_UNLOCK_EVENT (aggpad);

      _add_aggregate_gsource (self);
      goto eat;
    }
    case GST_EVENT_SEGMENT:
    {
      PAD_LOCK_EVENT (aggpad);
      gst_event_copy_segment (event, &aggpad->segment);
      PAD_UNLOCK_EVENT (aggpad);

      goto eat;
    }
    case GST_EVENT_STREAM_START:
      goto eat;
    default:
    {
      break;
    }
  }

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
  GstBaseAggregator *self = GST_BASE_AGGREGATOR (element);

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      _start (self);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      break;
    default:
      break;
  }

  if ((ret =
          GST_ELEMENT_CLASS (parent_class)->change_state (element,
              transition)) == GST_STATE_CHANGE_FAILURE)
    goto failure;


  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      _reset_flow_values (self);
      break;
    default:
      break;
  }

  return ret;

failure:
  {
    GST_ERROR_OBJECT (element, "parent failed state change");
    return ret;
  }
}

static void
_release_pad (GstElement * element, GstPad * pad)
{
  GstBuffer *tmpbuf;

  GstBaseAggregator *self = GST_BASE_AGGREGATOR (element);
  GstBaseAggregatorPad *aggpad = GST_BASE_AGGREGATOR_PAD (pad);

  GST_INFO_OBJECT (pad, "Removing pad");

  g_atomic_int_set (&aggpad->flushing, TRUE);
  tmpbuf = gst_base_aggregator_pad_get_buffer (aggpad);
  gst_buffer_replace (&tmpbuf, NULL);
  gst_element_remove_pad (element, pad);

  /* Something changed make sure we try to aggregate */
  _add_aggregate_gsource (self);
}

static GstPad *
_request_new_pad (GstElement * element,
    GstPadTemplate * templ, const gchar * req_name, const GstCaps * caps)
{
  GstBaseAggregator *self;
  GstBaseAggregatorPad *agg_pad;

  GstElementClass *klass = GST_ELEMENT_GET_CLASS (element);
  GstBaseAggregatorPrivate *priv = GST_BASE_AGGREGATOR (element)->priv;

  self = GST_BASE_AGGREGATOR (element);

  if (templ == gst_element_class_get_pad_template (klass, "sink_%u")) {
    guint serial = 0;
    gchar *name = NULL;

    GST_OBJECT_LOCK (element);
    if (req_name == NULL || strlen (req_name) < 6
        || !g_str_has_prefix (req_name, "sink_")) {
      /* no name given when requesting the pad, use next available int */
      serial = priv->padcount++;
    } else {
      /* parse serial number from requested padname */
      serial = g_ascii_strtoull (&req_name[5], NULL, 10);
      if (serial >= priv->padcount)
        priv->padcount = serial + 1;
    }

    name = g_strdup_printf ("sink_%u", priv->padcount++);
    agg_pad = g_object_new (GST_BASE_AGGREGATOR_GET_CLASS (self)->sinkpads_type,
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
    GST_DEBUG_OBJECT (pad, "return of seeking is %d", ret);
    gst_object_unref (peer);
  }

  evdata->result &= ret;

  if (ret == FALSE) {
    GST_ERROR_OBJECT (pad, "Seek %" GST_PTR_FORMAT " failed", evdata->event);
    if (evdata->flush) {
      padpriv->pending_flush_start = FALSE;
      padpriv->pending_flush_stop = FALSE;
    }
  }

  /* Always send to all pads */
  return FALSE;
}

static gboolean
_set_flush_pending (GstBaseAggregatorPad * pad, gpointer udata)
{
  pad->priv->pending_flush_start = TRUE;
  pad->priv->pending_flush_stop = FALSE;

  return TRUE;
}

static gboolean
_forward_event_to_all_sinkpads (GstBaseAggregator * self, GstEvent * event,
    gboolean flush)
{
  EventData evdata;

  evdata.event = event;
  evdata.result = TRUE;
  evdata.flush = flush;

  /* We first need to set all pads as flushing in a first pass
   * as flush_start flush_stop is sometimes sent synchronously
   * while we send the seek event */
  _iterate_all_sinkpads (self, (PadForeachFunc) _set_flush_pending, NULL);
  gst_pad_forward (self->srcpad, (GstPadForwardFunction) event_forward_func,
      &evdata);

  gst_event_unref (event);

  return evdata.result;
}

static gboolean
_do_seek (GstBaseAggregator * self, GstEvent * event)
{
  gdouble rate;
  GstFormat fmt;
  GstSeekFlags flags;
  GstSeekType start_type, stop_type;
  gint64 start, stop;
  gboolean flush;
  gboolean res;
  GstBaseAggregatorPrivate *priv = self->priv;

  gst_event_parse_seek (event, &rate, &fmt, &flags, &start_type,
      &start, &stop_type, &stop);

  GST_INFO_OBJECT (self, "starting SEEK");

  flush = flags & GST_SEEK_FLAG_FLUSH;

  if (flush) {
    g_atomic_int_set (&priv->pending_flush_start, TRUE);
    g_atomic_int_set (&priv->flush_seeking, TRUE);
  }

  gst_segment_do_seek (&self->segment, rate, fmt, flags, start_type, start,
      stop_type, stop, NULL);

  /* forward the seek upstream */
  res = _forward_event_to_all_sinkpads (self, event, flush);
  event = NULL;

  if (!res) {
    g_atomic_int_set (&priv->flush_seeking, FALSE);
    g_atomic_int_set (&priv->pending_flush_start, FALSE);
  }

  GST_INFO_OBJECT (self, "seek done, result: %d", res);

  return res;
}

static gboolean
_src_event (GstBaseAggregator * self, GstEvent * event)
{
  gboolean res = TRUE;

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_SEEK:
    {
      res = _do_seek (self, event);
      event = NULL;
      goto done;
    }
    case GST_EVENT_NAVIGATION:
    {
      /* navigation is rather pointless. */
      res = FALSE;
      gst_event_unref (event);
      goto done;
    }
    default:
    {
      break;
    }
  }

  return _forward_event_to_all_sinkpads (self, event, FALSE);

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
src_activate_mode (GstPad * pad,
    GstObject * parent, GstPadMode mode, gboolean active)
{
  GstBaseAggregator *self = GST_BASE_AGGREGATOR (parent);

  if (active == TRUE) {
    switch (mode) {
      case GST_PAD_MODE_PUSH:
      {
        GST_INFO_OBJECT (pad, "Activating pad!");
        _start_srcpad_task (self);
        return TRUE;
      }
      default:
      {
        GST_ERROR_OBJECT (pad, "Only supported mode is PUSH");
        return FALSE;
      }
    }
  }

  /* desactivating */
  GST_INFO_OBJECT (self, "Desactivating srcpad");
  _stop_srcpad_task (self, FALSE);

  return TRUE;
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

  GST_DEBUG_CATEGORY_INIT (base_aggregator_debug, "baseaggregator",
      GST_DEBUG_FG_MAGENTA, "GstBaseAggregator");

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
  GstBaseAggregatorPrivate *priv;

  g_return_if_fail (klass->aggregate != NULL);

  self->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (self, GST_TYPE_BASE_AGGREGATOR,
      GstBaseAggregatorPrivate);

  priv = self->priv;

  pad_template =
      gst_element_class_get_pad_template (GST_ELEMENT_CLASS (klass), "src");
  g_return_if_fail (pad_template != NULL);

  priv->padcount = -1;
  _reset_flow_values (self);

  priv->mcontext = g_main_context_new ();

  self->srcpad = gst_pad_new_from_template (pad_template, "src");

  gst_pad_set_event_function (self->srcpad,
      GST_DEBUG_FUNCPTR ((GstPadEventFunction) src_event_func));
  gst_pad_set_query_function (self->srcpad,
      GST_DEBUG_FUNCPTR ((GstPadQueryFunction) src_query_func));
  gst_pad_set_activatemode_function (self->srcpad,
      GST_DEBUG_FUNCPTR ((GstPadActivateModeFunction) src_activate_mode));

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

  GST_DEBUG_OBJECT (aggpad, "Start chaining a buffer of size %d",
      (gint) gst_buffer_get_size (buffer));

  if (g_atomic_int_get (&aggpad->flushing) == TRUE)
    goto flushing;

  if (g_atomic_int_get (&aggpad->priv->pending_eos) == TRUE)
    goto eos;

  PAD_LOCK_EVENT (aggpad);
  if (aggpad->buffer) {
    GST_DEBUG_OBJECT (aggpad, "Waiting for buffer to be consumed");
    PAD_WAIT_EVENT (aggpad);
  }
  PAD_UNLOCK_EVENT (aggpad);

  if (g_atomic_int_get (&aggpad->flushing) == TRUE)
    goto flushing;

  PAD_LOCK_EVENT (aggpad);
  if (aggpad->buffer)
    gst_buffer_unref (aggpad->buffer);
  aggpad->buffer = buffer;
  PAD_UNLOCK_EVENT (aggpad);

  _add_aggregate_gsource (self);

  GST_DEBUG_OBJECT (aggpad, "Done chaining");

  return priv->flow_return;

flushing:

  GST_DEBUG_OBJECT (aggpad, "We are flushing");

  return GST_FLOW_FLUSHING;

eos:

  GST_DEBUG_OBJECT (pad, "We are EOS already...");

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

static gboolean
pad_activate_mode_func (GstPad * pad,
    GstObject * parent, GstPadMode mode, gboolean active)
{
  GstBaseAggregatorPad *aggpad = GST_BASE_AGGREGATOR_PAD (pad);

  if (active == FALSE) {
    PAD_LOCK_EVENT (aggpad);
    g_atomic_int_set (&aggpad->flushing, TRUE);
    gst_buffer_replace (&aggpad->buffer, NULL);
    PAD_BROADCAST_EVENT (aggpad);
    PAD_UNLOCK_EVENT (aggpad);
  } else {
    g_atomic_int_set (&aggpad->flushing, FALSE);
    PAD_LOCK_EVENT (aggpad);
    PAD_BROADCAST_EVENT (aggpad);
    PAD_UNLOCK_EVENT (aggpad);
  }

  return TRUE;
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
  gst_pad_set_activatemode_function (pad,
      GST_DEBUG_FUNCPTR ((GstPadActivateModeFunction) pad_activate_mode_func));
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
    if (pad->priv->pending_eos) {
      pad->priv->pending_eos = FALSE;
      pad->eos = TRUE;
    }
    PAD_BROADCAST_EVENT (pad);
    GST_DEBUG_OBJECT (pad, "Consummed");
  }
  PAD_UNLOCK_EVENT (pad);

  return buffer;
}
