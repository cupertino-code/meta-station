# Image for station side
require image-common.inc

SPECIFIC_TOOLS = " \
    station-control \
    crsf-station \
    video-out \
    fontconfig \
"

GSTREAMER:append = " \
    gstreamer1.0-libav \
"

IMAGE_INSTALL:append = " udev udev-extraconf"
