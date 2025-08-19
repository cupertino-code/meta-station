# Image for station side
require image-common.inc

SPECIFIC_TOOLS = " \
    station-control \
    crsf-station \
    video-out \
"
GSTREAMER:append = " \
    gstreamer1.0-libav \
"
