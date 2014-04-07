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
  GstIterator *iter;
  GstAggregator *aggregator;

  gboolean done_iterating = FALSE;

  aggregator = GST_AGGREGATOR (baseaggregator);

  if (aggregator->send_segment) {
    GstSegment segment;

    gst_segment_init (&segment, GST_FORMAT_BYTES);
    gst_pad_push_event (aggregator->srcpad,
        gst_event_new_stream_start ("test"));
    gst_pad_push_event (aggregator->srcpad, gst_event_new_segment (&segment));
    aggregator->send_segment = FALSE;
  }

  iter = gst_element_iterate_sink_pads (GST_ELEMENT (aggregator));
  while (!done_iterating) {
    GValue value = { 0, };
    GstBaseAggregatorPad *pad;

    switch (gst_iterator_next (iter, &value)) {
      case GST_ITERATOR_OK:
        pad = g_value_get_object (&value);

        fail_unless (GST_IS_BUFFER (pad->buffer));
        gst_buffer_replace (&pad->buffer, NULL);

        g_value_reset (&value);
        break;
      case GST_ITERATOR_RESYNC:
        gst_iterator_resync (iter);
        break;
      case GST_ITERATOR_ERROR:
        GST_WARNING_OBJECT (aggregator, "Sinkpads iteration error");
        done_iterating = TRUE;
        break;
      case GST_ITERATOR_DONE:
        done_iterating = TRUE;
        break;
    }
  }
  gst_iterator_free (iter);

  gst_pad_push (aggregator->srcpad, gst_buffer_new ());

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

typedef struct
{
  GstEvent *event;
  GstBuffer *buffer;
  GstElement *aggregator;
  GstPad *sinkpad, *srcpad;
  GstFlowReturn expected_result;
} ChainData;

typedef struct
{
  GMainLoop *ml;
  GstPad *srcpad;
  guint timeout_id;
  GstElement *aggregator;
} TestData;

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static gpointer
push_buffer (gpointer user_data)
{
  GstFlowReturn flow;
  GstCaps *caps;
  ChainData *test_data = (ChainData *) user_data;
  GstSegment segment;

  gst_pad_push_event (test_data->srcpad, gst_event_new_stream_start ("test"));

  caps = gst_caps_new_empty_simple ("foo/x-bar");
  gst_pad_push_event (test_data->srcpad, gst_event_new_caps (caps));
  gst_caps_unref (caps);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  gst_pad_push_event (test_data->srcpad, gst_event_new_segment (&segment));

  flow = gst_pad_push (test_data->srcpad, test_data->buffer);
  fail_unless (flow == test_data->expected_result, "got flow %s instead of OK",
      gst_flow_get_name (flow));

  return NULL;
}

static gboolean
_aggregate_timeout (GMainLoop * ml)
{
  g_main_loop_quit (ml);

  fail_unless ("No buffer found on aggregator.srcpad -> TIMEOUT" == NULL);

  return FALSE;
}

static GstPadProbeReturn
_aggregated_cb (GstPad * pad, GstPadProbeInfo * info, GMainLoop * ml)
{
  g_main_loop_quit (ml);

  return GST_PAD_PROBE_REMOVE;
}

/*
 * Not thread safe, will create a new ChainData which contains
 * an activated src pad linked to a requested sink pad of @agg, and
 * a newly allocated buffer ready to be pushed. Caller needs to
 * clear with _chain_data_clear after.
 */
static void
_chain_data_init (ChainData * data, GstElement * agg)
{
  static gint num_src_pads = 0;
  gchar *pad_name = g_strdup_printf ("src%d", num_src_pads);

  num_src_pads += 1;

  data->srcpad = gst_pad_new_from_static_template (&srctemplate, pad_name);
  g_free (pad_name);
  gst_pad_set_active (data->srcpad, TRUE);
  data->aggregator = agg;
  data->buffer = gst_buffer_new ();
  data->sinkpad = gst_element_get_request_pad (agg, "sink_%u");
  fail_unless (GST_IS_PAD (data->sinkpad));
  fail_unless (gst_pad_link (data->srcpad, data->sinkpad) == GST_PAD_LINK_OK);
}

static void
_chain_data_clear (ChainData * data)
{
  if (data->buffer)
    gst_buffer_unref (data->buffer);
  if (data->srcpad)
    gst_object_unref (data->srcpad);
  if (data->sinkpad)
    gst_object_unref (data->sinkpad);
}

static void
_test_data_init (TestData * test)
{
  test->aggregator = gst_element_factory_make ("aggregator", NULL);
  gst_element_set_state (test->aggregator, GST_STATE_PLAYING);
  test->ml = g_main_loop_new (NULL, TRUE);
  test->srcpad = gst_element_get_static_pad (test->aggregator, "src");

  gst_pad_add_probe (test->srcpad, GST_PAD_PROBE_TYPE_BUFFER,
      (GstPadProbeCallback) _aggregated_cb, test->ml, NULL);

  test->timeout_id =
      g_timeout_add (1000, (GSourceFunc) _aggregate_timeout, test->ml);
}

static void
_test_data_clear (TestData * test)
{
  gst_element_set_state (test->aggregator, GST_STATE_NULL);
  gst_object_unref (test->aggregator);
  gst_object_unref (test->srcpad);

  g_main_loop_unref (test->ml);
}

GST_START_TEST (test_aggregate)
{
  GThread *thread1, *thread2;

  ChainData data1 = { 0, };
  ChainData data2 = { 0, };
  TestData test = { 0, };

  _test_data_init (&test);
  _chain_data_init (&data1, test.aggregator);
  _chain_data_init (&data2, test.aggregator);

  thread1 = g_thread_try_new ("gst-check", push_buffer, &data1, NULL);
  thread2 = g_thread_try_new ("gst-check", push_buffer, &data2, NULL);

  g_main_loop_run (test.ml);
  g_source_remove (test.timeout_id);

  /* these will return immediately as when the data is popped the threads are
   * unlocked and will terminate */
  g_thread_join (thread1);
  g_thread_join (thread2);

  _chain_data_clear (&data1);
  _chain_data_clear (&data2);
  _test_data_clear (&test);
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
  tcase_add_test (general, test_aggregate);

  return suite;
}

GST_CHECK_MAIN (gst_base_aggregator);
