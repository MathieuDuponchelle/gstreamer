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

struct _GstBaseAggregatorPrivate
{
  gpointer *nothing;
};

static void
gst_base_aggregator_finalize (GObject * object)
{
}

static void
gst_base_aggregator_class_init (GstBaseAggregatorClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  g_type_class_add_private (klass, sizeof (GstBaseAggregatorPrivate));

  GST_DEBUG_CATEGORY_INIT (base_aggregator_debug, "baseaggregator", 0,
      "GstBaseAggregator");

  gobject_class->finalize = GST_DEBUG_FUNCPTR (gst_base_aggregator_finalize);
}

static void
gst_base_aggregator_init (GstBaseAggregator * self)
{
  self->priv =
      G_TYPE_INSTANCE_GET_PRIVATE (self, GST_TYPE_BASE_AGGREGATOR,
      GstBaseAggregatorPrivate);
}

static GstFlowReturn
_chain (GstBaseAggregator * base_aggregator)
{
  return GST_FLOW_OK;
}

static gboolean
_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  return TRUE;
}

static gboolean
_query (GstPad * pad, GstObject * parent, GstQuery * query)
{
  return TRUE;
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
gst_base_aggregator_pad_class_init (GstBaseAggregatorPadClass * klass)
{
  GObjectClass *gobject_class = (GObjectClass *) klass;

  g_type_class_add_private (klass, sizeof (GstBaseAggregatorPadPrivate));

  gobject_class->finalize =
      GST_DEBUG_FUNCPTR (gst_base_aggregator_pad_finalize);
}

static void
gst_base_aggregator_pad_init (GstBaseAggregatorPad * pad)
{
  GstPad *bpad = GST_PAD (pad);

  gst_pad_set_chain_function (bpad,
      GST_DEBUG_FUNCPTR ((GstPadChainFunction) _chain));
  gst_pad_set_event_function (bpad,
      GST_DEBUG_FUNCPTR ((GstPadEventFunction) _event));
  gst_pad_set_query_function (bpad,
      GST_DEBUG_FUNCPTR ((GstPadQueryFunction) _query));
}

GstBaseAggregatorPad *
gst_base_aggregator_pad_new (void)
{
  return g_object_new (GST_TYPE_BASE_AGGREGATOR_PAD, NULL);
}
