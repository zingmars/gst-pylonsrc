/* GStreamer
 * Copyright (C) 2016-2017 Ingmars Melkis <zingmars@playgineering.com>
 * Copyright (C) 2018 Ingmars Melkis <contact@zingmars.me>
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
 * Free Software Foundation, Inc., 51 Franklin Street, Suite 500,
 * Boston, MA 02110-1335, USA.
 */
/**
 * SECTION:element-gstpylonsrc
 *
 * The pylonsrc element uses Basler's pylonc API to get video from Basler's USB3 Vision cameras.
 *
 * <refsect2>
 * <title>Example launch line</title>
 * |[
 * gst-launch-1.0 -v pylonsrc ! bayer2rgb ! videoconvert ! xvimagesink
 * ]|
 * Outputs camera output to screen.
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstpylonsrc.h"
#include <gst/gst.h>

#include <malloc.h> //malloc
#include <string.h> //memcpy, strcmp
#include <inttypes.h> //int64 printing
#include <unistd.h> //sleep

#ifdef HAVE_ORC
#include <orc/orc.h>
#else
#define orc_memcpy(a,b,c) memcpy(a,b,c)
#endif

/* PylonC */
_Bool pylonc_reset_camera(GstPylonsrc* pylonsrc);
_Bool pylonc_connect_camera(GstPylonsrc* pylonsrc);
void  pylonc_disconnect_camera(GstPylonsrc* pylonsrc);
void  pylonc_print_camera_info(GstPylonsrc* pylonsrc, PYLON_DEVICE_HANDLE deviceHandle, int deviceId);
void  pylonc_initialize();
void  pylonc_terminate();

_Bool deviceConnected = FALSE;
#define NUM_BUFFERS 10
unsigned char* buffers[NUM_BUFFERS];
PYLON_STREAMBUFFER_HANDLE bufferHandle[NUM_BUFFERS];

/* debug category */
GST_DEBUG_CATEGORY_STATIC (gst_pylonsrc_debug_category);
#define GST_CAT_DEFAULT gst_pylonsrc_debug_category
#define GST_MESSAGE_OBJECT(obj, ...) GST_CAT_LEVEL_LOG(GST_CAT_DEFAULT, GST_LEVEL_NONE, obj, __VA_ARGS__)
#define PYLONC_CHECK_ERROR(obj, res) if (res != GENAPI_E_OK) { char* errMsg; size_t length; GenApiGetLastErrorMessage( NULL, &length ); errMsg = (char*) malloc( length ); GenApiGetLastErrorMessage( errMsg, &length ); GST_CAT_LEVEL_LOG(GST_CAT_DEFAULT, GST_LEVEL_NONE, obj, "PylonC error: %s (%#08x).\n", errMsg, (unsigned int) res); free(errMsg); GenApiGetLastErrorDetail( NULL, &length ); errMsg = (char*) malloc( length ); GenApiGetLastErrorDetail( errMsg, &length ); GST_CAT_LEVEL_LOG(GST_CAT_DEFAULT, GST_LEVEL_NONE, obj, "PylonC error: %s\n", errMsg); free(errMsg); goto error; }

/* prototypes */
static void gst_pylonsrc_set_property (GObject * object,
    guint property_id, const GValue * value, GParamSpec * pspec);
static void gst_pylonsrc_get_property (GObject * object,
    guint property_id, GValue * value, GParamSpec * pspec);
static void gst_pylonsrc_dispose (GObject * object);
static void gst_pylonsrc_finalize (GObject * object);

static gboolean gst_pylonsrc_start (GstBaseSrc * src);
static gboolean gst_pylonsrc_stop (GstBaseSrc * src);
static GstCaps *gst_pylonsrc_get_caps (GstBaseSrc * src, 
    GstCaps * filter);
static gboolean gst_pylonsrc_set_caps (GstBaseSrc * src, 
    GstCaps * caps);

static GstFlowReturn gst_pylonsrc_create (GstPushSrc *src, 
    GstBuffer **buf);

/* parameters */
enum
{
  PROP_0,
  PROP_CAMERA,
  PROP_HEIGHT,
  PROP_WIDTH,
  PROP_LIMITBANDWIDTH,
  PROP_MAXBANDWIDTH,
  PROP_SENSORREADOUTMODE,
  PROP_ACQUISITIONFRAMERATEENABLE,
  PROP_FPS,
  PROP_LIGHTSOURCE,
  PROP_AUTOEXPOSURE,
  PROP_EXPOSURE,
  PROP_AUTOWHITEBALANCE,
  PROP_BALANCERED,
  PROP_BALANCEGREEN,
  PROP_BALANCEBLUE,
  PROP_COLORREDHUE,
  PROP_COLORREDSATURATION,
  PROP_COLORYELLOWHUE,
  PROP_COLORYELLOWSATURATION,
  PROP_COLORGREENHUE,
  PROP_COLORGREENSATURATION,
  PROP_COLORCYANHUE,
  PROP_COLORCYANSATURATION,
  PROP_COLORBLUEHUE,
  PROP_COLORBLUESATURATION,
  PROP_COLORMAGENTAHUE,
  PROP_COLORMAGENTASATURATION,
  PROP_AUTOGAIN,
  PROP_GAIN,
  PROP_BLACKLEVEL,
  PROP_GAMMA,
  PROP_RESET,
  PROP_TESTIMAGE,
  PROP_CONTINUOUSMODE,
  PROP_IMAGEFORMAT,
  PROP_USERID,
  PROP_BASLERDEMOSAICING,
  PROP_DEMOSAICINGNOISEREDUCTION,
  PROP_DEMOSAICINGSHARPNESSENHANCEMENT,
  PROP_OFFSETX,
  PROP_CENTERX,
  PROP_OFFSETY,
  PROP_CENTERY,
  PROP_FLIPX,
  PROP_FLIPY,
  PROP_AUTOEXPOSUREUPPERLIMIT,
  PROP_AUTOEXPOSURELOWERLIMIT,
  PROP_GAINUPPERLIMIT,
  PROP_GAINLOWERLIMIT,
  PROP_AUTOPROFILE,
  PROP_AUTOBRIGHTNESSTARGET,
  PROP_TRANSFORMATIONSELECTOR,
  PROP_TRANSFORMATION00,
  PROP_TRANSFORMATION01,
  PROP_TRANSFORMATION02,
  PROP_TRANSFORMATION10,
  PROP_TRANSFORMATION11,
  PROP_TRANSFORMATION12,
  PROP_TRANSFORMATION20,
  PROP_TRANSFORMATION21,
  PROP_TRANSFORMATION22
};

/* pad templates */
static GstStaticPadTemplate gst_pylonsrc_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY
);

/* class initialisation */
G_DEFINE_TYPE_WITH_CODE (GstPylonsrc, gst_pylonsrc, GST_TYPE_PUSH_SRC,
  GST_DEBUG_CATEGORY_INIT (gst_pylonsrc_debug_category, "pylonsrc", 0,
  "debug category for pylonsrc element"));

static void
gst_pylonsrc_class_init (GstPylonsrcClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstPushSrcClass *push_src_class = GST_PUSH_SRC_CLASS (klass);
  GstBaseSrcClass *base_src_class = GST_BASE_SRC_CLASS (klass);

  gst_element_class_add_static_pad_template (GST_ELEMENT_CLASS(klass),
      &gst_pylonsrc_src_template);

  gst_element_class_set_static_metadata (GST_ELEMENT_CLASS(klass),
      "Basler's Python5 for Gstreamer", "Source/Video/Device", "Uses pylon5 to get video from Basler's USB3 Vision cameras for use with Gstreamer",
      "Ingmars Melkis <zingmars@playgineering.com>");

  gobject_class->set_property = gst_pylonsrc_set_property;
  gobject_class->get_property = gst_pylonsrc_get_property;
  gobject_class->dispose = gst_pylonsrc_dispose;
  gobject_class->finalize = gst_pylonsrc_finalize;

  base_src_class->start = GST_DEBUG_FUNCPTR(gst_pylonsrc_start);
  base_src_class->stop = GST_DEBUG_FUNCPTR(gst_pylonsrc_stop);
  base_src_class->get_caps = GST_DEBUG_FUNCPTR(gst_pylonsrc_get_caps);
  base_src_class->set_caps = GST_DEBUG_FUNCPTR(gst_pylonsrc_set_caps);

  push_src_class->create = GST_DEBUG_FUNCPTR(gst_pylonsrc_create);

  g_object_class_install_property (gobject_class, PROP_CAMERA,
      g_param_spec_int ("camera", "camera", "(Number) Camera ID as defined by Basler's API. If only one camera is connected this parameter will be ignored and the lone camera will be used. If there are multiple cameras and this parameter isn't defined, the plugin will output a list of available cameras and their IDs. Note that if there are multiple cameras available to the API and the camera parameter isn't defined then this plugin will not run.", 0,
          100, 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_HEIGHT,
      g_param_spec_int ("height", "height", "(Pixels) The height of the picture. Note that the camera will remember this setting, and will use values from the previous runs if you relaunch without specifying this parameter. Reconnect the camera or use the reset parameter to reset.", 0,
          10000, 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_WIDTH,
      g_param_spec_int ("width", "width", "(Pixels) The width of the picture. Note that the camera will remember this setting, and will use values from the previous runs if you relaunch without specifying this parameter. Reconnect the camera or use the reset parameter to reset.", 0,
          10000, 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_LIMITBANDWIDTH,
      g_param_spec_boolean ("limitbandwidth", "Link Throughput limit mode", "(true/false) Bandwidth limit mode. Disabling this will potentially allow the camera to reach higher frames per second, but can potentially damage your camera. Use with caution. Running the plugin without specifying this parameter will reset the value stored on the camera to `true`.", TRUE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_MAXBANDWIDTH,
      g_param_spec_int64 ("maxbandwidth", "Maximum bandwidth", "(Bytes per second) This property sets the maximum bandwidth the camera can use. The camera will only use as much as it needs for the specified resolution and framerate. This setting will have no effect if limitbandwidth is set to off. Note that the camera will remember this setting, and will use values from the previous runs if you relaunch without specifying this parameter. Reconnect the camera or use the reset parameter to reset.", 0,
          999999999, 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_SENSORREADOUTMODE,
      g_param_spec_string ("sensorreadoutmode", "Sensor readout mode", "(normal/fast) This property changes the sensor readout mode. Fast will allow for faster framerates, but might cause quality loss. It might be required to either increase max bandwidth or disabling bandwidth limiting for this to cause any noticeable change. Running the plugin without specifying this parameter will reset the value stored on the camera to \"normal\".", "Normal",
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_ACQUISITIONFRAMERATEENABLE,
      g_param_spec_boolean ("acquisitionframerateenable", "Custom FPS mode", "(true/false) Enables the use of custom fps values. Will be set to true if the fps poperty is set. Running the plugin without specifying this parameter will reset the value stored on the camera to false.", FALSE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_FPS,
      g_param_spec_double ("fps", "Framerate", "(Frames per second) Sets the framerate of the video coming from the camera. Setting the value too high might cause the plugin to crash. Note that if your pipeline proves to be too much for your computer then the resulting video won't be in the resolution you set. Setting this parameter will set acquisitionframerateenable to true. The value of this parameter will be saved to the camera, but it will have no effect unless either this or the acquisitionframerateenable parameters are set. Reconnect the camera or use the reset parameter to reset.", 0.0, 1024.0, 0.0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_LIGHTSOURCE,
      g_param_spec_string ("lightsource", "Lightsource preset", "(off, 2800k, 5000k, 6500k) Changes the colour balance settings to ones defined by presests. Just pick one that's closest to your environment's lighting. Running the plugin without specifying this parameter will reset the value stored on the camera to \"5000k\"", "5000k",
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_AUTOEXPOSURE,
      g_param_spec_string  ("autoexposure", "Automatic exposure setting", "(off, once, continuous) Controls whether or not the camera will try to adjust the exposure settings. Setting this parameter to anything but \"off\" will override the exposure parameter. Running the plugin without specifying this parameter will reset the value stored on the camera to \"off\"", "off",
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_EXPOSURE,
      g_param_spec_double ("exposure", "Exposure", "(Microseconds) Exposure time for the camera in microseconds. Will only have an effect if autoexposure is set to off (default). Higher numbers will cause lower frame rate. Note that the camera will remember this setting, and will use values from the previous runs if you relaunch without specifying this parameter. Reconnect the camera or use the reset parameter to reset.", 0.0, 1000000.0, 0.0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_AUTOWHITEBALANCE,
      g_param_spec_string ("autowhitebalance", "Automatic colour balancing", "(off, once, continuous) Controls whether or not the camera will try to adjust the white balance settings. Setting this parameter to anything but \"off\" will override the exposure parameter. Running the plugin without specifying this parameter will reset the value stored on the camera to \"off\"", "off",
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_BALANCERED,
      g_param_spec_double ("balancered", "Red balance", "Specifies the red colour balance. the autowhitebalance must be set to \"off\" for this property to have any effect. Note that the this value gets saved on the camera, and running this plugin again without specifying this value will cause the previous value being used. Use the reset parameter or reconnect the camera to reset.", 0.0, 15.9, 0.0,
        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_BALANCEGREEN,
      g_param_spec_double ("balancegreen", "Green balance", "Specifies the green colour balance. the autowhitebalance must be set to \"off\" for this property to have any effect. Note that the this value gets saved on the camera, and running this plugin again without specifying this value will cause the previous value being used. Use the reset parameter or reconnect the camera to reset.", 0.0, 15.9, 0.0,
        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_BALANCEBLUE,
      g_param_spec_double ("balanceblue", "Blue balance", "Specifies the blue colour balance. the autowhitebalance must be set to \"off\" for this property to have any effect. Note that the this value gets saved on the camera, and running this plugin again without specifying this value will cause the previous value being used. Use the reset parameter or reconnect the camera to reset.", 0.0, 15.9, 0.0,
        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_COLORREDHUE,
      g_param_spec_double ("colorredhue", "Red's hue", "Specifies the red colour's hue. Note that the this value gets saved on the camera, and running this plugin again without specifying this value will cause the previous value being used. Use the reset parameter or reconnect the camera to reset.", -4.0, 3.9, 0.0,
        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_COLORREDSATURATION,
      g_param_spec_double ("colorredsaturation", "Red's saturation", "Specifies the red colour's saturation. Note that the this value gets saved on the camera, and running this plugin again without specifying this value will cause the previous value being used. Use the reset parameter or reconnect the camera to reset.", 0.0, 1.9, 0.0,
        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_COLORYELLOWHUE,
      g_param_spec_double ("coloryellowhue", "Yellow's hue", "Specifies the yellow colour's hue. Note that the this value gets saved on the camera, and running this plugin again without specifying this value will cause the previous value being used. Use the reset parameter or reconnect the camera to reset.", -4.0, 3.9, 0.0,
        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_COLORYELLOWSATURATION,
      g_param_spec_double ("coloryellowsaturation", "Yellow's saturation", "Specifies the yellow colour's saturation. Note that the this value gets saved on the camera, and running this plugin again without specifying this value will cause the previous value being used. Use the reset parameter or reconnect the camera to reset.", 0.0, 1.9, 0.0,
        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_COLORGREENHUE,
      g_param_spec_double ("colorgreenhue", "Green's hue", "Specifies the green colour's hue. Note that the this value gets saved on the camera, and running this plugin again without specifying this value will cause the previous value being used. Use the reset parameter or reconnect the camera to reset.", -4.0, 3.9, 0.0,
        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_COLORGREENSATURATION,
      g_param_spec_double ("colorgreensaturation", "Green's saturation", "Specifies the green colour's saturation. Note that the this value gets saved on the camera, and running this plugin again without specifying this value will cause the previous value being used. Use the reset parameter or reconnect the camera to reset.", 0.0, 1.9, 0.0,
        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_COLORCYANHUE,
      g_param_spec_double ("colorcyanhue", "Cyan's hue", "Specifies the cyan colour's hue. Note that the this value gets saved on the camera, and running this plugin again without specifying this value will cause the previous value being used. Use the reset parameter or reconnect the camera to reset.", -4.0, 3.9, 0.0,
        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_COLORCYANSATURATION,
      g_param_spec_double ("colorcyansaturation", "Cyan's saturation", "Specifies the cyan colour's saturation. Note that the this value gets saved on the camera, and running this plugin again without specifying this value will cause the previous value being used. Use the reset parameter or reconnect the camera to reset.", 0.0, 1.9, 0.0,
        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));  
  g_object_class_install_property (gobject_class, PROP_COLORBLUEHUE,
      g_param_spec_double ("colorbluehue", "Blue's hue", "Specifies the blue colour's hue. Note that the this value gets saved on the camera, and running this plugin again without specifying this value will cause the previous value being used. Use the reset parameter or reconnect the camera to reset.", -4.0, 3.9, 0.0,
        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_COLORBLUESATURATION,
      g_param_spec_double ("colorbluesaturation", "Blue's saturation", "Specifies the blue colour's saturation. Note that the this value gets saved on the camera, and running this plugin again without specifying this value will cause the previous value being used. Use the reset parameter or reconnect the camera to reset.", 0.0, 1.9, 0.0,
        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_COLORMAGENTAHUE,
      g_param_spec_double ("colormagentahue", "Magenta's hue", "Specifies the magenta colour's hue. Note that the this value gets saved on the camera, and running this plugin again without specifying this value will cause the previous value being used. Use the reset parameter or reconnect the camera to reset.", -4.0, 3.9, 0.0,
        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_COLORMAGENTASATURATION,
      g_param_spec_double ("colormagentasaturation", "Magenta's saturation", "Specifies the magenta colour's saturation. Note that the this value gets saved on the camera, and running this plugin again without specifying this value will cause the previous value being used. Use the reset parameter or reconnect the camera to reset.", 0.0, 1.9, 0.0,
        (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_AUTOGAIN,
      g_param_spec_string ("autogain", "Automatic gain", "(off, once, continuous) Controls whether or not the camera will try to adjust the gain settings. Setting this parameter to anything but \"off\" will override the exposure parameter. Running the plugin without specifying this parameter will reset the value stored on the camera to \"off\"", "off",
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_GAIN,
      g_param_spec_double ("gain", "Gain", "(dB) Sets the gain added on the camera before sending the frame to the computer. The value of this parameter will be saved to the camera, but it will be set to 0 every time this plugin is launched without specifying gain or overriden if the autogain parameter is set to anything that's not \"off\". Reconnect the camera or use the reset parameter to reset the stored value.", 0.0, 12.0, 0.0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_BLACKLEVEL,
      g_param_spec_double ("blacklevel", "Black Level", "(DN) Sets stream's black level. This parameter is processed on the camera before the picture is sent to the computer. The value of this parameter will be saved to the camera, but it will be set to 0 every time this plugin is launched without specifying this parameter. Reconnect the camera or use the reset parameter to reset the stored value.", 0.0, 63.75, 0.0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_GAMMA,
      g_param_spec_double ("gamma", "Gamma", "Sets the gamma correction value. This parameter is processed on the camera before the picture is sent to the computer. The value of this parameter will be saved to the camera, but it will be set to 1.0 every time this plugin is launched without specifying this parameter. Reconnect the camera or use the reset parameter to reset the stored value.", 0.0, 3.9, 1.0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_RESET,
      g_param_spec_string ("reset", "Camera reset settings", "(off, before, after). Controls whether or when the camera's settings will be reset. Setting this to \"before\" will wipe the settings before the camera initialisation begins. Setting this to \"after\" will reset the device once the pipeline closes. This can be useful for debugging or when you want to use the camera with other software that doesn't reset the camera settings before use (such as PylonViewerApp).", "off",
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_TESTIMAGE,
      g_param_spec_int ("testimage", "Test image", "(1-6) Specifies a test image to show instead of a video stream. Useful for debugging. Will be disabled by default.", 0,
          6, 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_CONTINUOUSMODE,
      g_param_spec_boolean ("continuous", "Continuous mode", "(true/false) Used to switch between triggered and continuous mode. To switch to triggered mode this parameter has to be switched to false.", TRUE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_IMAGEFORMAT,
      g_param_spec_string ("imageformat", "Image format", "(Mono8/Bayer8/Bayer10/Bayer10p/RGB8/BGR8/YCbCr422_8). Determines the pixel format in which to send frames. Note that downstream elements might not support some of these.", "Bayer8",
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_USERID,
      g_param_spec_string ("userid", "Custom Device User ID", "(<string>) Sets the device custom id so that it can be identified later.", "",
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_BASLERDEMOSAICING,
      g_param_spec_boolean ("demosaicing", "Basler's Demosaicing mode'", "(true/false) Switches between simple and Basler's Demosaicing (PGI) mode. Note that this will not work if bayer output is used.", FALSE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_DEMOSAICINGNOISEREDUCTION,
      g_param_spec_double ("noisereduction", "Noise reduction", "Specifies the amount of noise reduction to apply. To use this Basler's demosaicing mode must be enabled. Setting this will enable demosaicing mode.", 0.0, 2.0, 0.0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_DEMOSAICINGSHARPNESSENHANCEMENT,
      g_param_spec_double ("sharpnessenhancement", "Sharpness enhancement", "Specifies the amount of sharpness enhancement to apply. To use this Basler's demosaicing mode must be enabled. Setting this will enable demosaicing mode.", 1.0, 3.98, 1.0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_OFFSETX,
      g_param_spec_int ("offsetx", "horizontal offset", "(0-10000) Determines the vertical offset. Note that the maximum offset value is calculated during initialisation, and will not be shown in this output.", 0,
          10000, 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_OFFSETY,
      g_param_spec_int ("offsety", "vertical offset", "(0-10000) Determines the vertical offset. Note that the maximum offset value is calculated during initialisation, and will not be shown in this output.", 0,
          10000, 0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_CENTERX,
      g_param_spec_boolean ("centerx", "center horizontally", "(true/false) Setting this will center the horizontal offset. Setting this to true this will cause the plugin to ignore offsetx value.", FALSE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_CENTERY,
      g_param_spec_boolean ("centery", "center vertically", "(true/false) Setting this will center the vertical offset. Setting this to true this will cause the plugin to ignore offsetx value.", FALSE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_FLIPX,
      g_param_spec_boolean ("flipx", "Flip horizontally", "(true/false) Setting this will flip the image horizontally.", FALSE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_FLIPY,
      g_param_spec_boolean ("flipy", "Flip vertically", "(true/false) Setting this will flip the image vertically.", FALSE,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_AUTOEXPOSURELOWERLIMIT,
      g_param_spec_double ("exposurelowerlimit", "Auto exposure lower limit", "(105-1000000) Sets the lower limit for the auto exposure function.", 105.0, 1000000.0, 105.0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_AUTOEXPOSUREUPPERLIMIT,
      g_param_spec_double ("exposureupperlimit", "Auto exposure upper limit", "(105-1000000) Sets the upper limit for the auto exposure function.", 105.0, 1000000.0, 1000000.0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_GAINUPPERLIMIT,
      g_param_spec_double ("gainupperlimit", "Auto exposure upper limit", "(0-12.00921) Sets the upper limit for the auto gain function.", 0.0, 12.00921, 12.00921,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_GAINLOWERLIMIT,
      g_param_spec_double ("gainlowerlimit", "Auto exposure lower limit", "(0-12.00921) Sets the lower limit for the auto gain function.", 0.0, 12.00921, 0.0,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_AUTOBRIGHTNESSTARGET,
      g_param_spec_double ("autobrightnesstarget", "Auto brightness target", "(0.19608-0.80392) Sets the brightness value the auto exposure function should strive for.", 0.19608, 0.80392, 0.50196,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_AUTOPROFILE,
      g_param_spec_string ("autoprofile", "Auto function minimize profile", "(gain/exposure) When the auto functions are on, this determines whether to focus on minimising gain or exposure.", "gain",
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_TRANSFORMATION00,
      g_param_spec_double ("transformation00", "Color Transformation selector 00", "Gain00 transformation selector.", -8.0, 7.96875, 1.4375,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_TRANSFORMATION01,
      g_param_spec_double ("transformation01", "Color Transformation selector 01", "Gain01 transformation selector.", -8.0, 7.96875, -0.3125,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_TRANSFORMATION02,
      g_param_spec_double ("transformation02", "Color Transformation selector 02", "Gain02 transformation selector.", -8.0, 7.96875, -0.125,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_TRANSFORMATION10,
      g_param_spec_double ("transformation10", "Color Transformation selector 10", "Gain10 transformation selector.", -8.0, 7.96875, -0.28125,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_TRANSFORMATION11,
      g_param_spec_double ("transformation11", "Color Transformation selector 11", "Gain11 transformation selector.", -8.0, 7.96875, 1.75,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_TRANSFORMATION12,
      g_param_spec_double ("transformation12", "Color Transformation selector 12", "Gain12 transformation selector.", -8.0, 7.96875, -0.46875,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_TRANSFORMATION20,
      g_param_spec_double ("transformation20", "Color Transformation selector 20", "Gain20 transformation selector.", -8.0, 7.96875, 0.0625,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_TRANSFORMATION21,
      g_param_spec_double ("transformation21", "Color Transformation selector 21", "Gain21 transformation selector.", -8.0, 7.96875, -0.8125,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_TRANSFORMATION22,
      g_param_spec_double ("transformation22", "Color Transformation selector 22", "Gain22 transformation selector.", -8.0, 7.96875, 1.75,
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
  g_object_class_install_property (gobject_class, PROP_TRANSFORMATIONSELECTOR,
      g_param_spec_string  ("transformationselector", "Color Transformation Selector", "(RGBRGB, RGBYUV, YUVRGB) Sets the type of color transformation done by the color transformation selectors.", "RGBRGB",
          (GParamFlags) (G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS)));
}

static gboolean
plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "pylonsrc", GST_RANK_NONE,
      GST_TYPE_PYLONSRC);
}

static void
gst_pylonsrc_init (GstPylonsrc *pylonsrc)
{
  GST_DEBUG_OBJECT(pylonsrc, "Initialising defaults");

  // Default parameter values
  pylonsrc->continuousMode = TRUE;
  pylonsrc->limitBandwidth = TRUE;
  pylonsrc->setFPS = FALSE;
  pylonsrc->demosaicing = FALSE;
  pylonsrc->centerx = FALSE;
  pylonsrc->centery = FALSE;
  pylonsrc->flipx = FALSE;
  pylonsrc->flipy = FALSE;
  pylonsrc->offsetx = 99999;
  pylonsrc->offsety = 99999;
  pylonsrc->cameraId = 9999;
  pylonsrc->height = 0;
  pylonsrc->width = 0;
  pylonsrc->maxBandwidth = 0;
  pylonsrc->testImage = 0;
  pylonsrc->sensorMode = "normal\0";
  pylonsrc->lightsource = "5000k\0";
  pylonsrc->autoexposure = "off\0";
  pylonsrc->autowhitebalance = "off\0";
  pylonsrc->autogain = "off\0";
  pylonsrc->reset = "off\0";
  pylonsrc->imageFormat = "bayer8\0";
  pylonsrc->userid = "\0";
  pylonsrc->autoprofile = "default\0";
  pylonsrc->transformationselector = "default\0";
  pylonsrc->fps = 0.0;
  pylonsrc->exposure = 0.0;
  pylonsrc->gain = 0.0;
  pylonsrc->blacklevel = 0.0;
  pylonsrc->gamma = 1.0;
  pylonsrc->balancered = 999.0;
  pylonsrc->balancegreen = 999.0;
  pylonsrc->balanceblue = 999.0;
  pylonsrc->redhue = 999.0;
  pylonsrc->redsaturation = 999.0;
  pylonsrc->yellowhue = 999.0;
  pylonsrc->yellowsaturation = 999.0;
  pylonsrc->greenhue = 999.0;
  pylonsrc->greensaturation = 999.0;
  pylonsrc->cyanhue = 999.0;
  pylonsrc->cyansaturation = 999.0;
  pylonsrc->bluehue = 999.0;
  pylonsrc->bluesaturation = 999.0;
  pylonsrc->magentahue = 999.0;
  pylonsrc->magentasaturation = 999.0;
  pylonsrc->sharpnessenhancement = 999.0;
  pylonsrc->noisereduction = 999.0;
  pylonsrc->autoexposureupperlimit = 9999999.0;
  pylonsrc->autoexposurelowerlimit = 9999999.0;
  pylonsrc->gainupperlimit = 999.0; 
  pylonsrc->gainlowerlimit = 999.0;
  pylonsrc->brightnesstarget = 999.0;
  pylonsrc->transformation00 = 999.0;
  pylonsrc->transformation01 = 999.0;
  pylonsrc->transformation02 = 999.0;
  pylonsrc->transformation10 = 999.0;
  pylonsrc->transformation11 = 999.0;
  pylonsrc->transformation12 = 999.0;
  pylonsrc->transformation20 = 999.0;
  pylonsrc->transformation21 = 999.0;
  pylonsrc->transformation22 = 999.0;

  // Mark this element as a live source (disable preroll)
  gst_base_src_set_live(GST_BASE_SRC(pylonsrc), TRUE);
  gst_base_src_set_format(GST_BASE_SRC(pylonsrc), GST_FORMAT_TIME);
  #pragma GCC diagnostic ignored "-Wincompatible-pointer-types" //Our class inherits the base source, so this will work.
  gst_base_src_set_do_timestamp(pylonsrc, TRUE);
  #pragma GCC diagnostic pop
}

/* plugin's parameters/properties */
void
gst_pylonsrc_set_property (GObject * object, guint property_id,
    const GValue * value, GParamSpec * pspec)
{
  GstPylonsrc *pylonsrc = GST_PYLONSRC (object);

  GST_DEBUG_OBJECT (pylonsrc, "Setting a property.");

  switch (property_id) {
    case PROP_CAMERA:
      pylonsrc->cameraId = g_value_get_int(value);
      break;
    case PROP_HEIGHT:
      pylonsrc->height = g_value_get_int(value);
      break;
    case PROP_WIDTH:
      pylonsrc->width = g_value_get_int(value);
      break;
    case PROP_OFFSETX:
      pylonsrc->offsetx = g_value_get_int(value);
      break;
    case PROP_OFFSETY:
      pylonsrc->offsety = g_value_get_int(value);
      break;
    case PROP_TESTIMAGE:
      pylonsrc->testImage = g_value_get_int(value);
      break;
    case PROP_SENSORREADOUTMODE:
      pylonsrc->sensorMode = g_value_dup_string(value+'\0');
      break;
    case PROP_LIGHTSOURCE:
      pylonsrc->lightsource = g_value_dup_string(value+'\0');
      break;
    case PROP_AUTOEXPOSURE:
      pylonsrc->autoexposure = g_value_dup_string(value+'\0');
      break;
    case PROP_AUTOWHITEBALANCE:
      pylonsrc->autowhitebalance = g_value_dup_string(value+'\0');
      break;
    case PROP_IMAGEFORMAT:
      pylonsrc->imageFormat = g_value_dup_string(value+'\0');
      break;
    case PROP_AUTOGAIN:
      pylonsrc->autogain = g_value_dup_string(value+'\0');
      break;
    case PROP_RESET:
      pylonsrc->reset = g_value_dup_string(value+'\0');
      break;
    case PROP_AUTOPROFILE:
      pylonsrc->autoprofile = g_value_dup_string(value+'\0');
      break;
    case PROP_TRANSFORMATIONSELECTOR:
      pylonsrc->transformationselector = g_value_dup_string(value+'\0');
      break;
    case PROP_USERID:
      pylonsrc->userid = g_value_dup_string(value+'\0');
      break;
    case PROP_BALANCERED:
      pylonsrc->balancered = g_value_get_double(value);
      break; 
    case PROP_BALANCEGREEN:
      pylonsrc->balancegreen = g_value_get_double(value);
      break;
    case PROP_BALANCEBLUE:
      pylonsrc->balanceblue = g_value_get_double(value);
      break;
    case PROP_COLORREDHUE:
      pylonsrc->redhue = g_value_get_double(value);
      break;
    case PROP_COLORREDSATURATION:
      pylonsrc->redsaturation = g_value_get_double(value);
      break;
    case PROP_COLORYELLOWHUE:
      pylonsrc->yellowhue = g_value_get_double(value);
      break;
    case PROP_COLORYELLOWSATURATION:
      pylonsrc->yellowsaturation = g_value_get_double(value);
      break;
    case PROP_COLORGREENHUE:
      pylonsrc->greenhue = g_value_get_double(value);
      break;
    case PROP_COLORGREENSATURATION:
      pylonsrc->greensaturation = g_value_get_double(value);
      break;
    case PROP_COLORCYANHUE:
      pylonsrc->cyanhue = g_value_get_double(value);
      break;
    case PROP_COLORCYANSATURATION:
      pylonsrc->cyansaturation = g_value_get_double(value);
      break;
    case PROP_COLORBLUEHUE:
      pylonsrc->bluehue = g_value_get_double(value);
      break;
    case PROP_COLORBLUESATURATION:
      pylonsrc->bluesaturation = g_value_get_double(value);
      break;
    case PROP_COLORMAGENTAHUE:
      pylonsrc->magentahue = g_value_get_double(value);
      break;
    case PROP_COLORMAGENTASATURATION:
      pylonsrc->magentasaturation = g_value_get_double(value);
      break;
    case PROP_MAXBANDWIDTH:
      pylonsrc->maxBandwidth = g_value_get_int64(value);      
      break;
    case PROP_FLIPX:
      pylonsrc->flipx = g_value_get_boolean(value);
      break;
    case PROP_FLIPY:
      pylonsrc->flipy = g_value_get_boolean(value);
      break;
    case PROP_CENTERX:
      pylonsrc->centerx = g_value_get_boolean(value);
      break;
    case PROP_CENTERY:
      pylonsrc->centery = g_value_get_boolean(value);
      break;
    case PROP_LIMITBANDWIDTH:
      pylonsrc->limitBandwidth = g_value_get_boolean(value);
      break;
    case PROP_ACQUISITIONFRAMERATEENABLE:
      pylonsrc->setFPS = g_value_get_boolean(value);
      break;
    case PROP_CONTINUOUSMODE:
      pylonsrc->continuousMode = g_value_get_boolean(value);
      break;
    case PROP_BASLERDEMOSAICING:
      pylonsrc->demosaicing = g_value_get_boolean(value);
      break;
    case PROP_FPS:
      pylonsrc->fps = g_value_get_double(value);
      break;
    case PROP_EXPOSURE:
      pylonsrc->exposure = g_value_get_double(value);
      break;
    case PROP_GAIN:
      pylonsrc->gain = g_value_get_double(value);
      break;
    case PROP_BLACKLEVEL:
      pylonsrc->blacklevel = g_value_get_double(value);
      break;
    case PROP_GAMMA:
      pylonsrc->gamma = g_value_get_double(value);
      break;
    case PROP_DEMOSAICINGNOISEREDUCTION:
      pylonsrc->noisereduction = g_value_get_double(value);
      break;
    case PROP_AUTOEXPOSUREUPPERLIMIT:
      pylonsrc->autoexposureupperlimit = g_value_get_double(value);
      break;
    case PROP_AUTOEXPOSURELOWERLIMIT:
      pylonsrc->autoexposurelowerlimit = g_value_get_double(value);
      break;
    case PROP_GAINLOWERLIMIT:
      pylonsrc->gainlowerlimit = g_value_get_double(value);
      break;
    case PROP_GAINUPPERLIMIT:
      pylonsrc->gainupperlimit = g_value_get_double(value);
      break;
    case PROP_AUTOBRIGHTNESSTARGET:
      pylonsrc->brightnesstarget = g_value_get_double(value);
      break;
    case PROP_DEMOSAICINGSHARPNESSENHANCEMENT:
      pylonsrc->sharpnessenhancement = g_value_get_double(value);
      break;
    case PROP_TRANSFORMATION00:
      pylonsrc->transformation00 = g_value_get_double(value);
      break;
    case PROP_TRANSFORMATION01:
      pylonsrc->transformation01 = g_value_get_double(value);
      break;
    case PROP_TRANSFORMATION02:
      pylonsrc->transformation02 = g_value_get_double(value);
      break;
    case PROP_TRANSFORMATION10:
      pylonsrc->transformation10 = g_value_get_double(value);
      break;
    case PROP_TRANSFORMATION11:
      pylonsrc->transformation11 = g_value_get_double(value);
      break;
    case PROP_TRANSFORMATION12:
      pylonsrc->transformation12 = g_value_get_double(value);
      break;
      case PROP_TRANSFORMATION20:
      pylonsrc->transformation20 = g_value_get_double(value);
      break;
    case PROP_TRANSFORMATION21:
      pylonsrc->transformation21 = g_value_get_double(value);
      break;
    case PROP_TRANSFORMATION22:
      pylonsrc->transformation22 = g_value_get_double(value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

void
gst_pylonsrc_get_property (GObject * object, guint property_id,
    GValue * value, GParamSpec * pspec)
{
  GstPylonsrc *pylonsrc = GST_PYLONSRC (object);

  GST_DEBUG_OBJECT (pylonsrc, "Getting a property.");

  switch (property_id) {
    case PROP_CAMERA:
      g_value_set_int(value, pylonsrc->cameraId);
      break;
    case PROP_HEIGHT:
      g_value_set_int(value, pylonsrc->height);
      break;
    case PROP_WIDTH:
      g_value_set_int(value, pylonsrc->width);
      break;
    case PROP_OFFSETX:
      g_value_set_int(value, pylonsrc->offsetx);
      break;
    case PROP_OFFSETY:
      g_value_set_int(value, pylonsrc->offsety);
      break;
    case PROP_TESTIMAGE:
      g_value_set_int(value, pylonsrc->testImage);
      break;
    case PROP_SENSORREADOUTMODE:
      g_value_set_string(value, pylonsrc->sensorMode);
      break;
    case PROP_LIGHTSOURCE:
      g_value_set_string(value, pylonsrc->lightsource);
      break;
    case PROP_AUTOEXPOSURE:
      g_value_set_string(value, pylonsrc->autoexposure);
      break;
    case PROP_AUTOWHITEBALANCE:
      g_value_set_string(value, pylonsrc->autowhitebalance);
      break;
    case PROP_IMAGEFORMAT:
      g_value_set_string(value, pylonsrc->imageFormat);
      break;
    case PROP_USERID:
      g_value_set_string(value, pylonsrc->userid);
      break;
    case PROP_AUTOGAIN:
      g_value_set_string(value, pylonsrc->autogain);
      break;
    case PROP_RESET:
      g_value_set_string(value, pylonsrc->reset);
      break;
    case PROP_AUTOPROFILE:
      g_value_set_string(value, pylonsrc->autoprofile);
    case PROP_TRANSFORMATIONSELECTOR:
      g_value_set_string(value, pylonsrc->transformationselector);
      break;
    case PROP_BALANCERED:
      g_value_set_double(value, pylonsrc->balancered);
      break;
    case PROP_BALANCEGREEN:
      g_value_set_double(value, pylonsrc->balancegreen);
      break;
    case PROP_BALANCEBLUE:
      g_value_set_double(value, pylonsrc->balanceblue);
      break;
    case PROP_COLORREDHUE:
      g_value_set_double(value, pylonsrc->redhue);
      break;
    case PROP_COLORREDSATURATION:
      g_value_set_double(value, pylonsrc->redsaturation);
      break;
    case PROP_COLORYELLOWHUE:
      g_value_set_double(value, pylonsrc->yellowhue);
      break;
    case PROP_COLORYELLOWSATURATION:
      g_value_set_double(value, pylonsrc->yellowsaturation);
      break;
    case PROP_COLORGREENHUE:
      g_value_set_double(value, pylonsrc->greenhue);
      break;
    case PROP_COLORGREENSATURATION:
      g_value_set_double(value, pylonsrc->greensaturation);
      break;
    case PROP_COLORCYANHUE:
      g_value_set_double(value, pylonsrc->cyanhue);
      break;
    case PROP_COLORCYANSATURATION:
      g_value_set_double(value, pylonsrc->cyansaturation);
      break;
    case PROP_COLORBLUEHUE:
      g_value_set_double(value, pylonsrc->bluehue);
      break;
    case PROP_COLORBLUESATURATION:
      g_value_set_double(value, pylonsrc->bluesaturation);
      break;
    case PROP_COLORMAGENTAHUE:
      g_value_set_double(value, pylonsrc->magentahue);
      break;
    case PROP_COLORMAGENTASATURATION:
      g_value_set_double(value, pylonsrc->magentasaturation);
      break;
    case PROP_MAXBANDWIDTH:      
      g_value_set_int64(value, pylonsrc->maxBandwidth);
      break;
    case PROP_FLIPX:
      g_value_set_boolean(value, pylonsrc->flipx);
      break;
    case PROP_FLIPY:
      g_value_set_boolean(value, pylonsrc->flipy);
      break;
    case PROP_CENTERX:
      g_value_set_boolean(value, pylonsrc->centerx);
      break;
    case PROP_CENTERY:
      g_value_set_boolean(value, pylonsrc->centery);
      break;
    case PROP_LIMITBANDWIDTH:
      g_value_set_boolean(value, pylonsrc->limitBandwidth);
      break;
    case PROP_ACQUISITIONFRAMERATEENABLE:
      g_value_set_boolean(value, pylonsrc->setFPS);
      break;
    case PROP_CONTINUOUSMODE:
      g_value_set_boolean(value, pylonsrc->continuousMode);
      break;
    case PROP_BASLERDEMOSAICING:
      g_value_set_boolean(value, pylonsrc->demosaicing);
      break;
    case PROP_FPS:
      g_value_set_double(value, pylonsrc->fps);
      break;
    case PROP_EXPOSURE:
      g_value_set_double(value, pylonsrc->exposure);
      break;
    case PROP_GAIN:
      g_value_set_double(value, pylonsrc->gain);
      break;
    case PROP_BLACKLEVEL:
      g_value_set_double(value, pylonsrc->blacklevel);
      break;
    case PROP_GAMMA:
      g_value_set_double(value, pylonsrc->gamma);
      break;
    case PROP_DEMOSAICINGNOISEREDUCTION:
      g_value_set_double(value, pylonsrc->noisereduction);
      break;
    case PROP_DEMOSAICINGSHARPNESSENHANCEMENT:
      g_value_set_double(value, pylonsrc->sharpnessenhancement);
      break;
    case PROP_AUTOEXPOSUREUPPERLIMIT:
      g_value_set_double(value, pylonsrc->sharpnessenhancement);
      break;
    case PROP_AUTOEXPOSURELOWERLIMIT:
      g_value_set_double(value, pylonsrc->sharpnessenhancement);
      break;
    case PROP_GAINLOWERLIMIT:
      g_value_set_double(value, pylonsrc->sharpnessenhancement);
      break;
    case PROP_GAINUPPERLIMIT:
      g_value_set_double(value, pylonsrc->sharpnessenhancement);
      break;
    case PROP_AUTOBRIGHTNESSTARGET:
      g_value_set_double(value, pylonsrc->sharpnessenhancement);
      break;
    case PROP_TRANSFORMATION00:
      g_value_set_double(value, pylonsrc->transformation00);
      break;
    case PROP_TRANSFORMATION01:
      g_value_set_double(value, pylonsrc->transformation01);
      break;
    case PROP_TRANSFORMATION02:
      g_value_set_double(value, pylonsrc->transformation02);
      break;
    case PROP_TRANSFORMATION10:
      g_value_set_double(value, pylonsrc->transformation10);
      break;
    case PROP_TRANSFORMATION11:
      g_value_set_double(value, pylonsrc->transformation11);
      break;
    case PROP_TRANSFORMATION12:
      g_value_set_double(value, pylonsrc->transformation12);
      break;
    case PROP_TRANSFORMATION20:
      g_value_set_double(value, pylonsrc->transformation20);
      break;
    case PROP_TRANSFORMATION21:
      g_value_set_double(value, pylonsrc->transformation21);
      break;
    case PROP_TRANSFORMATION22:
      g_value_set_double(value, pylonsrc->transformation22);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
      break;
  }
}

/* caps negotiation */
static GstCaps *
gst_pylonsrc_get_caps (GstBaseSrc * src, GstCaps * filter)
{
  GstPylonsrc *pylonsrc = GST_PYLONSRC (src);
  GST_DEBUG_OBJECT(pylonsrc, "Received a request for caps.");
  if(!deviceConnected) {
    GST_DEBUG_OBJECT(pylonsrc, "Could not send caps - no camera connected.");
    return gst_pad_get_pad_template_caps(GST_BASE_SRC_PAD(src));
  } else {
    // Set caps
    char *type = "";
    char *format = "";
    
    if(strncmp("bayer", pylonsrc->imageFormat, 5) == 0) {
      type = "video/x-bayer\0";
      format = "bggr\0";
      if(pylonsrc->flipx && !pylonsrc->flipy) {
        format = "gbrg\0";
      } else if (!pylonsrc->flipx && pylonsrc->flipy) {
        format = "grbg\0";
      } else if (pylonsrc->flipx && pylonsrc->flipy){
        format = "rggb\0";
      }
    } else {
      type = "video/x-raw\0";
      
      if(strcmp(pylonsrc->imageFormat, "rgb8") == 0) {
        format = "RGB\0";
      } else if (strcmp(pylonsrc->imageFormat, "bgr8") == 0){
        format = "BGR\0";
      } else if (strcmp(pylonsrc->imageFormat, "ycbcr422_8") == 0) {
        format = "YUY2\0";
      } else if (strcmp(pylonsrc->imageFormat, "mono8") == 0) {
        format = "GRAY8\0";
      }
    }

    GstCaps *caps = gst_caps_new_simple (type,
    "format", G_TYPE_STRING, format,
    "width", G_TYPE_INT, pylonsrc->width,
    "height", G_TYPE_INT, pylonsrc->height,
    "framerate", GST_TYPE_FRACTION_RANGE, 0, 1, G_MAXINT, 1, NULL);
    
    GST_DEBUG_OBJECT(pylonsrc, "The following caps were sent: %s, %s, %"PRId64"x%"PRId64", variable fps.", type, format, pylonsrc->width, pylonsrc->height);
    return caps;
  }
}

static gboolean
gst_pylonsrc_set_caps (GstBaseSrc * src, GstCaps * caps)
{
  GstPylonsrc *pylonsrc = GST_PYLONSRC (src);
  GstStructure *s = gst_caps_get_structure (caps, 0);

  GST_DEBUG_OBJECT (pylonsrc, "Setting caps to %" GST_PTR_FORMAT, caps);
  
  if(strncmp("bayer", pylonsrc->imageFormat, 5) == 0) {
    if (!g_str_equal ("video/x-bayer", gst_structure_get_name (s))) {
      goto unsupported_caps;
    }
  } else {
    if (!g_str_equal("video/x-raw", gst_structure_get_name (s)) || (!g_str_equal("YUY2", gst_structure_get_string(s, "format")) && !g_str_equal("RGB", gst_structure_get_string(s, "format")) && !g_str_equal("BGR", gst_structure_get_string(s, "format")) && !g_str_equal("GRAY8", gst_structure_get_string(s, "format")))){
      goto unsupported_caps; 
    }
  }
  return TRUE;

unsupported_caps:
  GST_ERROR_OBJECT (src, "Unsupported caps: %" GST_PTR_FORMAT, caps);
  return FALSE;
}

/* plugin's code */
static gboolean
gst_pylonsrc_start (GstBaseSrc * src)
{
  // Initialise PylonC
  GstPylonsrc *pylonsrc = GST_PYLONSRC (src);
  pylonc_initialize();
  GENAPIC_RESULT res;
  gint i;

  // Select a device
  size_t numDevices;
  res = PylonEnumerateDevices( &numDevices );
  PYLONC_CHECK_ERROR(pylonsrc, res);
  GST_DEBUG_OBJECT(pylonsrc, "pylonsrc: found %i Basler device(s).", (int)numDevices);  
  if(numDevices==0) {
    GST_ERROR_OBJECT(pylonsrc, "No devices connected, canceling initialisation.");
    GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("No camera connected"));
    goto error;  
  } else if (numDevices==1) {
    if (pylonsrc->cameraId!=9999) {
      GST_MESSAGE_OBJECT(pylonsrc, "Camera id was set, but was ignored as only one camera was found.");
    }
    pylonsrc->cameraId = 0;
  } else if (numDevices>1 && pylonsrc->cameraId==9999) {
    GST_MESSAGE_OBJECT(pylonsrc, "Multiple cameras found, and the user didn't specify which camera to use.");
    GST_MESSAGE_OBJECT(pylonsrc, "Please specify the camera using the CAMERA property.");
    GST_MESSAGE_OBJECT(pylonsrc, "The camera IDs are as follows: ");

    for(i=0; i<numDevices; i++) {
      PYLON_DEVICE_HANDLE deviceHandle;
      res = PylonCreateDeviceByIndex(i, &deviceHandle);

      if (res == GENAPI_E_OK) {
        res = PylonDeviceOpen(deviceHandle, PYLONC_ACCESS_MODE_CONTROL | PYLONC_ACCESS_MODE_STREAM); 
        PYLONC_CHECK_ERROR(pylonsrc,res);

        pylonc_print_camera_info(pylonsrc, deviceHandle, i);
      } else {
        GST_MESSAGE_OBJECT(pylonsrc, "ID:%i, Name: Unavailable, Serial No: Unavailable, Status: In use?", i);
      }

      PylonDeviceClose(deviceHandle);
      PylonDestroyDevice(deviceHandle);
    }

    GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("No camera selected"));
    goto error;
  } else if ( pylonsrc->cameraId != 9999 && pylonsrc->cameraId > numDevices) {
    GST_MESSAGE_OBJECT(pylonsrc, "No camera found with id %i.", pylonsrc->cameraId);    
    GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("No camera connected"));
    goto error;
  }

  // Connect to the camera 
  _Bool device = pylonc_connect_camera(pylonsrc);
  if(!device) {
    GST_ERROR_OBJECT(pylonsrc, "Couldn't initialise the camera");
    GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("No camera connected"));
    goto error;
  }

  if(strcmp(pylonsrc->userid, "") != 0) {
    if (PylonDeviceFeatureIsWritable(pylonsrc->deviceHandle, "DeviceUserID")) {
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "DeviceUserID", pylonsrc->userid);
      PYLONC_CHECK_ERROR(pylonsrc, res);
    }
  }

  // Print the name of the camera
  pylonc_print_camera_info(pylonsrc, pylonsrc->deviceHandle, pylonsrc->cameraId);

  // Reset the camera if required.
  pylonsrc->reset = g_ascii_strdown(pylonsrc->reset, -1);
  if(strcmp(pylonsrc->reset, "before") == 0) {
    if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "DeviceReset")) {
        pylonc_reset_camera(pylonsrc);
        pylonc_disconnect_camera(pylonsrc);
        pylonc_terminate();

        GST_MESSAGE_OBJECT(pylonsrc, "Camera reset. Waiting 6 seconds for it to fully reboot.");
        sleep(6);

        pylonc_initialize();
        res = PylonEnumerateDevices( &numDevices );
        PYLONC_CHECK_ERROR(pylonsrc, res);

        _Bool device = pylonc_connect_camera(pylonsrc);
        if(!device) {
          GST_ERROR_OBJECT(pylonsrc, "Couldn't initialise the camera. It looks like the reset failed. Please manually reconnect the camera and try again.");
          GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("No camera connected"));
          goto error;
        }
      } else {
        GST_ERROR_OBJECT(pylonsrc, "Couldn't reset the device - feature not supported. Cancelling startup.");
        GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("Camera couldn't be reset properly."));
        goto error;
      }
  }

  // Get the camera's resolution
  _Bool cameraReportsWidth = PylonDeviceFeatureIsImplemented(pylonsrc->deviceHandle, "Width");
  _Bool cameraReportsHeight = PylonDeviceFeatureIsImplemented(pylonsrc->deviceHandle, "Height");
  if(!cameraReportsWidth || !cameraReportsHeight) {
    GST_ERROR_OBJECT(pylonsrc, "The camera doesn't seem to be reporting it's resolution.");    
    GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("Camera isn't reporting it's resolution. (Unsupported device?)"));
    goto error;
  }

  // Default height/width
  int64_t width = 0, height = 0;
  res = PylonDeviceGetIntegerFeature(pylonsrc->deviceHandle, "Width", &width);
  PYLONC_CHECK_ERROR(pylonsrc, res);
  res = PylonDeviceGetIntegerFeature(pylonsrc->deviceHandle, "Height", &height);
  PYLONC_CHECK_ERROR(pylonsrc, res);
  
  // Max Width and Height.
  cameraReportsWidth = PylonDeviceFeatureIsImplemented(pylonsrc->deviceHandle, "WidthMax");
  cameraReportsHeight = PylonDeviceFeatureIsImplemented(pylonsrc->deviceHandle, "HeightMax");
  if(cameraReportsWidth && cameraReportsHeight) {      
    res = PylonDeviceGetIntegerFeature(pylonsrc->deviceHandle, "WidthMax", &pylonsrc->maxWidth);
    PYLONC_CHECK_ERROR(pylonsrc, res);
    res = PylonDeviceGetIntegerFeature(pylonsrc->deviceHandle, "HeightMax", &pylonsrc->maxHeight);
    PYLONC_CHECK_ERROR(pylonsrc, res);
  }
  GST_DEBUG_OBJECT(pylonsrc, "Max resolution is %"PRId64"x%"PRId64".", pylonsrc->maxWidth, pylonsrc->maxHeight);

  // If custom resolution is set, check if it's even possible and set it
  if(pylonsrc->height != 0 || pylonsrc->width != 0) {
    if(pylonsrc->width > pylonsrc->maxWidth) {
      GST_MESSAGE_OBJECT(pylonsrc, "Set width is above camera's capabilities.");
      GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("Wrong width specified"));
      goto error;
    } else if(pylonsrc->width == 0) {       
        pylonsrc->width = width;      
    }

    if(pylonsrc->height > pylonsrc->maxHeight) {
      GST_MESSAGE_OBJECT(pylonsrc, "Set height is above camera's capabilities.");
      GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("Wrong height specified"));
      goto error;
    } else if (pylonsrc->height == 0){       
      pylonsrc->height = height;
    }
  } else {
    pylonsrc->height = height;
    pylonsrc->width = width;
  }

  // Set the final resolution
  res = PylonDeviceSetIntegerFeature(pylonsrc->deviceHandle, "Width", pylonsrc->width);
  PYLONC_CHECK_ERROR(pylonsrc, res);
  res = PylonDeviceSetIntegerFeature(pylonsrc->deviceHandle, "Height", pylonsrc->height);
  PYLONC_CHECK_ERROR(pylonsrc, res);
  GST_MESSAGE_OBJECT(pylonsrc, "Setting resolution to %" PRId64 "x%" PRId64 ".", pylonsrc->width, pylonsrc->height);

  // Set the offset
  _Bool cameraReportsOffsetX = PylonDeviceFeatureIsImplemented(pylonsrc->deviceHandle, "OffsetX");
  _Bool cameraReportsOffsetY = PylonDeviceFeatureIsImplemented(pylonsrc->deviceHandle, "OffsetY");
  if(!cameraReportsOffsetX || !cameraReportsOffsetY) {
    GST_WARNING_OBJECT(pylonsrc, "The camera doesn't seem to allow setting offsets. Skipping...");
  } else {
    // Check if the user wants to center image first
    _Bool cameraSupportsCenterX = PylonDeviceFeatureIsImplemented(pylonsrc->deviceHandle, "CenterX");
    _Bool cameraSupportsCenterY = PylonDeviceFeatureIsImplemented(pylonsrc->deviceHandle, "CenterY");
    if(!cameraSupportsCenterX || !cameraSupportsCenterY) {
      GST_WARNING_OBJECT(pylonsrc, "The camera doesn't seem to allow offset centering. Skipping...");
    } else {
      res = PylonDeviceSetBooleanFeature(pylonsrc->deviceHandle, "CenterX", pylonsrc->centerx);
      PYLONC_CHECK_ERROR(pylonsrc, res);
      res = PylonDeviceSetBooleanFeature(pylonsrc->deviceHandle, "CenterY", pylonsrc->centery);
      PYLONC_CHECK_ERROR(pylonsrc, res);
      GST_DEBUG_OBJECT(pylonsrc, "Centering X: %s, Centering Y: %s.", pylonsrc->centerx ? "True" : "False", pylonsrc->centery ? "True" : "False");

      if(!pylonsrc->centerx && pylonsrc->offsetx != 99999) {
        int64_t maxoffsetx = pylonsrc->maxWidth - pylonsrc->width;

        if(maxoffsetx >= pylonsrc->offsetx) {
          res = PylonDeviceSetIntegerFeature(pylonsrc->deviceHandle, "OffsetX", pylonsrc->offsetx);
          PYLONC_CHECK_ERROR(pylonsrc, res);
          GST_DEBUG_OBJECT(pylonsrc, "Setting X offset to %"PRId64, pylonsrc->offsetx);
        } else {
          GST_MESSAGE_OBJECT(pylonsrc, "Set X offset is above camera's capabilities. (%"PRId64" > %"PRId64")", pylonsrc->offsetx, maxoffsetx);
          GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("Wrong offset for X axis specified"));
          goto error;
        }
      }

      if(!pylonsrc->centery && pylonsrc->offsety != 99999) {
        int64_t maxoffsety = pylonsrc->maxHeight - pylonsrc->height;
        if(maxoffsety >= pylonsrc->offsety) {
          res = PylonDeviceSetIntegerFeature(pylonsrc->deviceHandle, "OffsetY", pylonsrc->offsety);
          PYLONC_CHECK_ERROR(pylonsrc, res);
          GST_DEBUG_OBJECT(pylonsrc, "Setting Y offset to %"PRId64, pylonsrc->offsety);
        } else {
          GST_MESSAGE_OBJECT(pylonsrc, "Set Y offset is above camera's capabilities. (%"PRId64" > %"PRId64")", pylonsrc->offsety, maxoffsety);
          GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("Wrong offset for Y axis specified"));
          goto error;
        }
      }
    }
  }

  // Flip the image
  _Bool cameraAllowsReverseX = PylonDeviceFeatureIsImplemented(pylonsrc->deviceHandle, "ReverseX");
  _Bool cameraAllowsReverseY = PylonDeviceFeatureIsImplemented(pylonsrc->deviceHandle, "ReverseY");
  if(!cameraAllowsReverseX) {
    pylonsrc->flipx = FALSE;
    GST_WARNING_OBJECT(pylonsrc, "Camera doesn't support reversing the X axis. Skipping...");
  } else {
    if (!cameraAllowsReverseY) {
      pylonsrc->flipy = FALSE;
      GST_WARNING_OBJECT(pylonsrc, "Camera doesn't support reversing the Y axis. Skipping...");
    } else {
      res = PylonDeviceSetBooleanFeature(pylonsrc->deviceHandle, "ReverseX", pylonsrc->flipx);
      PYLONC_CHECK_ERROR(pylonsrc, res);
      res = PylonDeviceSetBooleanFeature(pylonsrc->deviceHandle, "ReverseY", pylonsrc->flipy);
      PYLONC_CHECK_ERROR(pylonsrc, res);
      GST_DEBUG_OBJECT(pylonsrc, "Flipping X: %s, Flipping Y: %s.", pylonsrc->flipx ? "True" : "False", pylonsrc->flipy ? "True" : "False");
    }
  }

  // Set pixel format.
  g_autoptr(GString) pixelFormat = g_string_new(NULL);
  pylonsrc->imageFormat = g_ascii_strdown(pylonsrc->imageFormat, -1);
  if(strncmp("bayer", pylonsrc->imageFormat, 5) == 0) {
    g_autoptr(GString) format = g_string_new(NULL);
    g_autoptr(GString) filter = g_string_new(NULL);
    
    if(!pylonsrc->flipx && !pylonsrc->flipy) {
      g_string_printf(filter, "BG");
    } else if(pylonsrc->flipx && !pylonsrc->flipy) {
      g_string_printf(filter, "GB");
    } else if(!pylonsrc->flipx && pylonsrc->flipy) {
      g_string_printf(filter, "GR");
    } else {
      g_string_printf(filter, "RG");
    }
    g_string_printf(pixelFormat, "Bayer%s%s", filter->str, &pylonsrc->imageFormat[5]);
    g_string_printf(format, "EnumEntry_PixelFormat_%s", pixelFormat->str);

    if(!PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, format->str)) {
      GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("Camera doesn't support Bayer%s.", &pylonsrc->imageFormat[5]));
      goto error;
    }
  } else if (strcmp(pylonsrc->imageFormat, "rgb8") == 0) {
    if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "EnumEntry_PixelFormat_RGB8")) {
      g_string_printf(pixelFormat, "RGB8");
    } else {
      GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("Camera doesn't support RGB 8"));
      goto error;
    } 
  } else if (strcmp(pylonsrc->imageFormat, "bgr8") == 0) {
    if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "EnumEntry_PixelFormat_BGR8")) {
      g_string_printf(pixelFormat, "RGB8");
    } else {
      GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("Camera doesn't support BGR 8"));
      goto error;
    } 
  } else if (strcmp(pylonsrc->imageFormat, "ycbcr422_8") == 0) {
    if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "EnumEntry_PixelFormat_YCbCr422_8")) {
      g_string_printf(pixelFormat, "YCbCr422_8");
    } else {
      GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("Camera doesn't support YCbCr422 8"));
      goto error;
    }
  } else if (strcmp(pylonsrc->imageFormat, "mono8") == 0) {
    if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "EnumEntry_PixelFormat_Mono8")) {
      g_string_printf(pixelFormat, "Mono8");
    } else {
      GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("Camera doesn't support Mono 8"));
      goto error;
    }
  } else {
    GST_ERROR_OBJECT(pylonsrc, "Invalid parameter value for imageformat. Available values are: bayer8, bayer10, bayer10p, rgb8, bgr8, ycbcr422_8, mono8. Value provided: \"%s\".", pylonsrc->imageFormat);
    GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("Invalid parameters provided"));
    goto error;
  }
  GST_MESSAGE_OBJECT(pylonsrc, "Using %s image format.", pixelFormat->str);
  res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "PixelFormat", pixelFormat->str);
  PYLONC_CHECK_ERROR(pylonsrc, res);

  // Output the size of a pixel
  if(PylonDeviceFeatureIsReadable(pylonsrc->deviceHandle, "PixelSize")) {
    char pixelSize[10];
    size_t siz = sizeof(pixelSize);

    res = PylonDeviceFeatureToString(pylonsrc->deviceHandle, "PixelSize", pixelSize, &siz);
    PYLONC_CHECK_ERROR(pylonsrc, res);
    GST_DEBUG_OBJECT(pylonsrc, "Pixel is %s bits large.", pixelSize + 3);
  } else {
    GST_WARNING_OBJECT(pylonsrc, "Couldn't read pixel size from the camera");
  }


  // Set whether test image will be shown
  if(PylonDeviceFeatureIsImplemented(pylonsrc->deviceHandle, "TestImageSelector")) {
      if(pylonsrc->testImage != 0) {
        GST_MESSAGE_OBJECT(pylonsrc, "Test image mode enabled.");
        char* ImageId = malloc(11);
        snprintf(ImageId, 11, "Testimage%"PRId64, pylonsrc->testImage);
        res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "TestImageSelector", ImageId);
        free(ImageId);
        PYLONC_CHECK_ERROR(pylonsrc, res);
      } else {
        res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "TestImageSelector", "Off");
        PYLONC_CHECK_ERROR(pylonsrc, res);
      }
  } else {
    GST_WARNING_OBJECT(pylonsrc, "The camera doesn't support test image mode.");
  }

  // Set sensor readout mode (default: Normal)
  if(PylonDeviceFeatureIsImplemented(pylonsrc->deviceHandle, "SensorReadoutMode")) {
    pylonsrc->sensorMode = g_ascii_strdown(pylonsrc->sensorMode, -1);

    if(strcmp(pylonsrc->sensorMode, "normal") == 0) {
      GST_DEBUG_OBJECT(pylonsrc, "Setting the sensor readout mode to normal.");
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "SensorReadoutMode", "Normal");
      PYLONC_CHECK_ERROR(pylonsrc, res);
    } else if (strcmp(pylonsrc->sensorMode, "fast") == 0) {
      GST_DEBUG_OBJECT(pylonsrc, "Setting the sensor readout mode to fast.");
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "SensorReadoutMode", "Fast");
      PYLONC_CHECK_ERROR(pylonsrc, res);
    } else {
      GST_ERROR_OBJECT(pylonsrc, "Invalid parameter value for sensorreadoutmode. Available values are normal/fast, while the value provided was \"%s\".", pylonsrc->sensorMode);
      GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("Invalid parameters provided"));
      goto error;
    }
  } else {
    GST_WARNING_OBJECT(pylonsrc, "Camera does not support changing the readout mode.");
  }

  // Set bandwidth limit mode (default: on)  
  if(PylonDeviceFeatureIsImplemented(pylonsrc->deviceHandle, "DeviceLinkThroughputLimitMode")) {
    if(pylonsrc->limitBandwidth) {
      GST_DEBUG_OBJECT(pylonsrc, "Limiting camera's bandwidth.");
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "DeviceLinkThroughputLimitMode", "On");
      PYLONC_CHECK_ERROR(pylonsrc, res);
    } else {
      GST_DEBUG_OBJECT(pylonsrc, "Unlocking camera's bandwidth.");
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "DeviceLinkThroughputLimitMode", "Off");
      PYLONC_CHECK_ERROR(pylonsrc, res);
    }
  } else {
    GST_WARNING_OBJECT(pylonsrc, "Camera does not support disabling the throughput limit.");
  }

  // Set bandwidth limit
  if(PylonDeviceFeatureIsImplemented(pylonsrc->deviceHandle, "DeviceLinkThroughputLimit")) {
    if(pylonsrc->maxBandwidth != 0) {
      if(!pylonsrc->limitBandwidth) {
        GST_DEBUG_OBJECT(pylonsrc, "Saving bandwidth limits, but because throughput mode is disabled they will be ignored.");
      }

      GST_DEBUG_OBJECT(pylonsrc, "Setting bandwidth limit to %"PRId64" B/s.", pylonsrc->maxBandwidth);
      res = PylonDeviceSetIntegerFeature(pylonsrc->deviceHandle, "DeviceLinkThroughputLimit", pylonsrc->maxBandwidth);
      PYLONC_CHECK_ERROR(pylonsrc, res);
    }
  } else {
    GST_WARNING_OBJECT(pylonsrc, "Camera does not support changing the throughput limit.");
  }

  // Set framerate
  if(pylonsrc->setFPS || (pylonsrc->fps != 0)) {    
    if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "AcquisitionFrameRateEnable")) {
      res = PylonDeviceSetBooleanFeature(pylonsrc->deviceHandle, "AcquisitionFrameRateEnable", TRUE);
      PYLONC_CHECK_ERROR(pylonsrc, res);

      if(pylonsrc->fps != 0 && PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "AcquisitionFrameRate")) {        
        GST_DEBUG_OBJECT(pylonsrc, "Capping framerate to %0.2lf.", pylonsrc->fps);
        res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "AcquisitionFrameRate", pylonsrc->fps);
        PYLONC_CHECK_ERROR(pylonsrc, res);
      } else {
        GST_DEBUG_OBJECT(pylonsrc, "Enabled custom framerate limiter. See below for current framerate.");
      }
    }
  } else {
    if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "AcquisitionFrameRateEnable")) {
      res = PylonDeviceSetBooleanFeature(pylonsrc->deviceHandle, "AcquisitionFrameRateEnable", FALSE);
      PYLONC_CHECK_ERROR(pylonsrc, res);
      GST_DEBUG_OBJECT(pylonsrc, "Disabled custom framerate limiter.");
    }
  }

  // Set lightsource preset
  if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "LightSourcePreset")) {
    pylonsrc->lightsource = g_ascii_strdown(pylonsrc->lightsource, -1);

    if(strcmp(pylonsrc->lightsource, "off") == 0) {
      GST_DEBUG_OBJECT(pylonsrc, "Not using a lightsource preset.");
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "LightSourcePreset", "Off");
      PYLONC_CHECK_ERROR(pylonsrc, res);
    } else if (strcmp(pylonsrc->lightsource, "2800k") == 0) {
      GST_DEBUG_OBJECT(pylonsrc, "Setting light preset to Tungsten 2800k (Incandescen light).");
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "LightSourcePreset", "Tungsten2800K");
      PYLONC_CHECK_ERROR(pylonsrc, res);
    } else if (strcmp(pylonsrc->lightsource, "5000k") == 0) {
      GST_DEBUG_OBJECT(pylonsrc, "Setting light preset to Daylight 5000k (Daylight).");      
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "LightSourcePreset", "Daylight5000K");
      PYLONC_CHECK_ERROR(pylonsrc, res);
    } else if (strcmp(pylonsrc->lightsource, "6500k") == 0) {
      GST_DEBUG_OBJECT(pylonsrc, "Setting light preset to Daylight 6500k (Very bright day).");
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "LightSourcePreset", "Daylight6500K");      
      PYLONC_CHECK_ERROR(pylonsrc, res);
    } else {
      GST_ERROR_OBJECT(pylonsrc, "Invalid parameter value for lightsource. Available values are off/2800k/5000k/6500k, while the value provided was \"%s\".", pylonsrc->lightsource);
      GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("Invalid parameters provided"));
      goto error;
    }
  } else {
    GST_WARNING_OBJECT(pylonsrc, "This camera doesn't have any lightsource presets");
  }

  // Enable/disable automatic exposure
  pylonsrc->autoexposure = g_ascii_strdown(pylonsrc->autoexposure, -1);
  if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "ExposureAuto")) {
    if(strcmp(pylonsrc->autoexposure, "off") == 0) {
      GST_DEBUG_OBJECT(pylonsrc, "Disabling automatic exposure.");
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ExposureAuto", "Off");
      PYLONC_CHECK_ERROR(pylonsrc, res);
    } else if (strcmp(pylonsrc->autoexposure, "once") == 0) {
      GST_DEBUG_OBJECT(pylonsrc, "Making the camera only calibrate exposure once.");
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ExposureAuto", "Once");
      PYLONC_CHECK_ERROR(pylonsrc, res);
    } else if (strcmp(pylonsrc->autoexposure, "continuous") == 0) {
      GST_DEBUG_OBJECT(pylonsrc, "Making the camera calibrate exposure automatically all the time.");
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ExposureAuto", "Continuous");
      PYLONC_CHECK_ERROR(pylonsrc, res);
    } else {
      GST_ERROR_OBJECT(pylonsrc, "Invalid parameter value for autoexposure. Available values are off/once/continuous, while the value provided was \"%s\".", pylonsrc->autoexposure);
      GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("Invalid parameters provided"));
      goto error;
    }
  } else {
    GST_WARNING_OBJECT(pylonsrc, "This camera doesn't support automatic exposure.");
  }

  // Enable/disable automatic gain
  pylonsrc->autogain = g_ascii_strdown(pylonsrc->autogain, -1);
  if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "GainAuto")) {
    if(strcmp(pylonsrc->autogain, "off") == 0) {
      GST_DEBUG_OBJECT(pylonsrc, "Disabling automatic gain.");
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "GainAuto", "Off");
      PYLONC_CHECK_ERROR(pylonsrc, res);
    } else if (strcmp(pylonsrc->autogain, "once") == 0) {
      GST_DEBUG_OBJECT(pylonsrc, "Making the camera only calibrate it's gain once.");
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "GainAuto", "Once");
      PYLONC_CHECK_ERROR(pylonsrc, res);
    } else if (strcmp(pylonsrc->autogain, "continuous") == 0) {
      GST_DEBUG_OBJECT(pylonsrc, "Making the camera calibrate gain settings automatically.");
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "GainAuto", "Continuous");
      PYLONC_CHECK_ERROR(pylonsrc, res);
    } else {
      GST_ERROR_OBJECT(pylonsrc, "Invalid parameter value for autogain. Available values are off/once/continuous, while the value provided was \"%s\".", pylonsrc->autogain);
      GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("Invalid parameters provided"));
      goto error;
    }
  } else {
    GST_WARNING_OBJECT(pylonsrc, "This camera doesn't support automatic gain.");
  }

  // Enable/disable automatic white balance
  pylonsrc->autowhitebalance = g_ascii_strdown(pylonsrc->autowhitebalance, -1);
  if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "BalanceWhiteAuto")) {
    if(strcmp(pylonsrc->autowhitebalance, "off") == 0) {
      GST_DEBUG_OBJECT(pylonsrc, "Disabling automatic white balance.");
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "BalanceWhiteAuto", "Off");
      PYLONC_CHECK_ERROR(pylonsrc, res);
    } else if (strcmp(pylonsrc->autowhitebalance, "once") == 0) {
      GST_DEBUG_OBJECT(pylonsrc, "Making the camera only calibrate it's colour balance once.");
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "BalanceWhiteAuto", "Once");
      PYLONC_CHECK_ERROR(pylonsrc, res);
    } else if (strcmp(pylonsrc->autowhitebalance, "continuous") == 0) {
      GST_DEBUG_OBJECT(pylonsrc, "Making the camera calibrate white balance settings automatically.");
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "BalanceWhiteAuto", "Continuous");
      PYLONC_CHECK_ERROR(pylonsrc, res);
    } else {
      GST_ERROR_OBJECT(pylonsrc, "Invalid parameter value for autowhitebalance. Available values are off/once/continuous, while the value provided was \"%s\".", pylonsrc->autowhitebalance);
      GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("Invalid parameters provided"));
      goto error;
    }
  } else {
    GST_WARNING_OBJECT(pylonsrc, "This camera doesn't support automatic white balance.");
  }

  // Configure automatic exposure and gain settings
  if(pylonsrc->autoexposureupperlimit != 9999999.0) {
    if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "AutoExposureTimeUpperLimit")) {
      res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "AutoExposureTimeUpperLimit", pylonsrc->autoexposureupperlimit);
      PYLONC_CHECK_ERROR(pylonsrc, res);
    } else {
      GST_WARNING_OBJECT(pylonsrc, "This camera doesn't support changing the auto exposure limits.");
    }
  }
  if(pylonsrc->autoexposurelowerlimit != 9999999.0) {
    if(pylonsrc->autoexposurelowerlimit >= pylonsrc->autoexposureupperlimit) {
      GST_ERROR_OBJECT(pylonsrc, "Invalid parameter value for autoexposurelowerlimit. It seems like you're trying to set a lower limit (%.2f) that's higher than the upper limit (%.2f).", pylonsrc->autoexposurelowerlimit, pylonsrc->autoexposureupperlimit);
      GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("Invalid parameters provided"));
      goto error;
    }
    
    if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "AutoExposureTimeLowerLimit")) {
      res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "AutoExposureTimeLowerLimit", pylonsrc->autoexposurelowerlimit);
      PYLONC_CHECK_ERROR(pylonsrc, res);
    } else {
      GST_WARNING_OBJECT(pylonsrc, "This camera doesn't support changing the auto exposure limits.");
    }
  }
  if(pylonsrc->gainlowerlimit != 999.0) {
    if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "AutoGainLowerLimit")) {
      res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "AutoGainLowerLimit", pylonsrc->gainlowerlimit);
      PYLONC_CHECK_ERROR(pylonsrc, res);
    } else {
      GST_WARNING_OBJECT(pylonsrc, "This camera doesn't support changing the auto gain limits.");
    }
  }
  if(pylonsrc->gainupperlimit != 999.0) {
    if(pylonsrc->gainlowerlimit >= pylonsrc->gainupperlimit) {
      GST_ERROR_OBJECT(pylonsrc, "Invalid parameter value for gainupperlimit. It seems like you're trying to set a lower limit (%.5f) that's higher than the upper limit (%.5f).", pylonsrc->gainlowerlimit, pylonsrc->gainupperlimit);
      GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("Invalid parameters provided"));
      goto error;
    }

    if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "AutoGainUpperLimit")) {
      res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "AutoGainUpperLimit", pylonsrc->gainupperlimit);
      PYLONC_CHECK_ERROR(pylonsrc, res);
    } else {
      GST_WARNING_OBJECT(pylonsrc, "This camera doesn't support changing the auto gain limits.");
    }
  }
  if(pylonsrc->brightnesstarget != 999.0) {
    if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "AutoTargetBrightness")) {
      res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "AutoTargetBrightness", pylonsrc->brightnesstarget);
      PYLONC_CHECK_ERROR(pylonsrc, res);
    } else {
      GST_WARNING_OBJECT(pylonsrc, "This camera doesn't support changing the brightness target.");
    }
  }
  pylonsrc->autoprofile = g_ascii_strdown(pylonsrc->autoprofile, -1);
  if(strcmp(pylonsrc->autoprofile, "default") != 0) {
    GST_DEBUG_OBJECT(pylonsrc, "Setting automatic profile to minimise %s.", pylonsrc->autoprofile);
    if(strcmp(pylonsrc->autoprofile, "gain") == 0) {
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "AutoFunctionProfile", "MinimizeGain");
      PYLONC_CHECK_ERROR(pylonsrc, res);
    } else if(strcmp(pylonsrc->autoprofile, "exposure") == 0) {
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "AutoFunctionProfile", "MinimizeExposureTime");
      PYLONC_CHECK_ERROR(pylonsrc, res);
    } else {
      GST_ERROR_OBJECT(pylonsrc, "Invalid parameter value for autoprofile. Available values are gain/exposure, while the value provided was \"%s\".", pylonsrc->autoprofile);
      GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("Invalid parameters provided"));
      goto error;
    }
  } else {
    GST_DEBUG_OBJECT(pylonsrc, "Using the auto profile currently saved on the device.");
  }

  // Configure colour balance
  if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "BalanceRatio")) {
    if(strcmp(pylonsrc->autowhitebalance, "off") == 0) {
      if (pylonsrc->balancered != 999.0) {
        res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "BalanceRatioSelector", "Red");
        PYLONC_CHECK_ERROR(pylonsrc, res);
        res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "BalanceRatio", pylonsrc->balancered);
        PYLONC_CHECK_ERROR(pylonsrc, res);

        GST_DEBUG_OBJECT(pylonsrc, "Red balance set to %.2lf", pylonsrc->balancered);
      } else {
        GST_DEBUG_OBJECT(pylonsrc, "Using current settings for the colour red.");
      }

      if (pylonsrc->balancegreen != 999.0) {
        res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "BalanceRatioSelector", "Green");
        PYLONC_CHECK_ERROR(pylonsrc, res);
        res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "BalanceRatio", pylonsrc->balancegreen);
        PYLONC_CHECK_ERROR(pylonsrc, res);

        GST_DEBUG_OBJECT(pylonsrc, "Green balance set to %.2lf", pylonsrc->balancegreen);
      } else {
        GST_DEBUG_OBJECT(pylonsrc, "Using current settings for the colour green.");
      }

      if (pylonsrc->balanceblue != 999.0) {
        res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "BalanceRatioSelector", "Blue");
        PYLONC_CHECK_ERROR(pylonsrc, res);
        res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "BalanceRatio", pylonsrc->balanceblue);
        PYLONC_CHECK_ERROR(pylonsrc, res);

        GST_DEBUG_OBJECT(pylonsrc, "Blue balance set to %.2lf", pylonsrc->balanceblue);
      } else {
        GST_DEBUG_OBJECT(pylonsrc, "Using current settings for the colour blue.");
      }
    } else {
      GST_DEBUG_OBJECT(pylonsrc, "Auto White Balance is enabled. Not setting Balance Ratio.");
    }
  }

  // Configure colour adjustment
  if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "ColorAdjustmentSelector")) {
    if (pylonsrc->redhue != 999.0) {
        res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ColorAdjustmentSelector", "Red");
        PYLONC_CHECK_ERROR(pylonsrc, res);        
        res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "ColorAdjustmentHue", pylonsrc->redhue);
        PYLONC_CHECK_ERROR(pylonsrc, res);

        GST_DEBUG_OBJECT(pylonsrc, "Red hue set to %.2lf", pylonsrc->redhue);
    } else {
        GST_DEBUG_OBJECT(pylonsrc, "Using saved colour red's hue.");
    }
    if (pylonsrc->redsaturation != 999.0) {      
        res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ColorAdjustmentSelector", "Red");
        PYLONC_CHECK_ERROR(pylonsrc, res);        
        res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "ColorAdjustmentSaturation", pylonsrc->redsaturation);
        PYLONC_CHECK_ERROR(pylonsrc, res);

        GST_DEBUG_OBJECT(pylonsrc, "Red saturation set to %.2lf", pylonsrc->redsaturation);
    } else {
        GST_DEBUG_OBJECT(pylonsrc, "Using saved colour red's saturation.");
    }

    if (pylonsrc->yellowhue != 999.0) {
        res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ColorAdjustmentSelector", "Yellow");
        PYLONC_CHECK_ERROR(pylonsrc, res);        
        res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "ColorAdjustmentHue", pylonsrc->yellowhue);
        PYLONC_CHECK_ERROR(pylonsrc, res);

        GST_DEBUG_OBJECT(pylonsrc, "Yellow hue set to %.2lf", pylonsrc->yellowhue);
    } else {
        GST_DEBUG_OBJECT(pylonsrc, "Using saved colour yellow's hue.");
    }
    if (pylonsrc->yellowsaturation != 999.0) {      
        res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ColorAdjustmentSelector", "Yellow");
        PYLONC_CHECK_ERROR(pylonsrc, res);        
        res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "ColorAdjustmentSaturation", pylonsrc->yellowsaturation);
        PYLONC_CHECK_ERROR(pylonsrc, res);

        GST_DEBUG_OBJECT(pylonsrc, "Yellow saturation set to %.2lf", pylonsrc->yellowsaturation);
    } else {
        GST_DEBUG_OBJECT(pylonsrc, "Using saved colour yellow's saturation.");
    }

    if (pylonsrc->greenhue != 999.0) {
        res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ColorAdjustmentSelector", "Green");
        PYLONC_CHECK_ERROR(pylonsrc, res);        
        res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "ColorAdjustmentHue", pylonsrc->greenhue);
        PYLONC_CHECK_ERROR(pylonsrc, res);

        GST_DEBUG_OBJECT(pylonsrc, "Green hue set to %.2lf", pylonsrc->greenhue);
    } else {
        GST_DEBUG_OBJECT(pylonsrc, "Using saved colour green's hue.");
    }
    if (pylonsrc->greensaturation != 999.0) {      
        res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ColorAdjustmentSelector", "Green");
        PYLONC_CHECK_ERROR(pylonsrc, res);        
        res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "ColorAdjustmentSaturation", pylonsrc->greensaturation);
        PYLONC_CHECK_ERROR(pylonsrc, res);

        GST_DEBUG_OBJECT(pylonsrc, "Green saturation set to %.2lf", pylonsrc->greensaturation);
    } else {
        GST_DEBUG_OBJECT(pylonsrc, "Using saved colour green's saturation.");
    }

    if (pylonsrc->cyanhue != 999.0) {
        res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ColorAdjustmentSelector", "Cyan");
        PYLONC_CHECK_ERROR(pylonsrc, res);        
        res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "ColorAdjustmentHue", pylonsrc->cyanhue);
        PYLONC_CHECK_ERROR(pylonsrc, res);

        GST_DEBUG_OBJECT(pylonsrc, "Cyan hue set to %.2lf", pylonsrc->cyanhue);
    } else {
        GST_DEBUG_OBJECT(pylonsrc, "Using saved colour cyan's hue.");
    }
    if (pylonsrc->cyansaturation != 999.0) {      
        res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ColorAdjustmentSelector", "Cyan");
        PYLONC_CHECK_ERROR(pylonsrc, res);        
        res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "ColorAdjustmentSaturation", pylonsrc->cyansaturation);
        PYLONC_CHECK_ERROR(pylonsrc, res);

        GST_DEBUG_OBJECT(pylonsrc, "Cyan saturation set to %.2lf", pylonsrc->cyansaturation);
    } else {
        GST_DEBUG_OBJECT(pylonsrc, "Using saved colour cyan's saturation.");
    }

    if (pylonsrc->bluehue != 999.0) {
        res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ColorAdjustmentSelector", "Blue");
        PYLONC_CHECK_ERROR(pylonsrc, res);        
        res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "ColorAdjustmentHue", pylonsrc->bluehue);
        PYLONC_CHECK_ERROR(pylonsrc, res);

        GST_DEBUG_OBJECT(pylonsrc, "Blue hue set to %.2lf", pylonsrc->bluehue);
    } else {
        GST_DEBUG_OBJECT(pylonsrc, "Using saved colour blue's hue.");
    }
    if (pylonsrc->bluesaturation != 999.0) {      
        res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ColorAdjustmentSelector", "Blue");
        PYLONC_CHECK_ERROR(pylonsrc, res);        
        res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "ColorAdjustmentSaturation", pylonsrc->bluesaturation);
        PYLONC_CHECK_ERROR(pylonsrc, res);

        GST_DEBUG_OBJECT(pylonsrc, "Blue saturation set to %.2lf", pylonsrc->bluesaturation);
    } else {
        GST_DEBUG_OBJECT(pylonsrc, "Using saved colour blue's saturation.");
    }

    if (pylonsrc->magentahue != 999.0) {
        res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ColorAdjustmentSelector", "Magenta");
        PYLONC_CHECK_ERROR(pylonsrc, res);        
        res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "ColorAdjustmentHue", pylonsrc->magentahue);
        PYLONC_CHECK_ERROR(pylonsrc, res);

        GST_DEBUG_OBJECT(pylonsrc, "Magenta hue set to %.2lf", pylonsrc->magentahue);
    } else {
        GST_DEBUG_OBJECT(pylonsrc, "Using saved colour magenta's hue.");
    }
    if (pylonsrc->magentasaturation != 999.0) {      
        res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ColorAdjustmentSelector", "Magenta");
        PYLONC_CHECK_ERROR(pylonsrc, res);        
        res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "ColorAdjustmentSaturation", pylonsrc->magentasaturation);
        PYLONC_CHECK_ERROR(pylonsrc, res);

        GST_DEBUG_OBJECT(pylonsrc, "Magenta saturation set to %.2lf", pylonsrc->magentasaturation);
    } else {
        GST_DEBUG_OBJECT(pylonsrc, "Using saved colour magenta's saturation.");
    }
  } else {
    GST_DEBUG_OBJECT(pylonsrc, "This camera doesn't support adjusting colours. Skipping...");
  }

  // Configure colour transformation
  pylonsrc->transformationselector = g_ascii_strdown(pylonsrc->transformationselector, -1);
  if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "ColorTransformationSelector")) {
    if(strcmp(pylonsrc->transformationselector, "default") != 0) {
      if(strcmp(pylonsrc->transformationselector, "rgbrgb") == 0) {
        res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ColorTransformationSelector", "RGBtoRGB");
        PYLONC_CHECK_ERROR(pylonsrc, res);
      } else if(strcmp(pylonsrc->transformationselector, "rgbyuv") == 0) {
        res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ColorTransformationSelector", "RGBtoYUV");
        PYLONC_CHECK_ERROR(pylonsrc, res);
      } else if(strcmp(pylonsrc->transformationselector, "rgbyuv") == 0) {
        res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ColorTransformationSelector", "YUVtoRGB");
        PYLONC_CHECK_ERROR(pylonsrc, res);
      } else {
        GST_ERROR_OBJECT(pylonsrc, "Invalid parameter value for transformationselector. Available values are: RGBtoRGB, RGBtoYUV, YUVtoRGB. Value provided: \"%s\".", pylonsrc->transformationselector);
        GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Failed to initialise the camera"), ("Invalid parameters provided"));
        goto error;
      }
    }

    if(pylonsrc->transformation00 != 999.0) {
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ColorTransformationSelector", "Gain00");
      PYLONC_CHECK_ERROR(pylonsrc, res);        
      res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "ColorTransformationValueSelector", pylonsrc->transformation00);
      PYLONC_CHECK_ERROR(pylonsrc, res);

      GST_DEBUG_OBJECT(pylonsrc, "Gain00 set to %.2lf", pylonsrc->transformation00);
    } else {
      GST_DEBUG_OBJECT(pylonsrc, "Using saved Gain00 transformation value.");
    }

    if(pylonsrc->transformation01 != 999.0) {
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ColorTransformationValueSelector", "Gain01");
      PYLONC_CHECK_ERROR(pylonsrc, res);        
      res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "ColorTransformationValue", pylonsrc->transformation01);
      PYLONC_CHECK_ERROR(pylonsrc, res);

      GST_DEBUG_OBJECT(pylonsrc, "Gain01 set to %.2lf", pylonsrc->transformation01);
    } else {
      GST_DEBUG_OBJECT(pylonsrc, "Using saved Gain01 transformation value.");
    }

    if(pylonsrc->transformation02 != 999.0) {
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ColorTransformationValueSelector", "Gain02");
      PYLONC_CHECK_ERROR(pylonsrc, res);        
      res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "ColorTransformationValue", pylonsrc->transformation02);
      PYLONC_CHECK_ERROR(pylonsrc, res);

      GST_DEBUG_OBJECT(pylonsrc, "Gain02 set to %.2lf", pylonsrc->transformation02);
    } else {
      GST_DEBUG_OBJECT(pylonsrc, "Using saved Gain02 transformation value.");
    }

    if(pylonsrc->transformation10 != 999.0) {
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ColorTransformationValueSelector", "Gain10");
      PYLONC_CHECK_ERROR(pylonsrc, res);        
      res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "ColorTransformationValue", pylonsrc->transformation10);
      PYLONC_CHECK_ERROR(pylonsrc, res);

      GST_DEBUG_OBJECT(pylonsrc, "Gain10 set to %.2lf", pylonsrc->transformation10);
    } else {
      GST_DEBUG_OBJECT(pylonsrc, "Using saved Gain10 transformation value.");
    }

    if(pylonsrc->transformation11 != 999.0) {
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ColorTransformationValueSelector", "Gain11");
      PYLONC_CHECK_ERROR(pylonsrc, res);        
      res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "ColorTransformationValue", pylonsrc->transformation11);
      PYLONC_CHECK_ERROR(pylonsrc, res);

      GST_DEBUG_OBJECT(pylonsrc, "Gain11 set to %.2lf", pylonsrc->transformation11);
    } else {
      GST_DEBUG_OBJECT(pylonsrc, "Using saved Gain11 transformation value.");
    }

    if(pylonsrc->transformation12 != 999.0) {
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ColorTransformationValueSelector", "Gain12");
      PYLONC_CHECK_ERROR(pylonsrc, res);        
      res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "ColorTransformationValue", pylonsrc->transformation12);
      PYLONC_CHECK_ERROR(pylonsrc, res);

      GST_DEBUG_OBJECT(pylonsrc, "Gain12 set to %.2lf", pylonsrc->transformation12);
    } else {
      GST_DEBUG_OBJECT(pylonsrc, "Using saved Gain12 transformation value.");
    }

    if(pylonsrc->transformation20 != 999.0) {
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ColorTransformationValueSelector", "Gain20");
      PYLONC_CHECK_ERROR(pylonsrc, res);        
      res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "ColorTransformationValue", pylonsrc->transformation20);
      PYLONC_CHECK_ERROR(pylonsrc, res);

      GST_DEBUG_OBJECT(pylonsrc, "Gain20 set to %.2lf", pylonsrc->transformation20);
    } else {
      GST_DEBUG_OBJECT(pylonsrc, "Using saved Gain20 transformation value.");
    }

    if(pylonsrc->transformation21 != 999.0) {
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ColorTransformationValueSelector", "Gain21");
      PYLONC_CHECK_ERROR(pylonsrc, res);        
      res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "ColorTransformationValue", pylonsrc->transformation21);
      PYLONC_CHECK_ERROR(pylonsrc, res);

      GST_DEBUG_OBJECT(pylonsrc, "Gain21 set to %.2lf", pylonsrc->transformation21);
    } else {
      GST_DEBUG_OBJECT(pylonsrc, "Using saved Gain21 transformation value.");
    }

    if(pylonsrc->transformation22 != 999.0) {
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "ColorTransformationValueSelector", "Gain22");
      PYLONC_CHECK_ERROR(pylonsrc, res);        
      res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "ColorTransformationValue", pylonsrc->transformation22);
      PYLONC_CHECK_ERROR(pylonsrc, res);

      GST_DEBUG_OBJECT(pylonsrc, "Gain22 set to %.2lf", pylonsrc->transformation22);
    } else {
      GST_DEBUG_OBJECT(pylonsrc, "Using saved Gain22 transformation value.");
    }
  } else {
    GST_DEBUG_OBJECT(pylonsrc, "This camera doesn't support transforming colours. Skipping...");
  }

  // Configure exposure
  if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "ExposureTime")) {
    if(strcmp(pylonsrc->autoexposure, "off") == 0) {
      if(pylonsrc->exposure != 0.0) {
        GST_DEBUG_OBJECT(pylonsrc, "Setting exposure to %0.2lf", pylonsrc->exposure);
        res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "ExposureTime", pylonsrc->exposure);
        PYLONC_CHECK_ERROR(pylonsrc, res);
      } else {
        GST_DEBUG_OBJECT(pylonsrc, "Exposure property not set, using the saved exposure setting.");
      }
    } else {
      GST_WARNING_OBJECT(pylonsrc, "Automatic exposure has been enabled, skipping setting manual exposure times.");
    }
  } else {
    GST_WARNING_OBJECT(pylonsrc, "This camera doesn't support setting manual exposure.");
  }

  // Configure gain
  if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "Gain")) {
    if(strcmp(pylonsrc->autogain, "off") == 0) {
      GST_DEBUG_OBJECT(pylonsrc, "Setting gain to %0.2lf", pylonsrc->gain);
      res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "Gain", pylonsrc->gain);
      PYLONC_CHECK_ERROR(pylonsrc, res);
    } else {
      GST_WARNING_OBJECT(pylonsrc, "Automatic gain has been enabled, skipping setting gain.");
    }
  } else {
    GST_WARNING_OBJECT(pylonsrc, "This camera doesn't support setting manual gain.");
  }

  // Configure black level
  if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "BlackLevel")) {    
    GST_DEBUG_OBJECT(pylonsrc, "Setting black level to %0.2lf", pylonsrc->blacklevel);
    res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "BlackLevel", pylonsrc->blacklevel);
    PYLONC_CHECK_ERROR(pylonsrc, res);
  } else {
    GST_WARNING_OBJECT(pylonsrc, "This camera doesn't support setting black level.");
  }

  // Configure gamma correction
  if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "Gamma")) {    
    GST_DEBUG_OBJECT(pylonsrc, "Setting gamma to %0.2lf", pylonsrc->gamma);
    res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "Gamma", pylonsrc->gamma);
    PYLONC_CHECK_ERROR(pylonsrc, res);
  } else {
    GST_WARNING_OBJECT(pylonsrc, "This camera doesn't support setting gamma values.");
  }

  // Basler PGI
  if(PylonDeviceFeatureIsImplemented(pylonsrc->deviceHandle, "DemosaicingMode")) {
    if(pylonsrc->demosaicing || pylonsrc->sharpnessenhancement != 999.0 || pylonsrc->noisereduction != 999.0) {
      if(strncmp("bayer", pylonsrc->imageFormat, 5) != 0) {
        GST_DEBUG_OBJECT(pylonsrc, "Enabling Basler's PGI.");
        res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "DemosaicingMode", "BaslerPGI");
        PYLONC_CHECK_ERROR(pylonsrc, res);

        // PGI Modules (Noise reduction and Sharpness enhancement).
        if(pylonsrc->noisereduction != 999.0) {
          if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "NoiseReduction")) {  
            GST_DEBUG_OBJECT(pylonsrc, "Setting PGI noise reduction to %0.2lf", pylonsrc->noisereduction);
            res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "NoiseReduction", pylonsrc->noisereduction);
          } else {
            GST_ERROR_OBJECT(pylonsrc, "This camera doesn't support noise reduction.");
          }
        } else {
          GST_DEBUG_OBJECT(pylonsrc, "Using the stored value for noise reduction.");
        }
        if(pylonsrc->sharpnessenhancement != 999.0) {
          if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "SharpnessEnhancement")) {    
            GST_DEBUG_OBJECT(pylonsrc, "Setting PGI sharpness enhancement to %0.2lf", pylonsrc->sharpnessenhancement);
            res = PylonDeviceSetFloatFeature(pylonsrc->deviceHandle, "SharpnessEnhancement", pylonsrc->sharpnessenhancement);
          } else {
            GST_ERROR_OBJECT(pylonsrc, "This camera doesn't support sharpness enhancement.");
          }
        } else {
          GST_DEBUG_OBJECT(pylonsrc, "Using the stored value for noise reduction.");
        }
      } else {
        res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "DemosaicingMode", "Simple");
        PYLONC_CHECK_ERROR(pylonsrc, res);
      }
    } else {
      GST_DEBUG_OBJECT(pylonsrc, "Usage of PGI is not permitted with bayer output. Skipping.");
    }
  } else {
    GST_DEBUG_OBJECT(pylonsrc, "Basler's PGI is not supported. Skipping.");
  }

  // Set camera trigger mode
  GST_DEBUG_OBJECT(pylonsrc, "Setting trigger mode.");
  const char* triggerSelectorValue = "FrameStart";      
  _Bool isAvailAcquisitionStart = PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "EnumEntry_TriggerSelector_AcquisitionStart");
  _Bool isAvailFrameStart = PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "EnumEntry_TriggerSelector_FrameStart");
  const char* triggerMode = (pylonsrc->continuousMode) ? "Off" : "On";

  // Check to see if the camera implements the acquisition start trigger mode only
  if (isAvailAcquisitionStart && !isAvailFrameStart) {
    // Select the software trigger as the trigger source
    res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "TriggerSelector", "AcquisitionStart");
    PYLONC_CHECK_ERROR(pylonsrc, res);
    res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "TriggerMode", triggerMode);
    PYLONC_CHECK_ERROR(pylonsrc, res);
    triggerSelectorValue = "AcquisitionStart";
  }
  else
  {
    // Camera may have the acquisition start trigger mode and the frame start trigger mode implemented.
    // In this case, the acquisition trigger mode must be switched off.
    if (isAvailAcquisitionStart) {
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "TriggerSelector", "AcquisitionStart");
      PYLONC_CHECK_ERROR(pylonsrc, res);
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "TriggerMode", "Off");
      PYLONC_CHECK_ERROR(pylonsrc, res);
    }
    // Disable frame burst start trigger if available
    if (PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "EnumEntry_TriggerSelector_FrameBurstStart")) {
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "TriggerSelector", "FrameBurstStart");
      PYLONC_CHECK_ERROR(pylonsrc, res);
      res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "TriggerMode", "Off");
      PYLONC_CHECK_ERROR(pylonsrc, res);
    }
    // To trigger each single frame by software or external hardware trigger: Enable the frame start trigger mode
    res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "TriggerSelector", "FrameStart");
    PYLONC_CHECK_ERROR(pylonsrc, res);
    res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "TriggerMode", triggerMode);
    PYLONC_CHECK_ERROR(pylonsrc, res);
  }

  if(!pylonsrc->continuousMode) {
    // Set the acquisiton selector to FrameTrigger in case it was changed by something else before launching the plugin so we don't request frames when they're still capturing or something.
    res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "AcquisitionStatusSelector", "FrameTriggerWait"); 
    PYLONC_CHECK_ERROR(pylonsrc, res);
  }
  GST_DEBUG_OBJECT(pylonsrc, "Using \"%s\" trigger selector. Software trigger mode is %s.", triggerSelectorValue, triggerMode);
  res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "TriggerSelector", triggerSelectorValue);
  PYLONC_CHECK_ERROR(pylonsrc, res);
  res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "TriggerSource", "Software");
  PYLONC_CHECK_ERROR(pylonsrc, res);
  res = PylonDeviceFeatureFromString(pylonsrc->deviceHandle, "AcquisitionMode", "Continuous" );
  PYLONC_CHECK_ERROR(pylonsrc, res);

  // Create a stream grabber
  size_t streams;
  res = PylonDeviceGetNumStreamGrabberChannels(pylonsrc->deviceHandle, &streams);
  PYLONC_CHECK_ERROR(pylonsrc, res);
  if (streams < 1) {
    GST_ERROR_OBJECT(pylonsrc, "The transport layer doesn't support image streams.");
    GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Transport layer error"), ("The system does not support image streams."));
    goto error;
  }

  // Open the stream grabber for the first channel
  res = PylonDeviceGetStreamGrabber (pylonsrc->deviceHandle, 0, &pylonsrc->streamGrabber);
  PYLONC_CHECK_ERROR(pylonsrc, res);
  res = PylonStreamGrabberOpen(pylonsrc->streamGrabber);
  PYLONC_CHECK_ERROR(pylonsrc, res);

  // Get the wait object
  res = PylonStreamGrabberGetWaitObject(pylonsrc->streamGrabber, &pylonsrc->waitObject);
  PYLONC_CHECK_ERROR(pylonsrc, res);

  // Get the size of each frame
  res = PylonDeviceGetIntegerFeatureInt32(pylonsrc->deviceHandle, "PayloadSize", &pylonsrc->payloadSize);
  PYLONC_CHECK_ERROR(pylonsrc, res);

  // Allocate the memory for the frame payloads
  for(i = 0; i < NUM_BUFFERS; ++i) {
    buffers[i] = (unsigned char*) malloc(pylonsrc->payloadSize);
    if (NULL == buffers[i]) {
      GST_ERROR_OBJECT(pylonsrc, "Memory allocation error.");
      GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("Memory allocation error"), ("Couldn't allocate memory."));
      goto error;
    }
  }
  
  // Define buffers 
  res = PylonStreamGrabberSetMaxNumBuffer(pylonsrc->streamGrabber, NUM_BUFFERS);
  PYLONC_CHECK_ERROR(pylonsrc, res);
  res = PylonStreamGrabberSetMaxBufferSize(pylonsrc->streamGrabber, pylonsrc->payloadSize);
  PYLONC_CHECK_ERROR(pylonsrc, res);

  // Prepare the camera for grabbing
  res = PylonStreamGrabberPrepareGrab(pylonsrc->streamGrabber);
  PYLONC_CHECK_ERROR(pylonsrc, res);

  for(i = 0; i <NUM_BUFFERS; ++i) {
    res = PylonStreamGrabberRegisterBuffer(pylonsrc->streamGrabber, buffers[i], pylonsrc->payloadSize, &bufferHandle[i]);
    PYLONC_CHECK_ERROR(pylonsrc, res);
  }
  
  for(i = 0; i <NUM_BUFFERS; ++i) {
    #pragma GCC diagnostic ignored "-Wint-to-pointer-cast" // This line comes from the SDK docs.
    res = PylonStreamGrabberQueueBuffer(pylonsrc->streamGrabber, bufferHandle[i], (void *) i);
    #pragma GCC diagnostic pop
    PYLONC_CHECK_ERROR(pylonsrc, res);
  }

  // Output the bandwidth the camera will actually use [B/s]
  if(PylonDeviceFeatureIsImplemented(pylonsrc->deviceHandle, "DeviceLinkCurrentThroughput") && PylonDeviceFeatureIsImplemented(pylonsrc->deviceHandle, "DeviceLinkSpeed")) {
    int64_t throughput = 0, linkSpeed = 0;

    res = PylonDeviceGetIntegerFeature(pylonsrc->deviceHandle, "DeviceLinkCurrentThroughput", &throughput);
    PYLONC_CHECK_ERROR(pylonsrc, res);
    res = PylonDeviceGetIntegerFeature(pylonsrc->deviceHandle, "DeviceLinkSpeed", &linkSpeed);
    PYLONC_CHECK_ERROR(pylonsrc, res);

    if(throughput > linkSpeed) {
      GST_ERROR_OBJECT(pylonsrc, "Not enough bandwidth for the specified parameters.");
      GST_ELEMENT_ERROR(pylonsrc, RESOURCE, FAILED, ("USB3 error"), ("Not enough bandwidth."));
      goto error;
    }

    GST_DEBUG_OBJECT(pylonsrc, "With current settings the camera requires %"PRId64"/%"PRId64" B/s (%.1lf out of %.1lf MB/s) of bandwidth.", throughput, linkSpeed, (double)throughput/1000000, (double)linkSpeed/1000000);
  } else {
    GST_WARNING_OBJECT(pylonsrc, "Couldn't determine link speed.");
  }

  // Output sensor readout time [us]
  if(PylonDeviceFeatureIsImplemented(pylonsrc->deviceHandle, "SensorReadoutTime")) {
    double readoutTime = 0.0;

    res = PylonDeviceGetFloatFeature(pylonsrc->deviceHandle, "SensorReadoutTime", &readoutTime);
    PYLONC_CHECK_ERROR(pylonsrc, res);

    GST_DEBUG_OBJECT(pylonsrc, "With these settings it will take approximately %.0lf microseconds to grab each frame.", readoutTime);
  } else {
    GST_WARNING_OBJECT(pylonsrc, "Couldn't determine sensor readout time.");
  }

  // Output final frame rate [Hz]
  if(PylonDeviceFeatureIsImplemented(pylonsrc->deviceHandle, "ResultingFrameRate")) {
    double frameRate = 0.0;

    res = PylonDeviceGetFloatFeature(pylonsrc->deviceHandle, "ResultingFrameRate", &frameRate);
    PYLONC_CHECK_ERROR(pylonsrc, res);

    GST_DEBUG_OBJECT(pylonsrc, "The resulting framerate is %.0lf fps.", frameRate);
    GST_DEBUG_OBJECT(pylonsrc, "Each frame is %"PRId32" bytes big (%.1lf MB). That's %.1lfMB/s.", pylonsrc->payloadSize, (double)pylonsrc->payloadSize/1000000, (pylonsrc->payloadSize*frameRate)/1000000);
  } else {
    GST_WARNING_OBJECT(pylonsrc, "Couldn't determine the resulting framerate.");
  }

  // Tell the camera to start recording
  res = PylonDeviceExecuteCommandFeature(pylonsrc->deviceHandle, "AcquisitionStart");
  PYLONC_CHECK_ERROR(pylonsrc, res);
  if(!pylonsrc->continuousMode) {
    res = PylonDeviceExecuteCommandFeature(pylonsrc->deviceHandle, "TriggerSoftware"); 
  PYLONC_CHECK_ERROR(pylonsrc, res);
  }
  pylonsrc->frameNumber = 0;

  GST_MESSAGE_OBJECT(pylonsrc, "Initialised successfully.");  
  return TRUE;

error:
  pylonc_disconnect_camera(pylonsrc);
  return FALSE;
}

static GstFlowReturn gst_pylonsrc_create (GstPushSrc *src, GstBuffer **buf)
{  
  GstPylonsrc *pylonsrc = GST_PYLONSRC (src);
  GENAPIC_RESULT res;
  size_t bufferIndex;
  PylonGrabResult_t grabResult;
  _Bool bufferReady;  
  GstMapInfo mapInfo;

  // Wait for the buffer to be filled  (up to 1 s)  
  res = PylonWaitObjectWait(pylonsrc->waitObject, 1000, &bufferReady);
  PYLONC_CHECK_ERROR(pylonsrc, res);
  if(!bufferReady) {
    GST_MESSAGE_OBJECT(pylonsrc, "Camera couldn't prepare the buffer in time. Probably dead.");    
    goto error;
  }

  res = PylonStreamGrabberRetrieveResult (pylonsrc->streamGrabber, &grabResult, &bufferReady);
  PYLONC_CHECK_ERROR(pylonsrc, res);
  if(!bufferReady) {
    GST_MESSAGE_OBJECT(pylonsrc, "Couldn't get a buffer from the camera. Basler said this should be impossible. You just proved them wrong. Congratulations!");    
    goto error;
  }

  if(!pylonsrc->continuousMode) {
      // Trigger the next picture while we process this one
      if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "AcquisitionStatus")) {
      _Bool isReady = FALSE;
      do {
        res = PylonDeviceGetBooleanFeature(pylonsrc->deviceHandle, "AcquisitionStatus", &isReady);
        PYLONC_CHECK_ERROR(pylonsrc, res);
      } while (!isReady);
    }
    res = PylonDeviceExecuteCommandFeature(pylonsrc->deviceHandle, "TriggerSoftware");
    PYLONC_CHECK_ERROR(pylonsrc, res);
  }

  // Process the current buffer
  bufferIndex = (size_t) grabResult.Context;
  if(grabResult.Status == Grabbed) {        
    //TODO: See if I can avoid memcopy and record directly into the gst buffer map.

    // Copy the image into the buffer that will be passed onto the next GStreamer element
    *buf = gst_buffer_new_and_alloc(pylonsrc->payloadSize);
    gst_buffer_map(*buf, &mapInfo, GST_MAP_WRITE);
    orc_memcpy(mapInfo.data, grabResult.pBuffer, mapInfo.size);
    gst_buffer_unmap(*buf, &mapInfo);        

    // Release frame's memory
    res = PylonStreamGrabberQueueBuffer( pylonsrc->streamGrabber, grabResult.hBuffer, (void*) bufferIndex );
    PYLONC_CHECK_ERROR(pylonsrc, res);
  } else {
    GST_ERROR_OBJECT(pylonsrc, "Error in the image processing loop.");    
    goto error;
  }

  // Set frame offset
  GST_BUFFER_OFFSET(*buf) = pylonsrc->frameNumber;
  pylonsrc->frameNumber += 1;
  GST_BUFFER_OFFSET_END(*buf) = pylonsrc->frameNumber;

  return GST_FLOW_OK;
error:
  return GST_FLOW_ERROR;
}

static gboolean
gst_pylonsrc_stop (GstBaseSrc * src) 
{
  GstPylonsrc *pylonsrc = GST_PYLONSRC (src);
  GST_DEBUG_OBJECT (pylonsrc, "stop");

  pylonc_disconnect_camera(pylonsrc);

  return TRUE;
}

void
gst_pylonsrc_dispose (GObject * object)
{
  GstPylonsrc *pylonsrc = GST_PYLONSRC (object);
  GST_DEBUG_OBJECT (pylonsrc, "dispose");
  G_OBJECT_CLASS (gst_pylonsrc_parent_class)->dispose (object);
}

void
gst_pylonsrc_finalize (GObject * object)
{
  GstPylonsrc *pylonsrc = GST_PYLONSRC (object);
  GST_DEBUG_OBJECT (pylonsrc, "finalize");

  pylonc_terminate();

  G_OBJECT_CLASS (gst_pylonsrc_parent_class)->finalize (object);
}

/* PylonC functions */
void  pylonc_initialize() {
  PylonInitialize();
}

void  pylonc_terminate() {
  PylonTerminate();
}

void
pylonc_disconnect_camera(GstPylonsrc* pylonsrc)
{
  if (deviceConnected) {
    if(strcmp(pylonsrc->reset, "after") == 0) {
      pylonc_reset_camera(pylonsrc);
    }

    PylonDeviceClose(pylonsrc->deviceHandle);
    PylonDestroyDevice(pylonsrc->deviceHandle);
    deviceConnected = FALSE;
    GST_DEBUG_OBJECT(pylonsrc, "Camera disconnected.");
  }
}

_Bool
pylonc_reset_camera(GstPylonsrc* pylonsrc)
{
  GENAPIC_RESULT res;
  if(PylonDeviceFeatureIsAvailable(pylonsrc->deviceHandle, "DeviceReset")) {
    GST_MESSAGE_OBJECT(pylonsrc, "Resetting device...");    
    res = PylonDeviceExecuteCommandFeature(pylonsrc->deviceHandle, "DeviceReset");
    PYLONC_CHECK_ERROR(pylonsrc, res);
    return TRUE;
  }

  error:
  GST_ERROR_OBJECT(pylonsrc, "ERROR: COULDN'T RESET THE DEVICE.");
  return FALSE;
}

_Bool
pylonc_connect_camera(GstPylonsrc* pylonsrc)
{
  GENAPIC_RESULT res;
  GST_DEBUG_OBJECT(pylonsrc, "Connecting to the camera...");

  res = PylonCreateDeviceByIndex(pylonsrc->cameraId, &pylonsrc->deviceHandle);
  PYLONC_CHECK_ERROR(pylonsrc, res);

  res = PylonDeviceOpen(pylonsrc->deviceHandle, PYLONC_ACCESS_MODE_CONTROL | PYLONC_ACCESS_MODE_STREAM);
  PYLONC_CHECK_ERROR(pylonsrc, res);

  deviceConnected = TRUE;
  return TRUE;

  error:
  return FALSE;
}

void
pylonc_print_camera_info(GstPylonsrc* pylonsrc, PYLON_DEVICE_HANDLE deviceHandle, int deviceId) {
  char name[256];
  char serial[256];
  char id[256];
  size_t siz = 0;
  GENAPIC_RESULT res;

  if (PylonDeviceFeatureIsReadable(deviceHandle, "DeviceModelName") && PylonDeviceFeatureIsReadable(deviceHandle, "DeviceSerialNumber")) {
    siz = sizeof(name);
    res = PylonDeviceFeatureToString(deviceHandle, "DeviceModelName", name, &siz );
    PYLONC_CHECK_ERROR(pylonsrc, res);

    siz = sizeof(serial);
    res = PylonDeviceFeatureToString(deviceHandle, "DeviceSerialNumber", serial, &siz );
    PYLONC_CHECK_ERROR(pylonsrc, res);
    
    if (PylonDeviceFeatureIsReadable(deviceHandle, "DeviceUserID")) {
      siz = sizeof(id);
      res = PylonDeviceFeatureToString(deviceHandle, "DeviceUserID", id, &siz );
      PYLONC_CHECK_ERROR(pylonsrc, res);
    }

    if(id[0]==(char)0) {
      strncpy(id, "None\0", 256);
    }
    
    if(pylonsrc->cameraId != deviceId) { // We're listing cameras
      GST_MESSAGE_OBJECT(pylonsrc, "ID:%i, Name:%s, Serial No:%s, Status: Available. Custom ID: %s", deviceId, name, serial, id);
    } else { // We've connected to a camera
      GST_MESSAGE_OBJECT(pylonsrc, "Status: Using camera \"%s\" (serial number: %s, id: %i). Custom ID: %s", name, serial, deviceId, id);
    }
  } else {
    error:
    GST_MESSAGE_OBJECT(pylonsrc, "ID:%i, Status: Could not properly identify connected camera, the camera might not be compatible with this plugin.", deviceId);
  }
}

/* GStreamer version definitions. */
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

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
    GST_VERSION_MINOR,
    pylonsrc,
    "A plugin that uses Basler's pylon5 to get data from Basler's USB3 Vision cameras.",
    plugin_init, VERSION, "LGPL", PACKAGE_NAME, GST_PACKAGE_ORIGIN);
