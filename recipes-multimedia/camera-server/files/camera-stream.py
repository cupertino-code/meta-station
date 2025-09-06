#!/usr/bin/env python3
import gi
import os
import signal
import sys
import time
import argparse

# Ensure GStreamer 1.0 is available
gi.require_version('Gst', '1.0')
from gi.repository import Gst, GLib

# Initialize GStreamer
Gst.init(None)

# Global pipeline object
pipeline = None

# A flag to trigger pipeline restart
restart_pipeline_flag = False

def create_pipeline(address: str, port: int):
    """
    Creates and returns a new GStreamer pipeline.
    """
    print("Creating new GStreamer pipeline...")

    # Create the pipeline and its elements
    pipeline = Gst.Pipeline.new("camera-h264-streamer")
    src = Gst.ElementFactory.make("v4l2src", "source")
    convert = Gst.ElementFactory.make("videoconvert", "converter")
    capsfilter = Gst.ElementFactory.make("capsfilter", "capsfilter")
    encoder = Gst.ElementFactory.make("v4l2h264enc", "encoder")
    # Create caps for the encoder output
    encoder_caps = Gst.Caps.from_string("video/x-h264,profile=main,level=(string)4")
    encoder_capsfilter = Gst.ElementFactory.make("capsfilter", "encoder-capsfilter")
    encoder_capsfilter.set_property("caps", encoder_caps)
#    h264_parse = Gst.ElementFactory.make("h264parse", "h264-parser")
    payloader = Gst.ElementFactory.make("rtph264pay", "payloader")
    sink = Gst.ElementFactory.make("udpsink", "sink")

    # Check if elements were created successfully
    if not all([pipeline, src, capsfilter, convert, encoder, encoder_capsfilter, payloader, sink]):
        print("Error: Failed to create one or more GStreamer elements!")
        return None


    # Set element properties
    src.set_property("device", "/dev/video0")
    src.set_property("norm", "PAL")
    # Set the caps filter for the camera's raw video output
    caps = Gst.Caps.from_string("video/x-raw,format=UYVY,width=720,height=576,framerate=25/1")
    capsfilter.set_property("caps", caps)
    encoder_controls = Gst.Structure.new_from_string("controls,video_bitrate=1000000")
    encoder.set_property("extra-controls", encoder_controls)
    payloader.set_property("config-interval", -1)
    payloader.set_property("mtu", 1450)
    payloader.set_property("aggregate-mode", 1)
    sink.set_property("host", address)
    sink.set_property("port", port)
    sink.set_property("sync", False)
    sink.set_property("loop", False)

    # Add elements to the pipeline
    pipeline.add(src)
    pipeline.add(capsfilter)
    pipeline.add(convert)
    pipeline.add(encoder)
    pipeline.add(encoder_capsfilter)
    pipeline.add(payloader)
    pipeline.add(sink)

    # Link the elements
    if not src.link(capsfilter): return None
    if not capsfilter.link(convert): return None
    if not convert.link(encoder): return None
    if not encoder.link(encoder_capsfilter): return None
    if not encoder_capsfilter.link(payloader): return None
    if not payloader.link(sink): return None

    return pipeline

def bus_call(bus, message, loop):
    """
    Handles messages from the GStreamer bus.
    """
    t = message.type
    if t == Gst.MessageType.EOS:
        sys.stdout.write("End-Of-Stream reached.\n")
        loop.quit()
    elif t == Gst.MessageType.ERROR:
        err, debug = message.parse_error()
        sys.stderr.write("GStreamer Error: %s: %s\n" % (err, debug))
        loop.quit()
    elif t == Gst.MessageType.WARNING:
        err, debug = message.parse_warning()
        sys.stderr.write("GStreamer Warning: %s: %s\n" % (err, debug))
    elif t == Gst.MessageType.STATE_CHANGED:
        old_state, new_state, pending_state = message.parse_state_changed()
        src = message.src
        # Check if the state change is for the entire pipeline
        if isinstance(src, Gst.Pipeline):
            print(f"Pipeline state changed from {Gst.Element.state_get_name(old_state)} to {Gst.Element.state_get_name(new_state)}")
    elif t == Gst.MessageType.QOS:
        live, running_time, stream_time, timestamp, duration = message.parse_qos()
        # You can check if the pipeline is dropping buffers
        print(f"QoS: {message.src.get_name()} live={live} running_time={running_time} stream_time={stream_time}, timestamp={timestamp}, duration={duration}")
#    else:
#        print(f'Got message {t}')
    return True

def signal_handler(sig, frame):
    """
    Signal handler for SIGUSR1. It sets a flag to restart the pipeline.
    """
    global restart_pipeline_flag
    print("Received SIGUSR1. Preparing to restart the pipeline...")
    restart_pipeline_flag = True

def main():
    """
    Main function to control the pipeline's lifecycle.
    """
    global pipeline, restart_pipeline_flag

    parser = argparse.ArgumentParser(
        prog='ProgramName',
        description='What the program does',
        epilog='Text at the bottom of help')
    parser.add_argument('address', help="Destignation IP address")
    parser.add_argument('-p', '--port', default=5600, type=int, action='store', help='UDP port')

    args = parser.parse_args()

    # Set up signal handler
    signal.signal(signal.SIGUSR1, signal_handler)

    # Create a GLib main loop
    loop = GLib.MainLoop()
    print(f"Script started. PID: {os.getpid()}")

    try:
        while True:
            if pipeline is None or restart_pipeline_flag:
                # Stop the current pipeline if it exists
                if pipeline:
                    print("Stopping current pipeline...")
                    bus = pipeline.get_bus()
                    bus.remove_signal_watch()
                    pipeline.set_state(Gst.State.NULL)
                    pipeline = None

                # Create and start a new pipeline
                pipeline = create_pipeline(args.address, args.port)
                if pipeline:
                    bus = pipeline.get_bus()
                    bus.add_signal_watch()
                    bus.connect("message", bus_call, loop)
                    print("Pipeline created. Starting to play...")
                    pipeline.set_state(Gst.State.PLAYING)
                    restart_pipeline_flag = False
                else:
                    print("Failed to create pipeline. Retrying in 5 seconds...")
                    time.sleep(5)
                    continue

            # Check for messages and handle events in a non-blocking way
            GLib.MainContext.default().iteration(False)
            time.sleep(0.1)

    except KeyboardInterrupt:
        print("Received Ctrl+C. Exiting...")
    finally:
        if pipeline:
            pipeline.set_state(Gst.State.NULL)

if __name__ == '__main__':
    main()
