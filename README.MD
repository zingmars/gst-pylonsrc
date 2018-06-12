# gst-pylonsrc

This is a gstreamer element that allows using Basler's USB3 Vision cameras using gstreamer.

This package also includes a simple plugin to check the framerate of any given pipeline called `fpsfilter`.

These plugins were open sourced by PlayGineering Ltd. on June 12, 2018.

## Requirements
#### To run
* Linux (Tested with Ubuntu 14.04 and Ubuntu 16.04)
* [Gstreamer-1.0 (Tested with gstreamer 1.8.3)](https://gstreamer.freedesktop.org/src/)
    * For older cameras You'll need the bayer2rgb plugin available in gst-plugins bad.
    * To record to file you'll need an encoder (i.e. x264enc).
* [Pylon 5.0.5 or newer (Tested with Pylon 5.0.5)](http://www.baslerweb.com/en/support/downloads/software-downloads)

#### To compile
* All of the above plus
* gcc 4.8 or newer (Tested with gcc 4.8.5). Note that this plugin will only compile with gcc or clang.
* GNU Make 3.8 or newer (Tested with gnu make 3.8.1)
* autoconf 2.69 or newer (Tested with autoconf 2.69)
* automake 1.14 or newer (Tested with automake 1.14.1)
* libtool 2.4 or newer (Tested with libtool 2.4.2)

## Compiling
After cloning the repository run the `autogen.sh` file in the root directory of the project. Autoconf will generate everything required for compilation. After it completes simply run the `make` command in the root directory and it should compile.

## Installation
After compiling there are two ways to install this plugin - automated and manual.

To install automatically simply run `sudo make install` inside the root directory of this project and Automake will automatically install the plugin.

To install manually you need copy, move or symlink the following files

* `./plugins/libgstpylonsrc.la`
* `./plugins/.libs/libgstpylonsrc.so`

to either of these locations:

* `~/.local/share/gstreamer-1.0/plugins/` if you only want your user to be able to use this plugin
* `/usr/local/lib/gstreamer-1.0/` if you want this plugin to be available to every user.

Note that these are the default locations which might be different if you installed gstreamer with a custom location prefix.

## Usage
To test whether this plugin was installed correctly use the `gst-inspect-1.0 pylonsrc` command. If it returns information about the plugin, then it's installed successfully and can be used like any other gstreamer source.

An example pipeline with this plugin for an older camera with no rgb8 support would be `gst-launch-1.0 -v pylonsrc ! bayer2rgb ! videoconvert ! xvimagesink`.

You set the parameters the same way one would set parameters for any other gstreamer pipeline element - by appending `key=value` to the plugin. For example, to get ycbcr422_8 output from the camera, you would write your pipeline like this - `gst-launch-1.0 pylonsrc imageformat=rgb8 ! videoconvert ! xvimagesink`.

#### Formats
This plugin supports the following output formats:
 + Bayer 8 (default) (value: `bayer8`)
 + Bayer 10 (value: `bayer10`)
 + Bayer 10 packed (value: `bayer10p`)
 + RGB8 (value: `rgb8`)
 + BGR8 (value: `bgr8`)
 + Mono8 (Greyscale) (value: `mono8`)
 + YcbCr422 8 (value: `ycbcr422_8`)

To change the output format simply set the `imageformat` parameter to the value of one of the formats above. Note that with the bayer formats you want to use the `bayer2rgb` plugin available in `gst-plugins-bad` (followed by `videoconvert` if you want the stream in YUY2 or similar format). RGB, BGR and Mono8 streams might require `videoconvert` for output to screen (video sinks such as `xvimagesink` only seems to play nice with YUY2 despite claiming to work with RGB).

#### Properties
If you have multiple cameras you can specify the camera you want to use using the `camera` parameter. To find out the IDs for each camera simply launch the pipeline without specifying the `camera` parameter, and the plugin will output them.

Each camera has a custom field that is saved on the camera called `userid`. This parameter allows user to quickly identify cameras on the field. This parameter will be listed when connecting to a camera or listing the devices. It can be set by either passing the `userid` parameter to the plugin, or by setting it in the Pylon Viewer App (`camera -> Device Control -> Device User ID`).

If you want to set a custom resolution you can do so with the `width` and `height` parameters. To find out the maximum resolution of the camera either refer to camera's technical specs or simply launch the plugin with logging tuned to loglevel4 (GST_DEBUG=pylonsrc:4), and it will output the maximum resolution the camera is capable of. Please note that the camera actually saves the resolution you set, so if you launch a pipeline with a camera once while specifying a resolution then the following runs will use the same resolution unless you either reconnect the device or specify a different resolution.

You can also specify resolution offsets to slightly move the image. Possible offsets are calculated `max(width|height)-current(width|height)`. The parameters to specify offsets are `offsetx` and `offsety`. If you want to center the image you can use `centerx` and `centery`. Note that setting the centering parameters will cause the plugin to ignore the offset values. 

The `limitbandwidth` parameter can be used to disable bandwidth limitations on the cameras. Basler cameras seem to limit themselves to 300MB/s (which is approx 130fps with a 1920x1200 resolution). Depending on the camera, it might be required to change the sensor readout mode (`sensorreadoutmode`) to fast for this to have any effect, as the default settings are usually close to the normal readout mode's maximum values. The possible values for this are either `on` or `off`. If you want to limit bandwidth to a specific number then set this property to `on` (or just don't specify it as `on` is the default) and use the `maxbandwidth` parameter which takes in the number of bytes per second that the camera will send data at. Note that setting the value too low will cause the camera to send data at a lower framerate than it is capable of, while setting it too high will in most cases have no discernible effect as the camera will only use as much bandwidth as it needs for the specified resolution and framerate. To see the bandwidth use run the pipeline with debugging level set to 4 (i.e. by adding `GST_DEBUG=pylonsrc:4` before the gst-launch-1.0 command) and all of the information related to framerate and bandwidth will be printed to screen during the initialisation procedure.

To achieve the full potential of the camera it might be required to set the sensor readout mode to fast. To do this use the `sensorreadoutmode` parameter which takes in either `fast` or `normal` as values. As the name implies, `fast` allows for higher framerates, while `normal` is the safe (and default) option.

To change the framerate use the `fps` property.

To have the camera send a test pattern instead of a video stream you can use the `testimage` property. It accepts values from 1-6 all of which correspond to different test patterns.

There are two modes for picture capture - trigger mode and continuous. Trigger mode asks for each frame seperately which while continuous mode makes the camera capture frames without software input. Continuous is default, but it can be disabled by setting the `continuous` parameter to `false` (default - `true`).

NOTE: Some of the parameters are saved to the camera. Running the pipeline multiple times without either reconnecting the device or using the `reset` parameter might cause weird behaviour. See the `gst-inspect-1.0` output for more details.

#### Image settings
To fine tune the image you can use the following properties:

* `lightsource` - Available values are `off`, `2800k`, (default)`5000k` and `6500k`. Changes the colour temperature of the video to have the best visibility in the given environment.
* `autoexposure` - Available values are (default)`off`, `once` and `continuous`. Enable automatic exposure. Overrides the `exposure` setting.
* `exposurelowerlimit`, `exposureupperlimit` - Sets the limit for auto exposure function. Values range from `105.0` to `1000000.0`.
* `exposure` - Accepts a number (microseconds). Sets the exposure time for each frame. `autoexposure` must be set to `off` for this to have any effect.
* `autowhitebalance` - Available values are `off`, `once` and `continuous`. Determines whether or not the camera will try to determine the best colour balance settings.
* `balancered`, `balancegreen` and `balanceblue` - Accepts a floating point number from `0` to `15,9`. Changes the colour balance for the specific colour. Defaults to whatever is set on the camera.
* `colorredhue`, `coloryellowhue`, `colorgreenhue`, `colorcyanhue`, `colorbluehue` and `colormagentahue` - Accepts a floating point number from -4 to 3.9. Changes the hue for the specific colour. Defaults to whatever is set on the camera.
* `colorredsaturation`, `coloryellowsaturation`, `colorgreensaturation`, `colorcyansaturation`, `colorbluesaturation` and `colormagentasaturation` - Accepts a floating point number from 0 to 1.9. Changes the saturation for the specific colour. Defaults to whatever is set on the camera.
* `transformationselector` - Sets the type of colour transformation used. This is used when using the `transformation` parameters. Available values are `rgbrgb`, `rgbyuv` and `yuvrgb`. The first colour is the origin format, the second format is the format that the colour will be transformed to. 
* `transformation00`, `transformation01`, `transformation02`, `transformation10`, `transformation11`, `transformation12`, `transformation20`, `transformation21`, `transformation22` allows for colour transformations within each colour channel. It's recommended to play around with the sliders in PylonViewerApp to find the values desired.
* `autogain` - Available values are `off`, `once` and `continuous`. Determines whether or not the camera will automatically adjust the gain option.
* `gainupperlimit`, `gainlowerlimit` - Sets the limit for auto gain function. Values range from `0` to `12.00921`.
* `gain` - Accepts a floating point number (dB) from `0` to `12`. Changes the amount of gain that will be added to the picture before it will be sent to the computer. `autogain` must be set to `off` for this to have any effect.
* `autobrightnesstarget` - Sets the target value for auto exposure function. Values range between `0.19608` and `0.80392`.
* `blacklevel` - Accepts a floating point number (DN) from `0` to `63.75`. Determines colour black's level.
* `gamma` - Accepts values from `0` to `3.9`. Changes the gamma of the picture.
* `sharpnessenhancement` - Uses basler's post-processing system to apply sharpness enhancement. This will only work if the `imageformat` is not bayer*. Values range from `1.0` to `3.98`. `demosaicing` must be set to true first.
* `noisereduction` - Uses basler's post-processing system to apply noise reduction. This will only work if the `imageformat` is not bayer*. Values range from `0` to `2.0`. `demosaicing` must be set to true fist.
* `autoprofile` - Determines whether the automatic brightness etc. functions will prioritise minimising gain or exposure. Values are `gain` and `exposure`.
#### Example pipelines
Output to screen: `GST_DEBUG=pylonsrc:5 gst-launch-1.0 pylonsrc imageformat=ycbcr422_8 ! xvimagesink`

Recording: `GST_DEBUG=pylonsrc:5 gst-launch-1.0 pylonsrc camera=0 ! bayer2rgb ! videoconvert ! x264enc noise-reduction=10000 pass=4 quantizer=22 bitrate=32768 speed-preset=0 byte-stream=true ! matroskamux ! filesink location='recording.mkv' sync=false`

Record a video at 150fps: `GST_DEBUG=pylonsrc:5 gst-launch-1.0 pylonsrc limitbandwidth=off sensorreadoutmode=fast fps=150 ! bayer2rgb ! videoconvert ! matroskamux ! filesink location='recording.mkv`

## fpsfilter
This package includes a simple plugin called `fpsfilter`. To use it simply plug it in any pipeline you want.

For example - `GST_DEBUG=pylonsrc:5 gst-launch-1.0 pylonsrc ! fpsfilter ! bayer2rgb ! videoconvert ! xvimagesink`.

The output is displayed in the console. You can control the frequency of the reports using `reporttime` parameter, which (in miliseconds) defines the time between messages that are sent to the screen. (default - 1000ms).

## Misc
If you need to reset the camera(s) quickly but don't want to reset it(them) using the reset parameter, you can use the `reset.sh` file in the tools directory which will reset the USB devices.

## TODO

* Rather than set every parameter ourselves, first get the value from camera and use that one if applicable.
* Convert all string literals to gstrings.
