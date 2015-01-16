/* GStreamer GstTracer interface unit tests
 * Copyright (C) 2015 Mathieu Duponchelle <mathieu.duponchelle@collabora.com>
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

#include <gst/check/gstcheck.h>
#include <gst/gst.h>
#include <gst/gsttracer.h>

static gint traced_buffers = 0;

typedef struct
{
  GstPipeline *pipeline;
  GstPad *srcpad;
  GstTracer *tracer;
} TestData;

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

#define GST_TYPE_DUMMY_TRACER gst_dummy_tracer_get_type()

typedef GstTracer GstDummyTracer;
typedef GstTracerClass GstDummyTracerClass;

GType gst_dummy_tracer_get_type (void);

G_DEFINE_TYPE (GstDummyTracer, gst_dummy_tracer, GST_TYPE_TRACER);

static void
do_push_buffer_pre (GstTracer * tracer, guint64 ts, GstPad * pad,
    GstBuffer * buffer)
{
  traced_buffers += 1;
}

static void
gst_dummy_tracer_class_init (GstDummyTracerClass * klass)
{
}

static void
gst_dummy_tracer_init (GstDummyTracer * self)
{
}

static TestData *
create_test_data (void)
{
  GstPipeline *pipeline;
  GstElement *identity, *fakesink;
  GstPad *srcpad;
  GstCaps *caps = gst_caps_new_empty_simple ("video/x-raw");
  TestData *data = (TestData *) g_malloc0 (sizeof (TestData));

  data->tracer = g_object_new (GST_TYPE_DUMMY_TRACER, NULL);
  identity = gst_check_setup_element ("identity");
  fakesink = gst_check_setup_element ("fakesink");
  pipeline = GST_PIPELINE (gst_pipeline_new ("tracedbin"));

  gst_bin_add_many (GST_BIN (pipeline), identity, fakesink, NULL);
  gst_element_link (identity, fakesink);
  gst_element_set_state (GST_ELEMENT (pipeline), GST_STATE_PLAYING);
  srcpad = gst_check_setup_src_pad (identity, &srctemplate);
  gst_pad_set_active (srcpad, TRUE);
  gst_check_setup_events (srcpad, identity, caps, GST_FORMAT_TIME);
  gst_caps_unref (caps);
  data->pipeline = pipeline;
  data->srcpad = srcpad;
  return data;
}

static void
dispose_test_data (TestData * data)
{
  gst_object_unref (data->tracer);
  gst_element_set_state (GST_ELEMENT (data->pipeline), GST_STATE_NULL);
}

GST_START_TEST (test_simple_trace)
{
  TestData *data = create_test_data ();

  traced_buffers = 0;
  gst_tracing_register_hook (data->tracer, "pad-push-pre",
      G_CALLBACK (do_push_buffer_pre));
  gst_pad_push (data->srcpad, gst_buffer_new_and_alloc (42));

  /* One for srcpad, the other for identity's src */
  fail_unless_equals_int (traced_buffers, 2);
  dispose_test_data (data);
}

GST_END_TEST
GST_START_TEST (test_targeted_trace)
{
  TestData *data = create_test_data ();

  traced_buffers = 0;
  gst_tracing_register_hook_for_target (data->tracer, "pad-push-pre",
      G_CALLBACK (do_push_buffer_pre), data->srcpad);
  gst_pad_push (data->srcpad, gst_buffer_new_and_alloc (42));

  /* One for srcpad, the other for identity's src */
  fail_unless_equals_int (traced_buffers, 1);
  dispose_test_data (data);
}

GST_END_TEST static Suite *
gst_tracer_suite (void)
{
  Suite *s = suite_create ("GstTracer");
  TCase *tc_chain = tcase_create ("general");

  suite_add_tcase (s, tc_chain);
  tcase_add_test (tc_chain, test_simple_trace);
  tcase_add_test (tc_chain, test_targeted_trace);

  return s;
}

GST_CHECK_MAIN (gst_tracer);
