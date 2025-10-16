# Image for station side
require image-common.inc

SPECIFIC_TOOLS:append = " \
    station-control \
    video-out \
    fontconfig \
"

GSTREAMER:append = " \
    gstreamer1.0-libav \
    gstreamer1.0-plugins-ugly \
"
KERNEL_MODULES = " \
    mnthlp \
"

IMAGE_INSTALL:append = " \
    udev udev-extraconf \
    ${KERNEL_MODULES} \
"
