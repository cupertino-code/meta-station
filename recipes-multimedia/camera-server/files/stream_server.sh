#!/bin/bash

# --- Configuration ---
# IP address of the client that will receive the stream
# This is usually the IP of your computer or another Raspberry Pi
TARGET_IP="$1"
TARGET_PORT="$2"       # The port to stream to (make sure it's not blocked by firewall)
TARGET_IP=${TARGET_IP:-"192.168.13.20"}
TARGET_PORT=${TARGET_PORT:-"5000"}

# Video resolution and framerate
WIDTH="720"
HEIGHT="576"
FRAMERATE="30"
BITRATE="1000000" # 2 Mbps (adjust as needed for quality vs. bandwidth)

# Define the PID file path
PID_FILE="/var/run/gst_stream.pid"

# --- GStreamer Pipeline ---
# This pipeline captures video from the libcamera source, encodes it with h264 (hardware),
# and sends it over RTP.

GST_PIPELINE="v4l2src device=/dev/video0 norm=PAL ! \
              videoconvert ! \
              v4l2h264enc extra_controls=\"controls,video_bitrate=${BITRATE}\" ! \
              video/x-h264,profile=main,level=(string)4 ! \
              rtph264pay config-interval=-1 mtu=1450 aggregate-mode=1 ! \
              udpsink host=${TARGET_IP} ttl-mc=4 port=${TARGET_PORT} sync=false loop=false"

echo "Starting RTP server to ${TARGET_IP}:${TARGET_PORT} with resolution ${WIDTH}x${HEIGHT}@${FRAMERATE}fps, bitrate ${BITRATE} bps..."
echo "GStreamer Pipeline: ${GST_PIPELINE}"
# Check if a previous PID file exists and the process is running
if [ -f "$PID_FILE" ]; then
    OLD_PID=$(cat "$PID_FILE")
    if ps -p $OLD_PID > /dev/null 2>&1; then
        echo "Error: A previous gst-launch-1.0 process (PID $OLD_PID) is already running. Please stop it first."
        exit 1
    else
        echo "Warning: Stale PID file found. Deleting it."
        rm "$PID_FILE"
    fi
fi

# Run the GStreamer pipeline in the background and capture its PID
gst-launch-1.0 ${GST_PIPELINE}
GSTREAMER_PID=$!

# Save the PID to the file
echo $GSTREAMER_PID > "$PID_FILE"

echo "GStreamer process started with PID: $GSTREAMER_PID. PID saved to $PID_FILE"
echo "To stop the stream, run: kill $(cat $PID_FILE) (or kill -9 $(cat $PID_FILE))"
echo "Or simply: kill $GSTREAMER_PID"
echo "Remember to remove the PID file manually if the process crashes: rm $PID_FILE"

# Optional: Wait for the GStreamer process to finish if you want the script to block
# wait $GSTREAMER_PID

exit 0
