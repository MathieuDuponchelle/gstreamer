/*
 * baseaggregatoror.c - GstBaseAggregator testsuite
 * Copyright (C) 2006 Alessandro Decina <alessandro.d@gmail.com>
 * Copyright (C) 2014 Mathieu Duponchelle <mathieu.duponchelle@oencreed.com>
 * Copyright (C) 2014 Thibault Saunier <tsaunier@opencreed.com>
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

#include <gst/check/gstcheck.h>
#include <gst/base/gstbaseaggregator.h>

/* dummy baseaggregator based element */

#define GST_TYPE_AGGREGATOR            (gst_aggregator_get_type ())
#define GST_AGGREGATOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_AGGREGATOR, GstAggregator))
#define GST_AGGREGATOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_AGGREGATOR, GstAggregatorClass))
#define GST_AGGREGATOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), GST_TYPE_AGGREGATOR, GstAggregatorClass))

typedef struct _GstAggregator GstAggregator;
typedef struct _GstAggregatorClass GstAggregatorClass;

static GstFlowReturn
gst_agregator_aggregate (GstBaseAggregator * baseaggregator)
{
  GSList *walk;
  GstBuffer *inbuf;
  GstIterator *iter;
  GstBaseAggregatorPad *pad, *wanted_pad;

  guint outsize = 0;
  gboolean done = FALSE;
  GstCollecttorPad *pad = NULL;
  GstAggregator *aggregator = GST_AGGREGATOR (user_data);

  iter = gst_element_iterate_sink_pads (GST_ELEMENT (baseaggregator));
  while (!done) {
    GValue value = { 0, };
    switch (gst_iterator_next (iter, &value)) {
      case GST_ITERATOR_OK:
        pad = g_value_get_object (&value);

        if (pad->buffer)
          wanted_pad = pad;
        g_value_reset (&value);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        /* FIXME What should I do here? */
        break;
      case GST_ITERATOR_ERROR:
        done = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);

  /* can only happen when no baseaggregator to collect or all EOS */
  if (wanted_pad == NULL)
    goto eos;

  outsize = gst_buffer_get_size (wanted_pad->buffer);
  inbuf = gst_collect_pads_take_buffer (baseaggregator, wanted_pad, outsize);
  if (!inbuf)
    goto eos;

  if (aggregator->first) {
    GstSegment segment;

    gst_segment_init (&segment, GST_FORMAT_BYTES);
    gst_pad_push_event (aggregator->srcpad,
        gst_event_new_stream_start ("test"));
    gst_pad_push_event (aggregator->srcpad, gst_event_new_segment (&segment));
    aggregator->first = FALSE;
  }

  /* just forward the first buffer */
  GST_DEBUG_OBJECT (aggregator, "forward buffer %p", inbuf);
  return gst_pad_push (aggregator->srcpad, inbuf);
  /* ERRORS */
eos:
  {
    GST_DEBUG_OBJECT (aggregator, "no data available, must be EOS");
    gst_pad_push_event (aggregator->srcpad, gst_event_new_eos ());
    return GST_FLOW_EOS;
  }
}

struct _GstAggregator
{
  GstBaseAggregator parent;
  GstPad *srcpad;

  GstPad *sinkpad[2];
  gint padcount;
  gboolean first;
};
struct _GstAggregatorClass
{
  GstElementClass parent_class;

};

static GType gst_aggregator_get_type (void);

G_DEFINE_TYPE (GstAggregator, gst_aggregator, GST_TYPE_BASE_AGGREGATOR);

static GstStaticPadTemplate gst_aggregator_src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_aggregator_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink_%u", GST_PAD_SINK, GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

static GstPad *
gst_aggregator_request_new_pad (GstElement * element, GstPadTemplate * templ,
    const gchar * unused, const GstCaps * caps)
{
  GstAggregator *aggregator = GST_AGGREGATOR (element);
  gchar *name;
  GstPad *newpad;
  gint padcount;

  if (templ->direction != GST_PAD_SINK)
    return NULL;

  /* create new pad */
  padcount = g_atomic_int_add (&aggregator->padcount, 1);
  name = g_strdup_printf ("sink_%u", padcount);
  newpad = gst_pad_new_from_template (templ, name);
  g_free (name);

  gst_collect_pads_add_pad (aggregator->collect, newpad,
      sizeof (GstCollectData), NULL, TRUE);

  /* takes ownership of the pad */
  if (!gst_element_add_pad (GST_ELEMENT (aggregator), newpad))
    goto could_not_add;

  GST_DEBUG_OBJECT (aggregator, "added new pad %s", GST_OBJECT_NAME (newpad));
  return newpad;

  /* errors */
could_not_add:
  {
    GST_DEBUG_OBJECT (aggregator, "could not add pad");
    gst_collect_pads_remove_pad (aggregator->collect, newpad);
    gst_object_unref (newpad);
    return NULL;
  }
}

static void
gst_aggregator_release_pad (GstElement * element, GstPad * pad)
{
  GstAggregator *aggregator = GST_AGGREGATOR (element);

  if (aggregator->collect)
    gst_collect_pads_remove_pad (aggregator->collect, pad);
  gst_element_remove_pad (element, pad);
}

static GstStateChangeReturn
gst_aggregator_change_state (GstElement * element, GstStateChange transition)
{
  GstAggregator *aggregator = GST_AGGREGATOR (element);
  GstStateChangeReturn ret;

  switch (transition) {
    case GST_STATE_CHANGE_NULL_TO_READY:
      break;
    case GST_STATE_CHANGE_READY_TO_PAUSED:
      gst_collect_pads_start (aggregator->collect);
      break;
    case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
      break;
    case GST_STATE_CHANGE_PAUSED_TO_READY:
      /* need to unblock the baseaggregator before calling the
       * parent change_state so that streaming can finish */
      gst_collect_pads_stop (aggregator->collect);
      break;
    default:
      break;
  }

  ret =
      GST_ELEMENT_CLASS (gst_aggregator_parent_class)->change_state (element,
      transition);

  switch (transition) {
    default:
      break;
  }

  return ret;
}

static void
gst_aggregator_dispose (GObject * object)
{
  GstAggregator *aggregator = GST_AGGREGATOR (object);

  if (aggregator->collect) {
    gst_object_unref (aggregator->collect);
    aggregator->collect = NULL;
  }

  G_OBJECT_CLASS (gst_aggregator_parent_class)->dispose (object);
}

static void
gst_aggregator_class_init (GstAggregatorClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;
  GstElementClass *gstelement_class = (GstElementClass *) klass;

  gobject_class->dispose = gst_aggregator_dispose;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_aggregator_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_aggregator_sink_template));
  gst_element_class_set_static_metadata (gstelement_class, "Aggregator",
      "Testing", "Combine N buffers", "Stefan Sauer <ensonic@users.sf.net>");

  gstelement_class->request_new_pad =
      GST_DEBUG_FUNCPTR (gst_aggregator_request_new_pad);
  gstelement_class->release_pad =
      GST_DEBUG_FUNCPTR (gst_aggregator_release_pad);
  gstelement_class->change_state =
      GST_DEBUG_FUNCPTR (gst_aggregator_change_state);
}

static void
gst_aggregator_init (GstAggregator * agregator)
{
  GstPadTemplate *template;

  template = gst_static_pad_template_get (&gst_aggregator_src_template);
  agregator->srcpad = gst_pad_new_from_template (template, "src");
  gst_object_unref (template);

  GST_PAD_SET_PROXY_CAPS (agregator->srcpad);
  gst_element_add_pad (GST_ELEMENT (agregator), agregator->srcpad);

  /* keep track of the sinkpads requested */
  agregator->collect = gst_collect_pads_new ();
  gst_collect_pads_set_function (agregator->collect,
      GST_DEBUG_FUNCPTR (gst_agregator_collected), agregator);

  agregator->first = TRUE;
}

static gboolean
gst_agregator_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "aggregator", GST_RANK_NONE,
      GST_TYPE_AGGREGATOR);
}

static gboolean
gst_agregator_plugin_register (void)
{
  return gst_plugin_register_static (GST_VERSION_MAJOR,
      GST_VERSION_MINOR,
      "aggregator",
      "Combine buffers",
      gst_agregator_plugin_init,
      VERSION, GST_LICENSE, PACKAGE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
}
