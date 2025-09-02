#!/bin/sh

PORT=$1
PORT=${PORT:-5600}

echo "gst-launch running on port $PORT"

gst-launch-1.0 -v udpsrc port=$PORT caps="application/x-rtp, media=video, encoding-name=H264, payload=96" ! \
    rtph264depay ! queue ! avdec_h264 ! \
    videoconvert ! queue ! \
    fbdevsink
