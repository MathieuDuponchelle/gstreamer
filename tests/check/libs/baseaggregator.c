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

static GType gst_aggregator_get_type (void);

struct _GstAggregator
{
  GstBaseAggregator parent;
  GstPad *srcpad;

  gboolean send_segment;
};

struct _GstAggregatorClass
{
  GstBaseAggregatorClass parent_class;
};

static GstFlowReturn
gst_aggregator_aggregate (GstBaseAggregator * baseaggregator)
{
  GstAggregator *aggregator;

  aggregator = GST_AGGREGATOR (baseaggregator);

  if (aggregator->send_segment) {
    GstSegment segment;

    gst_segment_init (&segment, GST_FORMAT_BYTES);
    gst_pad_push_event (aggregator->srcpad,
        gst_event_new_stream_start ("test"));
    gst_pad_push_event (aggregator->srcpad, gst_event_new_segment (&segment));
    aggregator->send_segment = FALSE;
  }

  return GST_FLOW_OK;
}

G_DEFINE_TYPE (GstAggregator, gst_aggregator, GST_TYPE_BASE_AGGREGATOR);

static GstStaticPadTemplate gst_aggregator_src_template =
GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate gst_aggregator_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink_%u", GST_PAD_SINK, GST_PAD_REQUEST,
    GST_STATIC_CAPS_ANY);

static void
gst_aggregator_class_init (GstAggregatorClass * klass)
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseAggregatorClass *base_aggregator_class =
      (GstBaseAggregatorClass *) klass;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_aggregator_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_aggregator_sink_template));
  gst_element_class_set_static_metadata (gstelement_class, "Aggregator",
      "Testing", "Combine N buffers", "Stefan Sauer <ensonic@users.sf.net>");

  base_aggregator_class->aggregate =
      GST_DEBUG_FUNCPTR (gst_aggregator_aggregate);
}

static void
gst_aggregator_init (GstAggregator * aggregator)
{
  GstPadTemplate *template;

  template = gst_static_pad_template_get (&gst_aggregator_src_template);
  aggregator->srcpad = gst_pad_new_from_template (template, "src");
  gst_object_unref (template);

  GST_PAD_SET_PROXY_CAPS (aggregator->srcpad);
  gst_element_add_pad (GST_ELEMENT (aggregator), aggregator->srcpad);
  aggregator->send_segment = TRUE;
}

static gboolean
gst_aggregator_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "aggregator", GST_RANK_NONE,
      GST_TYPE_AGGREGATOR);
}

static gboolean
gst_aggregator_plugin_register (void)
{
  return gst_plugin_register_static (GST_VERSION_MAJOR,
      GST_VERSION_MINOR,
      "aggregator",
      "Combine buffers",
      gst_aggregator_plugin_init,
      VERSION, GST_LICENSE, PACKAGE, GST_PACKAGE_NAME, GST_PACKAGE_ORIGIN);
}

GST_START_TEST (test_collect)
{
  GstElement *aggregator;
  GstBuffer *buf1, *buf2;

  GstPad *spad1, *spad2;

  aggregator = gst_element_factory_make ("aggregator", NULL);

  buf1 = gst_buffer_new ();
  buf2 = gst_buffer_new ();

  gst_element_set_state (aggregator, GST_STATE_PLAYING);

  spad1 = gst_element_get_request_pad (aggregator, "sink_%u");

  spad2 = gst_element_get_request_pad (aggregator, "sink_%u");

  fail_unless (spad1 != NULL);
  fail_unless (spad2 != NULL);
  gst_buffer_unref (buf1);
  gst_buffer_unref (buf2);
}

GST_END_TEST;

static Suite *
gst_base_aggregator_suite (void)
{
  Suite *suite;
  TCase *general;

  gst_aggregator_plugin_register ();

  suite = suite_create ("GstBaseAggregator");

  general = tcase_create ("general");
  suite_add_tcase (suite, general);
  tcase_add_test (general, test_collect);

  return suite;
}

GST_CHECK_MAIN (gst_base_aggregator);
