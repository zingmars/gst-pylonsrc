/* GStreamer
 * Copyright (C) 2017 Ingmars Melkis <zingmars@playgineering.com>
 * Copyright (C) 2018 Ingmars Melkis <contact@zingmars.me>
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Alternatively, the contents of this file may be used under the
 * GNU Lesser General Public License Version 2.1 (the "LGPL"), in
 * which case the following provisions apply instead of the ones
 * mentioned above:
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/**
 * SECTION:element-fpsfilter
 *
 * A gstreamer element that calculates the FPS value of the stream.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0  videotestsrc ! video/x-raw, framerate=1/1 ! videoconvert ! fpsfilter ! xvimagesink
 * ]|
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#  include <config.h>
#endif

#include <gst/gst.h>

#include "gstfpsfilter.h"

GST_DEBUG_CATEGORY_STATIC (gst_fps_filter_debug);
#define GST_CAT_DEFAULT gst_fps_filter_debug
#define GST_MESSAGE_OBJECT(obj, ...) GST_CAT_LEVEL_LOG(GST_CAT_DEFAULT, GST_LEVEL_NONE, obj, __VA_ARGS__)

/* Filter signals and args */
enum
{
  LAST_SIGNAL
};

/* Parameters */
enum
{
  PROP_0,
  PROP_REPORTTIME
};

/* the capabilities of the inputs and outputs.
 *
 * describe the real formats here.
 */
static GstStaticPadTemplate sink_factory = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("ANY")
    );

#define gst_fps_filter_parent_class parent_class
G_DEFINE_TYPE (GstFpsFilter, gst_fps_filter, GST_TYPE_ELEMENT);

static void gst_fps_filter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_fps_filter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);

static gboolean gst_fps_filter_sink_event (GstPad * pad, GstObject * parent, GstEvent * event);
static GstFlowReturn gst_fps_filter_chain (GstPad * pad, GstObject * parent, GstBuffer * buf);

/* GObject vmethod implementations */

/* initialize the fpsfilter's class */
static void
gst_fps_filter_class_init (GstFpsFilterClass * klass)
{
  GObjectClass *gobject_class;
  GstElementClass *gstelement_class;

  gobject_class = (GObjectClass *) klass;
  gstelement_class = (GstElementClass *) klass;

  gobject_class->set_property = gst_fps_filter_set_property;
  gobject_class->get_property = gst_fps_filter_get_property;

  gst_element_class_set_details_simple(gstelement_class,
    "FPS counter",
    "Filter",
    "Calculates the time between frames and outputs the stream's framerate",
    "Ingmars Melkis <zingmars@playgineering.com>");

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&src_factory));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sink_factory));

  g_object_class_install_property(gobject_class, PROP_REPORTTIME, 
      g_param_spec_uint64("reporttime", "reporttime", "(Number) Time between fps reports in miliseconds (default - 1000)",
          0, G_MAXUINT64, 1000,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

/* initialize the new element
 * instantiate pads and add them to element
 * set pad calback functions
 * initialize instance structure
 */
static void
gst_fps_filter_init (GstFpsFilter * filter)
{
  filter->sinkpad = gst_pad_new_from_static_template (&sink_factory, "sink");
  gst_pad_set_event_function (filter->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_fps_filter_sink_event));
  gst_pad_set_chain_function (filter->sinkpad,
                              GST_DEBUG_FUNCPTR(gst_fps_filter_chain));
  GST_PAD_SET_PROXY_CAPS (filter->sinkpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->sinkpad);

  filter->srcpad = gst_pad_new_from_static_template (&src_factory, "src");
  GST_PAD_SET_PROXY_CAPS (filter->srcpad);
  gst_element_add_pad (GST_ELEMENT (filter), filter->srcpad);

  filter->frames = 0;
  filter->lastframetime = 0;
  filter->elapsedtime = 0;
  filter->reporttime = 1000;
}

static void
gst_fps_filter_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstFpsFilter *filter = GST_FPSFILTER (object);

  switch (prop_id) {
    case PROP_REPORTTIME:
      filter->reporttime = g_value_get_uint64(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static void
gst_fps_filter_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstFpsFilter *filter = GST_FPSFILTER (object);

  switch (prop_id) {
    case PROP_REPORTTIME:
      g_value_set_uint64(value, filter->reporttime);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/* GstElement vmethod implementations */

/* this function handles sink events */
static gboolean
gst_fps_filter_sink_event (GstPad * pad, GstObject * parent, GstEvent * event)
{
  GstFpsFilter *filter;
  gboolean ret;

  filter = GST_FPSFILTER (parent);

  GST_LOG_OBJECT (filter, "Received %s event: %" GST_PTR_FORMAT,
      GST_EVENT_TYPE_NAME (event), event);

  switch (GST_EVENT_TYPE (event)) {
    case GST_EVENT_CAPS:
    {
      GstCaps * caps;

      gst_event_parse_caps (event, &caps);
      /* do something with the caps */

      /* and forward */
      ret = gst_pad_event_default (pad, parent, event);
      break;
    }
    default:
      ret = gst_pad_event_default (pad, parent, event);
      break;
  }
  return ret;
}

/* chain function
 * this function does the actual processing
 */
static GstFlowReturn
gst_fps_filter_chain (GstPad * pad, GstObject * parent, GstBuffer * buf)
{
  GstFpsFilter *filter;
  filter = GST_FPSFILTER (parent);

  if(GST_STATE(filter) == GST_STATE_PLAYING) {
    filter->frames = filter->frames + 1;
    #pragma GCC diagnostic ignored "-Wincompatible-pointer-types"
    gint64 time = GST_TIME_AS_MSECONDS(gst_clock_get_time(gst_element_get_clock(filter)));
    #pragma GCC diagnostic pop
    filter->elapsedtime = filter->elapsedtime + (time-filter->lastframetime);

    if(filter->elapsedtime >= filter->reporttime) {
      int64_t frames = filter->frames;
      int64_t elapsedtime = filter->elapsedtime;
      
      if(filter->elapsedtime != filter->reporttime) { //Most of the time our frame won't tick right on the dot. In those cases we're already processing a frame in the next cycle.
        frames=frames-1;
        elapsedtime = filter->elapsedtime-filter->elapsedtime%filter->reporttime;
      }

      GST_MESSAGE_OBJECT(filter, "FPS: %.0f (Calculated time per frame: %.1fms)", frames/((float)elapsedtime/1000), (float)elapsedtime/frames);

      filter->frames = filter->frames-frames;
      filter->elapsedtime = filter->elapsedtime-elapsedtime; //Carry over the extra time spent on the previous frame.
    }

    filter->lastframetime = time;
  }

  return gst_pad_push (filter->srcpad, buf);
}


/* entry point to initialize the plug-in
 * initialize the plug-in itself
 * register the element factories and other features
 */
static gboolean
fpsfilter_init (GstPlugin * fpsfilter)
{
  /* debug category for fltering log messages
   *
   * exchange the string 'Template fpsfilter' with your description
   */
  GST_DEBUG_CATEGORY_INIT (gst_fps_filter_debug, "fpsfilter",
      0, "Template fpsfilter");

  return gst_element_register (fpsfilter, "fpsfilter", GST_RANK_NONE,
      GST_TYPE_FPSFILTER);
}

/* PACKAGE: this is usually set by autotools depending on some _INIT macro
 * in configure.ac and then written into and defined in config.h, but we can
 * just set it ourselves here in case someone doesn't use autotools to
 * compile this code. GST_PLUGIN_DEFINE needs PACKAGE to be defined.
 */
#ifndef VERSION
#define VERSION "1.1.0"
#endif
#ifndef PACKAGE
#define PACKAGE "gstpylon"
#endif
#ifndef PACKAGE_NAME
#define PACKAGE_NAME "gstpylon"
#endif
#ifndef GST_PACKAGE_ORIGIN
#define GST_PACKAGE_ORIGIN "http://www.playgineering.com/"
#endif

/* gstreamer looks for this structure to register fpsfilters
 *
 * exchange the string 'Template fpsfilter' with your fpsfilter description
 */
GST_PLUGIN_DEFINE (
    GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    fpsfilter,
    "A plugin that calculates a stream's framerate value.",
    fpsfilter_init,
    VERSION,
    "LGPL",
    PACKAGE,
    GST_PACKAGE_ORIGIN
)
