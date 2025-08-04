#!/bin/sh

IP=192.168.13.20
PORT=5000
BITRATE=1000000

sudo gst-launch-1.0 -vvv v4l2src device=/dev/video0 norm=PAL ! videoconvert ! v4l2h264enc extra_controls="controls,video_bitrate=${BITRATE}" ! "video/x-h264,profile=main,level=(string)4" ! rtph264pay config-interval=-1 mtu=1450 aggregate-mode=1 ! udpsink host=${IP} ttl-mc=4 port=${PORT} sync=false loop=false
