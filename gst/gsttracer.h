/* GStreamer
 * Copyright (C) 2013 Stefan Sauer <ensonic@users.sf.net>
 *
 * gsttracer.h: tracing subsystem
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

#ifndef __GST_TRACER_H__
#define __GST_TRACER_H__

#include <glib.h>
#include <glib-object.h>
#include <gst/gstconfig.h>
#include <gst/gstbin.h>

G_BEGIN_DECLS

#ifndef GST_DISABLE_GST_DEBUG

/* hook flags and ids */

typedef enum
{
  GST_TRACER_HOOK_NONE      = 0,
  GST_TRACER_HOOK_BUFFERS   = (1 << 0),
  GST_TRACER_HOOK_EVENTS    = (1 << 1),
  GST_TRACER_HOOK_MESSAGES  = (1 << 2),
  GST_TRACER_HOOK_QUERIES   = (1 << 3),
  GST_TRACER_HOOK_TOPOLOGY  = (1 << 4),
  /*
  GST_TRACER_HOOK_TIMER
  */
  GST_TRACER_HOOK_ALL       = (1 << 5) - 1
} GstTracerHook;

typedef enum
{
  GST_TRACER_HOOK_ID_BUFFERS = 0,
  GST_TRACER_HOOK_ID_EVENTS,
  GST_TRACER_HOOK_ID_MESSAGES,
  GST_TRACER_HOOK_ID_QUERIES,
  GST_TRACER_HOOK_ID_TOPLOGY,
  /*
  GST_TRACER_HOOK_ID_TIMER
  */
  GST_TRACER_HOOK_ID_LAST
} GstTracerHookId;

typedef enum
{
  GST_TRACER_MESSAGE_ID_PAD_PUSH_PRE = 0,
  GST_TRACER_MESSAGE_ID_PAD_PUSH_POST,
  GST_TRACER_MESSAGE_ID_PAD_PUSH_LIST_PRE,
  GST_TRACER_MESSAGE_ID_PAD_PUSH_LIST_POST,
  GST_TRACER_MESSAGE_ID_PAD_PULL_RANGE_PRE,
  GST_TRACER_MESSAGE_ID_PAD_PULL_RANGE_POST,
  GST_TRACER_MESSAGE_ID_PAD_PUSH_EVENT_PRE,
  GST_TRACER_MESSAGE_ID_PAD_PUSH_EVENT_POST,
  GST_TRACER_MESSAGE_ID_ELEMENT_POST_MESSAGE_PRE,
  GST_TRACER_MESSAGE_ID_ELEMENT_POST_MESSAGE_POST,
  GST_TRACER_MESSAGE_ID_ELEMENT_QUERY_PRE,
  GST_TRACER_MESSAGE_ID_ELEMENT_QUERY_POST,
  GST_TRACER_MESSAGE_ID_LAST
} GstTracerMessageId;

/* tracing plugins */

typedef struct _GstTracer GstTracer;
typedef struct _GstTracerPrivate GstTracerPrivate;
typedef struct _GstTracerClass GstTracerClass;

#define GST_TYPE_TRACER            (gst_tracer_get_type())
#define GST_TRACER(obj)            (G_TYPE_CHECK_INSTANCE_CAST((obj),GST_TYPE_TRACER,GstTracer))
#define GST_TRACER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST((klass),GST_TYPE_TRACER,GstTracerClass))
#define GST_IS_TRACER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE((obj),GST_TYPE_TRACER))
#define GST_IS_TRACER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE((klass),GST_TYPE_TRACER))
#define GST_TRACER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS((obj),GST_TYPE_TRACER,GstTracerClass))
#define GST_TRACER_CAST(obj)       ((GstTracer *)(obj))

struct _GstTracer {
  GstObject        parent;
  /*< private >*/
  GstTracerPrivate *priv;
  gpointer _gst_reserved[GST_PADDING];
};

typedef void (*GstTracerInvokeFunction) (GstTracer * self, GstTracerHookId hid,
    GstTracerMessageId mid, va_list var_args);

struct _GstTracerClass {
  GstObjectClass parent_class;
  
  /* plugin vmethods */
  GstTracerInvokeFunction invoke;
  
  /*< private >*/
  gpointer _gst_reserved[GST_PADDING];
};

GType gst_tracer_get_type          (void);

/* tracing hooks */

void _priv_gst_tracer_init (void);
void _priv_gst_tracer_deinit (void);

/* tracing modules */

gboolean gst_tracer_register (GstPlugin * plugin, const gchar * name, GType type);

/* tracing helpers */

void gst_tracer_dispatch (GstTracerHookId hid, GstTracerMessageId mid, ...);

/* tracing module helpers */

void gst_tracer_log_trace (GstStructure * s);

extern gboolean _priv_tracer_enabled;
extern GList *_priv_tracers[GST_TRACER_HOOK_ID_LAST];

extern GstClockTime _priv_gst_info_start_time;
#define GST_TRACER_IS_ENABLED(id) \
  (_priv_tracer_enabled && (_priv_tracers[id] != NULL))

#define GST_TRACER_TS \
  GST_CLOCK_DIFF (_priv_gst_info_start_time, gst_util_get_timestamp ())

/* tracing hooks */

#define GST_TRACER_PAD_PUSH_PRE(pad, buffer) G_STMT_START{ \
  if (GST_TRACER_IS_ENABLED(GST_TRACER_HOOK_ID_BUFFERS)) { \
    gst_tracer_dispatch (GST_TRACER_HOOK_ID_BUFFERS, \
        GST_TRACER_MESSAGE_ID_PAD_PUSH_PRE, GST_TRACER_TS, \
        pad, buffer); \
  } \
}G_STMT_END

#define GST_TRACER_PAD_PUSH_POST(pad, res) G_STMT_START{ \
  if (GST_TRACER_IS_ENABLED(GST_TRACER_HOOK_ID_BUFFERS)) { \
    gst_tracer_dispatch (GST_TRACER_HOOK_ID_BUFFERS, \
        GST_TRACER_MESSAGE_ID_PAD_PUSH_POST, GST_TRACER_TS, \
        pad, res); \
  } \
}G_STMT_END

#define GST_TRACER_PAD_PUSH_LIST_PRE(pad, list) G_STMT_START{ \
  if (GST_TRACER_IS_ENABLED(GST_TRACER_HOOK_ID_BUFFERS)) { \
    gst_tracer_dispatch (GST_TRACER_HOOK_ID_BUFFERS, \
        GST_TRACER_MESSAGE_ID_PAD_PUSH_LIST_PRE, GST_TRACER_TS, \
        pad, list); \
  } \
}G_STMT_END

#define GST_TRACER_PAD_PUSH_LIST_POST(pad, res) G_STMT_START{ \
  if (GST_TRACER_IS_ENABLED(GST_TRACER_HOOK_ID_BUFFERS)) { \
    gst_tracer_dispatch (GST_TRACER_HOOK_ID_BUFFERS, \
        GST_TRACER_MESSAGE_ID_PAD_PUSH_LIST_POST, GST_TRACER_TS, \
        pad, res); \
  } \
}G_STMT_END

#define GST_TRACER_PAD_PULL_RANGE_PRE(pad, offset, size) G_STMT_START{ \
  if (GST_TRACER_IS_ENABLED(GST_TRACER_HOOK_ID_BUFFERS)) { \
    gst_tracer_dispatch (GST_TRACER_HOOK_ID_BUFFERS, \
        GST_TRACER_MESSAGE_ID_PAD_PULL_RANGE_PRE, GST_TRACER_TS, \
        pad, offset, size); \
  } \
}G_STMT_END

#define GST_TRACER_PAD_PULL_RANGE_POST(pad, buffer, res) G_STMT_START{ \
  if (GST_TRACER_IS_ENABLED(GST_TRACER_HOOK_ID_BUFFERS)) { \
    gst_tracer_dispatch (GST_TRACER_HOOK_ID_BUFFERS, \
        GST_TRACER_MESSAGE_ID_PAD_PULL_RANGE_POST, GST_TRACER_TS, \
        pad, buffer, res); \
  } \
}G_STMT_END

#define GST_TRACER_PAD_PUSH_EVENT_PRE(pad, event) G_STMT_START{ \
  if (GST_TRACER_IS_ENABLED(GST_TRACER_HOOK_ID_EVENTS)) { \
    gst_tracer_dispatch (GST_TRACER_HOOK_ID_EVENTS, \
        GST_TRACER_MESSAGE_ID_PAD_PUSH_EVENT_PRE, GST_TRACER_TS, \
        pad, event); \
  } \
}G_STMT_END

#define GST_TRACER_PAD_PUSH_EVENT_POST(pad, res) G_STMT_START{ \
  if (GST_TRACER_IS_ENABLED(GST_TRACER_HOOK_ID_EVENTS)) { \
    gst_tracer_dispatch (GST_TRACER_HOOK_ID_EVENTS, \
        GST_TRACER_MESSAGE_ID_PAD_PUSH_EVENT_POST, GST_TRACER_TS, \
        pad, res); \
  } \
}G_STMT_END

#define GST_TRACER_ELEMENT_POST_MESSAGE_PRE(element, message) G_STMT_START{ \
  if (GST_TRACER_IS_ENABLED(GST_TRACER_HOOK_ID_MESSAGES)) { \
    gst_tracer_dispatch (GST_TRACER_HOOK_ID_MESSAGES, \
        GST_TRACER_MESSAGE_ID_ELEMENT_POST_MESSAGE_PRE, GST_TRACER_TS, \
        element, message); \
  } \
}G_STMT_END

#define GST_TRACER_ELEMENT_POST_MESSAGE_POST(element, res) G_STMT_START{ \
  if (GST_TRACER_IS_ENABLED(GST_TRACER_HOOK_ID_MESSAGES)) { \
    gst_tracer_dispatch (GST_TRACER_HOOK_ID_MESSAGES, \
        GST_TRACER_MESSAGE_ID_ELEMENT_POST_MESSAGE_POST, GST_TRACER_TS, \
        element, res); \
  } \
}G_STMT_END

#define GST_TRACER_ELEMENT_QUERY_PRE(element, query) G_STMT_START{ \
  if (GST_TRACER_IS_ENABLED(GST_TRACER_HOOK_ID_QUERIES)) { \
    gst_tracer_dispatch (GST_TRACER_HOOK_ID_QUERIES, \
        GST_TRACER_MESSAGE_ID_ELEMENT_QUERY_PRE, GST_TRACER_TS, \
        element, query); \
  } \
}G_STMT_END

#define GST_TRACER_ELEMENT_QUERY_POST(element, res) G_STMT_START{ \
  if (GST_TRACER_IS_ENABLED(GST_TRACER_HOOK_ID_QUERIES)) { \
    gst_tracer_dispatch (GST_TRACER_HOOK_ID_QUERIES, \
        GST_TRACER_MESSAGE_ID_ELEMENT_QUERY_POST, GST_TRACER_TS, \
        element, res); \
  } \
}G_STMT_END

#else /* !GST_DISABLE_GST_DEBUG */

#define GST_TRACER_PAD_PUSH_PRE(pad, buffer)
#define GST_TRACER_PAD_PUSH_POST(pad, res)
#define GST_TRACER_PAD_PUSH_LIST_PRE(pad, list)
#define GST_TRACER_PAD_PUSH_LIST_POST(pad, res)
#define GST_TRACER_PAD_PULL_RANGE_PRE(pad, offset, size)
#define GST_TRACER_PAD_PULL_RANGE_POST(pad, buffer, res)
#define GST_TRACER_PAD_PUSH_EVENT_PRE(pad, event)
#define GST_TRACER_PAD_PUSH_EVENT_POST(pad, res)
#define GST_TRACER_ELEMENT_POST_MESSAGE_PRE(element, message)
#define GST_TRACER_ELEMENT_POST_MESSAGE_POST(element, res)
#define GST_TRACER_ELEMENT_QUERY_PRE(element, query)
#define GST_TRACER_ELEMENT_QUERY_POST(element, res)

#endif /* GST_DISABLE_GST_DEBUG */

G_END_DECLS

#endif /* __GST_TRACER_H__ */

