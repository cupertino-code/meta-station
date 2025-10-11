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
KERNEL_MODULES = " \
    mnthlp \
"

IMAGE_INSTALL:append = " \
    udev udev-extraconf \
    ${KERNEL_MODULES} \
"
