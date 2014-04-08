/* GStreamer
 * Copyright (C) 2014 Mathieu Duponchelle <mathieu.duponchelle@oencreed.com>
 * Copyright (C) 2014 Thibault Saunier <tsaunier@opencreed.com>
 *
 * gstcollectpads.c:
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

#ifndef __GST_BASE_AGGREGATOR_H__
#define __GST_BASE_AGGREGATOR_H__

#include <gst/gst.h>

G_BEGIN_DECLS

/**********************
 * GstBaseAggregatorPad API   *
 *********************/

#define GST_TYPE_BASE_AGGREGATOR_PAD            (gst_base_aggregator_pad_get_type())
#define GST_BASE_AGGREGATOR_PAD(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASE_AGGREGATOR_PAD, GstBaseAggregatorPad))
#define GST_BASE_AGGREGATOR_PAD_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BASE_AGGREGATOR_PAD, GstBaseAggregatorPadClass))
#define GST_BASE_AGGREGATOR_PAD_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),GST_TYPE_BASE_AGGREGATOR_PAD, GstBaseAggregatorPadClass))
#define GST_IS_BASE_AGGREGATOR_PAD(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASE_AGGREGATOR_PAD))
#define GST_IS_BASE_AGGREGATOR_PAD_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BASE_AGGREGATOR_PAD))

typedef struct _GstBaseAggregatorPad GstBaseAggregatorPad;
typedef struct _GstBaseAggregatorPadClass GstBaseAggregatorPadClass;
typedef struct _GstBaseAggregatorPadPrivate GstBaseAggregatorPadPrivate;

/**
 * GstBaseAggregatorPad:
 * @buffer: currently queued buffer.
 * @segment: last segment received.
 *
 * The implementation the GstPad to use with #GstBaseAggregator
 */
struct _GstBaseAggregatorPad
{
  GstPad        parent;

  GstBuffer     *buffer;
  GstSegment    segment;
  gboolean      eos;
  gboolean      flushing;

  gpointer _gst_reserved[GST_PADDING];
};

struct _GstBaseAggregatorPadClass
{
  GstPadClass parent_class;

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType gst_base_aggregator_pad_get_type(void);

GstBaseAggregatorPad * gst_base_aggregator_pad_new     (void);

/**********************
 * GstBaseAggregator API   *
 *********************/

#define GST_TYPE_BASE_AGGREGATOR            (gst_base_aggregator_get_type())
#define GST_BASE_AGGREGATOR(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_BASE_AGGREGATOR,GstBaseAggregator))
#define GST_BASE_AGGREGATOR_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_BASE_AGGREGATOR,GstBaseAggregatorClass))
#define GST_BASE_AGGREGATOR_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj),GST_TYPE_BASE_AGGREGATOR,GstBaseAggregatorClass))
#define GST_IS_BASE_AGGREGATOR(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_BASE_AGGREGATOR))
#define GST_IS_BASE_AGGREGATOR_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_BASE_AGGREGATOR))

typedef struct _GstBaseAggregator GstBaseAggregator;
typedef struct _GstBaseAggregatorPrivate GstBaseAggregatorPrivate;
typedef struct _GstBaseAggregatorClass GstBaseAggregatorClass;

#define GST_FLOW_CUSTOM_SUCCESS GST_FLOW_NOT_HANDLED

/**
 * GstBaseAggregator:
 * @base_aggregator_pads: #GList of #GstBaseAggregatorPad managed by this #GstBaseAggregator.
 *
 * Collectpads object.
 */
struct _GstBaseAggregator {
  GstElement      parent;

  GstPad *srcpad;

  /*< private >*/
  GstBaseAggregatorPrivate *priv;


  gpointer _gst_reserved[GST_PADDING];
};

struct _GstBaseAggregatorClass {
  GstElementClass parent_class;

  gboolean      (*flush)     (GstBaseAggregator *aggregator);
  gboolean      (*pad_event) (GstBaseAggregator *aggregate, GstPad *pad, GstEvent *event);
  gboolean      (*pad_query) (GstBaseAggregator *aggregate, GstPad *pad, GstQuery *query);
  GstFlowReturn (*aggregate) (GstBaseAggregator *aggregator);

  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType gst_base_aggregator_get_type(void);

gboolean   gst_base_aggregator_src_event_default (GstElement *aggregator,
                                                  GstPad *pad,
                                                  GstEvent *event);

gboolean
gst_base_aggregator_query_default (GstBaseAggregator * self, GstBaseAggregatorPad * bpad,
    GstQuery * query, gboolean discard);

G_END_DECLS

#endif /* __GST_BASE_AGGREGATOR_H__ */
