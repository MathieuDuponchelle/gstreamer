/*
 * baseaggregator.c - GstBaseAggregator testsuite
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

#define BUFFER_DURATION 100000000       /* 10 frames per second */

struct _GstAggregator
{
  GstBaseAggregator parent;

  guint64 timestamp;
};

struct _GstAggregatorClass
{
  GstBaseAggregatorClass parent_class;
};

static GstFlowReturn
gst_aggregator_aggregate (GstBaseAggregator * baseaggregator)
{
  GstIterator *iter;
  gboolean all_eos = TRUE;
  GstAggregator *aggregator;
  GstBuffer *buf;

  gboolean done_iterating = FALSE;

  aggregator = GST_AGGREGATOR (baseaggregator);

  iter = gst_element_iterate_sink_pads (GST_ELEMENT (aggregator));
  while (!done_iterating) {
    GstBuffer *buffer;
    GValue value = { 0, };
    GstBaseAggregatorPad *pad;

    switch (gst_iterator_next (iter, &value)) {
      case GST_ITERATOR_OK:
        pad = g_value_get_object (&value);

        if (pad->eos == FALSE)
          all_eos = FALSE;
        buffer = gst_base_aggregator_pad_get_buffer (pad);
        gst_buffer_replace (&buffer, NULL);

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

  if (all_eos == TRUE) {
    GST_DEBUG_OBJECT (aggregator, "no data available, must be EOS");
    gst_pad_push_event (baseaggregator->srcpad, gst_event_new_eos ());
    return GST_FLOW_EOS;
  }

  buf = gst_buffer_new ();
  GST_BUFFER_TIMESTAMP (buf) = aggregator->timestamp;
  GST_BUFFER_DURATION (buf) = BUFFER_DURATION;
  aggregator->timestamp += BUFFER_DURATION;

  gst_base_aggregator_finish_buffer (baseaggregator, buf);

  /* That's quite incorrect, we should return what finish_buffer returns, lazy
   * now
   */
  return GST_FLOW_OK;
}

#define gst_aggregator_parent_class parent_class
G_DEFINE_TYPE (GstAggregator, gst_aggregator, GST_TYPE_BASE_AGGREGATOR);

static void
gst_aggregator_class_init (GstAggregatorClass * klass)
{
  GstElementClass *gstelement_class = (GstElementClass *) klass;
  GstBaseAggregatorClass *base_aggregator_class =
      (GstBaseAggregatorClass *) klass;

  static GstStaticPadTemplate _src_template =
      GST_STATIC_PAD_TEMPLATE ("src", GST_PAD_SRC, GST_PAD_ALWAYS,
      GST_STATIC_CAPS_ANY);

  static GstStaticPadTemplate _sink_template =
      GST_STATIC_PAD_TEMPLATE ("sink_%u", GST_PAD_SINK, GST_PAD_REQUEST,
      GST_STATIC_CAPS_ANY);

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&_src_template));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&_sink_template));

  gst_element_class_set_static_metadata (gstelement_class, "Aggregator",
      "Testing", "Combine N buffers", "Stefan Sauer <ensonic@users.sf.net>");

  base_aggregator_class->aggregate =
      GST_DEBUG_FUNCPTR (gst_aggregator_aggregate);
}

static void
gst_aggregator_init (GstAggregator * self)
{
  GstBaseAggregator *agg = GST_BASE_AGGREGATOR (self);
  gst_segment_init (&agg->segment, GST_FORMAT_BYTES);
  self->timestamp = 0;
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

  /*                       ------------------
   * -----------   --------|--              |
   * | srcpad | -- | sinkpad |  aggregator  |
   * -----------   --------|--              |
   *                       ------------------
   *  This is for 1 Chain, we can have several
   */
} ChainData;

typedef struct
{
  GMainLoop *ml;
  GstPad *srcpad,               /* srcpad of the GstAggregator */
   *sinkpad;                    /* fake sinkpad to which GstAggregator.srcpad is linked */
  guint timeout_id;
  GstElement *aggregator;

  /* -----------------|
   * |             ----------    -----------
   * | aggregator  | srcpad | -- | sinkpad |
   * |             ----------    -----------
   * -----------------|
   */

  gint flush_start_events, flush_stop_events;
} TestData;

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

static gpointer
push_buffer (gpointer user_data)
{
  GstFlowReturn flow;
  GstCaps *caps;
  ChainData *chain_data = (ChainData *) user_data;
  GstSegment segment;

  gst_pad_push_event (chain_data->srcpad, gst_event_new_stream_start ("test"));

  caps = gst_caps_new_empty_simple ("foo/x-bar");
  gst_pad_push_event (chain_data->srcpad, gst_event_new_caps (caps));
  gst_caps_unref (caps);

  gst_segment_init (&segment, GST_FORMAT_TIME);
  gst_pad_push_event (chain_data->srcpad, gst_event_new_segment (&segment));

  GST_DEBUG ("Pushing buffer on pad: %s:%s",
      GST_DEBUG_PAD_NAME (chain_data->sinkpad));
  flow = gst_pad_push (chain_data->srcpad, chain_data->buffer);
  fail_unless (flow == chain_data->expected_result,
      "got flow %s instead of %s on %s:%s", gst_flow_get_name (flow),
      gst_flow_get_name (chain_data->expected_result),
      GST_DEBUG_PAD_NAME (chain_data->sinkpad));

  return NULL;
}

static gpointer
push_event (gpointer user_data)
{
  ChainData *chain_data = (ChainData *) user_data;

  fail_unless (gst_pad_push_event (chain_data->srcpad,
          chain_data->event) == TRUE);

  return NULL;
}

static gboolean
_aggregate_timeout (GMainLoop * ml)
{
  g_main_loop_quit (ml);

  fail_unless ("No buffer found on aggregator.srcpad -> TIMEOUT" == NULL);

  return FALSE;
}

static gboolean
_quit (GMainLoop * ml)
{
  g_main_loop_quit (ml);

  return G_SOURCE_REMOVE;
}

static GstPadProbeReturn
_aggregated_cb (GstPad * pad, GstPadProbeInfo * info, GMainLoop * ml)
{
  g_idle_add ((GSourceFunc) _quit, ml);

  return GST_PAD_PROBE_REMOVE;
}

static GstPadProbeReturn
downstream_probe_cb (GstPad * pad, GstPadProbeInfo * info, TestData * test)
{
  GST_DEBUG ("PROBING ");
  if (info->type & GST_PAD_PROBE_TYPE_EVENT_FLUSH) {
    if (GST_EVENT_TYPE (GST_PAD_PROBE_INFO_EVENT (info)) ==
        GST_EVENT_FLUSH_START) {

      g_atomic_int_inc (&test->flush_start_events);
      GST_DEBUG ("==========> FLUSH: %i", test->flush_start_events);
    } else if (GST_EVENT_TYPE (GST_PAD_PROBE_INFO_EVENT (info)) ==
        GST_EVENT_FLUSH_STOP)
      g_atomic_int_inc (&test->flush_stop_events);
  }

  return GST_PAD_PROBE_DROP;
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
_test_data_init (TestData * test, gboolean needs_flushing)
{
  test->aggregator = gst_element_factory_make ("aggregator", NULL);
  gst_element_set_state (test->aggregator, GST_STATE_PLAYING);
  test->ml = g_main_loop_new (NULL, TRUE);
  test->srcpad = GST_BASE_AGGREGATOR (test->aggregator)->srcpad;

  GST_DEBUG ("Srcpad: %p", test->srcpad);

  if (needs_flushing) {
    static gint num_sink_pads = 0;
    gchar *pad_name = g_strdup_printf ("sink%d", num_sink_pads);

    num_sink_pads += 1;
    test->sinkpad = gst_pad_new_from_static_template (&sinktemplate, pad_name);
    g_free (pad_name);
    fail_unless (gst_pad_link (test->srcpad, test->sinkpad) == GST_PAD_LINK_OK);
    gst_pad_add_probe (test->srcpad, GST_PAD_PROBE_TYPE_EVENT_DOWNSTREAM |
        GST_PAD_PROBE_TYPE_DATA_DOWNSTREAM |
        GST_PAD_PROBE_TYPE_EVENT_FLUSH,
        (GstPadProbeCallback) downstream_probe_cb, test, NULL);
  } else {
    gst_pad_add_probe (test->srcpad, GST_PAD_PROBE_TYPE_BUFFER,
        (GstPadProbeCallback) _aggregated_cb, test->ml, NULL);
  }


  test->timeout_id =
      g_timeout_add (1000, (GSourceFunc) _aggregate_timeout, test->ml);
}

static void
_test_data_clear (TestData * test)
{
  gst_element_set_state (test->aggregator, GST_STATE_NULL);
  gst_object_unref (test->aggregator);

  g_main_loop_unref (test->ml);
}

GST_START_TEST (test_aggregate)
{
  GThread *thread1, *thread2;

  ChainData data1 = { 0, };
  ChainData data2 = { 0, };
  TestData test = { 0, };

  _test_data_init (&test, FALSE);
  _chain_data_init (&data1, test.aggregator);
  _chain_data_init (&data2, test.aggregator);

  thread1 = g_thread_try_new ("gst-check", push_buffer, &data1, NULL);
  thread2 = g_thread_try_new ("gst-check", push_buffer, &data2, NULL);

  g_main_loop_run (test.ml);
  g_source_remove (test.timeout_id);

  GST_ERROR ("================ OUT =====================");

  /* these will return immediately as when the data is popped the threads are
   * unlocked and will terminate */
  g_thread_join (thread1);
  g_thread_join (thread2);

  _chain_data_clear (&data1);
  _chain_data_clear (&data2);
  _test_data_clear (&test);
}

GST_END_TEST;

GST_START_TEST (test_aggregate_eos)
{
  GThread *thread1, *thread2;

  ChainData data1 = { 0, };
  ChainData data2 = { 0, };
  TestData test = { 0, };

  _test_data_init (&test, FALSE);
  _chain_data_init (&data1, test.aggregator);
  _chain_data_init (&data2, test.aggregator);

  data2.event = gst_event_new_eos ();

  thread1 = g_thread_try_new ("gst-check", push_buffer, &data1, NULL);
  thread2 = g_thread_try_new ("gst-check", push_event, &data2, NULL);

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

#define NUM_BUFFERS 3
static void
handoff (GstElement * fakesink, GstBuffer * buf, GstPad * pad, guint * count)
{
  *count = *count + 1;
  GST_DEBUG ("HANDOFF: %i", *count);
}

/* Test a linear pipeline using aggregator */
GST_START_TEST (test_linear_pipeline)
{
  GstBus *bus;
  GstMessage *msg;
  GstElement *pipeline, *src, *agg, *sink;

  gint count = 0;

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_check_setup_element ("fakesrc");
  g_object_set (src, "num-buffers", NUM_BUFFERS, "sizetype", 2, "sizemax", 4,
      NULL);
  agg = gst_check_setup_element ("aggregator");
  sink = gst_check_setup_element ("fakesink");
  g_object_set (sink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (sink, "handoff", (GCallback) handoff, &count);

  fail_unless (gst_bin_add (GST_BIN (pipeline), src));
  fail_unless (gst_bin_add (GST_BIN (pipeline), agg));
  fail_unless (gst_bin_add (GST_BIN (pipeline), sink));
  fail_unless (gst_element_link (src, agg));
  fail_unless (gst_element_link (agg, sink));

  bus = gst_element_get_bus (pipeline);
  fail_if (bus == NULL);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_if (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_EOS);
  gst_message_unref (msg);

  fail_unless_equals_int (count, NUM_BUFFERS);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_two_src_pipeline)
{
  GstBus *bus;
  GstMessage *msg;
  GstElement *pipeline, *src, *src1, *agg, *sink;

  gint count = 0;

  pipeline = gst_pipeline_new ("pipeline");
  src = gst_element_factory_make ("fakesrc", NULL);
  g_object_set (src, "num-buffers", NUM_BUFFERS, "sizetype", 2, "sizemax", 4,
      NULL);

  src1 = gst_element_factory_make ("fakesrc", NULL);
  g_object_set (src1, "num-buffers", NUM_BUFFERS + 1, "sizetype", 2, "sizemax",
      4, NULL);

  agg = gst_check_setup_element ("aggregator");
  sink = gst_check_setup_element ("fakesink");
  g_object_set (sink, "signal-handoffs", TRUE, NULL);
  g_signal_connect (sink, "handoff", (GCallback) handoff, &count);

  fail_unless (gst_bin_add (GST_BIN (pipeline), src));
  fail_unless (gst_bin_add (GST_BIN (pipeline), src1));
  fail_unless (gst_bin_add (GST_BIN (pipeline), agg));
  fail_unless (gst_bin_add (GST_BIN (pipeline), sink));
  fail_unless (gst_element_link (src, agg));
  fail_unless (gst_element_link (src1, agg));
  fail_unless (gst_element_link (agg, sink));

  bus = gst_element_get_bus (pipeline);
  fail_if (bus == NULL);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);

  msg = gst_bus_poll (bus, GST_MESSAGE_EOS | GST_MESSAGE_ERROR, -1);
  fail_if (GST_MESSAGE_TYPE (msg) != GST_MESSAGE_EOS);
  gst_message_unref (msg);

  fail_unless_equals_int (count, NUM_BUFFERS + 1);

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_END_TEST;

GST_START_TEST (test_flushing_seek)
{
  GstEvent *event;
  GThread *thread1, *thread2;

  ChainData data1 = { 0, };
  ChainData data2 = { 0, };
  TestData test = { 0, };

  _test_data_init (&test, TRUE);

  /* Queue a buffer in agg:sink_1. Then do a flushing seek and check that the
   * new flushing seek logic is triggered. On the first FLUSH_START call the
   * buffers queued in collectpads should get flushed. Only one FLUSH_START and
   * one FLUSH_STOP should be forwarded downstream.
   */
  _chain_data_init (&data1, test.aggregator);
  _chain_data_init (&data2, test.aggregator);
  GST_BUFFER_TIMESTAMP (data2.buffer) = 0;

  gst_segment_init (&GST_BASE_AGGREGATOR (test.aggregator)->segment,
      GST_FORMAT_TIME);
  /* now do a successful flushing seek */
  event = gst_event_new_seek (1, GST_FORMAT_TIME, GST_SEEK_FLAG_FLUSH,
      GST_SEEK_TYPE_SET, 0, GST_SEEK_TYPE_SET, 10 * GST_SECOND);
  fail_unless (gst_pad_send_event (test.srcpad, event));

  /* flushing starts once one of the upstream elements sends the first
   * FLUSH_START */
  fail_unless_equals_int (test.flush_start_events, 0);
  fail_unless_equals_int (test.flush_stop_events, 0);

  /* flush ogg:sink_0. This flushs collectpads, calls ::flush() and sends
   * FLUSH_START downstream */
  GST_DEBUG ("Flushin: %s:%s", GST_DEBUG_PAD_NAME (data2.sinkpad));
  fail_unless (gst_pad_push_event (data2.srcpad, gst_event_new_flush_start ()));

  /* expect this buffer to be flushed */
  data2.expected_result = GST_FLOW_FLUSHING;
  thread2 = g_thread_try_new ("gst-check", push_buffer, &data2, NULL);

  fail_unless (gst_pad_push_event (data1.srcpad, gst_event_new_flush_start ()));
  fail_unless_equals_int (test.flush_start_events, 1);
  fail_unless_equals_int (test.flush_stop_events, 0);

  /* the first FLUSH_STOP is not forwarded downstream */
  fail_unless (gst_pad_push_event (data1.srcpad,
          gst_event_new_flush_stop (TRUE)));
  fail_unless_equals_int (test.flush_start_events, 1);
  fail_unless_equals_int (test.flush_stop_events, 0);

  /* at this point even the other pad agg:sink_1 should be flushing so thread2
   * should have stopped */
  g_thread_join (thread2);

  /* push a buffer on agg:sink_0 to trigger one collect after flushing to verify
   * that flushing completes once all the pads have been flushed */
  thread1 = g_thread_try_new ("gst-check", push_buffer, &data1, NULL);

  /* flush agg:sink_1 as well. This completes the flushing seek so a FLUSH_STOP is
   * sent downstream */
  gst_pad_push_event (data2.srcpad, gst_event_new_flush_stop (TRUE));

  /* and the last FLUSH_STOP is forwarded downstream */
  fail_unless_equals_int (test.flush_start_events, 1);

  /*  Check collected */
  gst_pad_add_probe (test.srcpad, GST_PAD_PROBE_TYPE_BUFFER,
      (GstPadProbeCallback) _aggregated_cb, test.ml, NULL);

  data2.event = gst_event_new_eos ();
  GST_DEBUG ("EEEEEEEEEEEEEEEEEEEEEEEEE =-> Srcpad: %p", data2.srcpad);
  thread2 = g_thread_try_new ("gst-check", push_event, &data2, NULL);

  g_main_loop_run (test.ml);
  g_source_remove (test.timeout_id);

  fail_unless_equals_int (test.flush_stop_events, 1);

  /* these will return immediately as at this point the threads have been
   * unlocked and are finished */
  g_thread_join (thread1);
  g_thread_join (thread2);

  _test_data_clear (&test);

}

GST_END_TEST;

static void
infinite_seek (guint num_srcs, guint num_seeks)
{
  GstBus *bus;
  GstMessage *message;
  GstElement *pipeline, *src, *agg, *sink;

  gint count = 0, i;
  gboolean seek_res, carry_on = TRUE;

  gst_init (NULL, NULL);

  pipeline = gst_pipeline_new ("pipeline");

  agg = gst_check_setup_element ("aggregator");
  sink = gst_check_setup_element ("fakesink");

  fail_unless (gst_bin_add (GST_BIN (pipeline), agg));
  fail_unless (gst_bin_add (GST_BIN (pipeline), sink));
  fail_unless (gst_element_link (agg, sink));

  for (i = 0; i < num_srcs; i++) {
    src = gst_element_factory_make ("fakesrc", NULL);
    g_object_set (src, "sizetype", 2, "sizemax", 4, NULL);
    fail_unless (gst_bin_add (GST_BIN (pipeline), src));
    fail_unless (gst_element_link (src, agg));
  }

  bus = gst_element_get_bus (pipeline);
  fail_if (bus == NULL);
  gst_element_set_state (pipeline, GST_STATE_PLAYING);
  while (count < num_seeks && carry_on) {
    message = gst_bus_poll (bus, GST_MESSAGE_ANY, GST_SECOND / 10);
    if (message) {
      switch (GST_MESSAGE_TYPE (message)) {
        case GST_MESSAGE_EOS:
        {
          /* we should check if we really finished here */
          GST_WARNING ("Got an EOS");
          carry_on = FALSE;
          break;
        }
        case GST_MESSAGE_STATE_CHANGED:
        {
          GstState new;

          if (GST_MESSAGE_SRC (message) == GST_OBJECT (pipeline)) {
            gst_message_parse_state_changed (message, NULL, &new, NULL);

            if (new != GST_STATE_PLAYING)
              break;

            GST_INFO ("Seeking (num: %i)", count);
            seek_res =
                gst_element_seek_simple (sink, GST_FORMAT_BYTES,
                GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE, 0);
            GST_INFO ("seek result is : %d", seek_res);
            fail_unless (seek_res != 0);
            count++;
          }

          break;
        }
        case GST_MESSAGE_ERROR:
          GST_ERROR ("Error on the bus: %" GST_PTR_FORMAT, message);
          carry_on = FALSE;
          break;
        default:
          break;
      }
      gst_message_unref (message);
    }
  }

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
}

GST_START_TEST (test_infinite_seek)
{
  infinite_seek (2, 500);
}

GST_END_TEST;

GST_START_TEST (test_infinite_seek_50_src)
{
  infinite_seek (50, 100);
}

GST_END_TEST;

GST_START_TEST (test_add_remove)
{
  GstBus *bus;
  guint num_iterations = 50;
  GstElement *pipeline, *src = NULL, *src1, *agg, *sink;

  gint count = 0;

  gst_init (NULL, NULL);

  pipeline = gst_pipeline_new ("pipeline");

  agg = gst_check_setup_element ("aggregator");
  sink = gst_check_setup_element ("fakesink");

  fail_unless (gst_bin_add (GST_BIN (pipeline), agg));
  fail_unless (gst_bin_add (GST_BIN (pipeline), sink));
  fail_unless (gst_element_link (agg, sink));

  bus = gst_element_get_bus (pipeline);
  fail_if (bus == NULL);

  GST_DEBUG_BIN_TO_DOT_FILE (GST_BIN (pipeline), GST_DEBUG_GRAPH_SHOW_ALL,
      "baseaggregator_infiniteseek");
  while (count < num_iterations) {
    GstPad *pad, *peer;

    src1 = gst_element_factory_make ("fakesrc", NULL);
    g_object_set (src1, "sizetype", 2, "sizemax", 4, NULL);
    fail_unless (gst_bin_add (GST_BIN (pipeline), src1));
    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
        GST_DEBUG_GRAPH_SHOW_ALL, "baseaggregator_added");
    fail_unless (gst_element_sync_state_with_parent (src1));
    fail_unless (gst_element_link (src1, agg));

    if (src) {
      pad = gst_element_get_static_pad (src, "src");
      peer = gst_pad_get_peer (pad);
      gst_pad_unlink (pad, peer);
      GST_ERROR ("HERE");
      gst_element_release_request_pad (agg, peer);
      fail_unless (gst_bin_remove (GST_BIN (pipeline), src));
      gst_element_set_state (src, GST_STATE_NULL);
    } else {
      gst_element_set_state (pipeline, GST_STATE_PLAYING);
      gst_element_get_state (pipeline, NULL, NULL, -1);
    }

    GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS (GST_BIN (pipeline),
        GST_DEBUG_GRAPH_SHOW_ALL, "baseaggregator_removed");

    src = src1;
    count++;
  }

  gst_element_set_state (pipeline, GST_STATE_NULL);
  gst_object_unref (bus);
  gst_object_unref (pipeline);
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
  tcase_add_test (general, test_aggregate_eos);
  tcase_add_test (general, test_flushing_seek);
  tcase_add_test (general, test_infinite_seek);
  tcase_add_test (general, test_infinite_seek_50_src);
  tcase_add_test (general, test_linear_pipeline);
  tcase_add_test (general, test_two_src_pipeline);
  tcase_add_test (general, test_add_remove);

  return suite;
}

GST_CHECK_MAIN (gst_base_aggregator);
