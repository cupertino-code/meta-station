require basic-dev-image.bb

SPECIFIC_TOOLS:append = " \
    antenna-control-dev \
    crsf-antenna-dev \
    video-streamer-dev \
"
GSTREAMER:append = " \
    gstreamer1.0-dev \
    gstreamer1.0-plugins-base-dev \
    gstreamer1.0-plugins-good-dev \
    opencv-dev \
"
