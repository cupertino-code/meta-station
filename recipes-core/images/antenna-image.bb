# Image for antenna side
require image-common.inc

SPECIFIC_TOOLS = " \
    antenna-control \
    camera-server \
    crsf-antenna \
"
GSTREAMER:append = " \
    gstreamer1.0-libav \
    gstreamer1.0-plugins-good-video4linux2 \
"
