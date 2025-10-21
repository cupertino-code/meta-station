#!/usr/bin/env python3
"""
Simple GStreamer RTP Stream Viewer with Recording (Command Line Version)

This application receives an RTP stream and displays it on screen.
Press Ctrl+C to quit, or send SIGUSR1 to toggle recording.

Usage:
    python3 rtp_viewer_cli.py --port 5600 --payload 96
"""

import gi
import sys
import argparse
import signal
import os
import mmap
import RPi.GPIO as GPIO
from datetime import datetime
import struct

gi.require_version('Gst', '1.0')
from gi.repository import Gst, GObject, GLib
PID_FILE_PATH = "/tmp/stream-viewer.pid"
MOUNT_HELPER_PATH = "/sys/kernel/mount_helper/mount_point"
LED_PIN = 22
SHARED_NAME = "/dev/shm/channel_data"

class GstElementError(Exception):
    def __init__(self, plugin):
        # Call the base class constructor with the parameters it needs
        super().__init__(f'No such element or plugin "{plugin}"')

class RTPStreamViewerCLI:
    def __init__(self, port=5600, payload_type=96, codec='H264'):
        Gst.init(None)
        GObject.threads_init()

        self.port = port
        self.payload_type = payload_type
        self.codec = codec
        self.is_recording = False
        self.pipeline = None
        self.tee = None
        self.record_elements = []
        self.main_loop = None
        self.bus_id = None
        shm_fd = os.open(SHARED_NAME, os.O_RDWR)
        self.shared_buffer = mmap.mmap(shm_fd, 2048)
        os.close(shm_fd)
        self.set_recording_flag(False)

        # Set up signal handlers
        signal.signal(signal.SIGUSR1, self.signal_handler)
        signal.signal(signal.SIGINT, self.signal_handler)
        signal.signal(signal.SIGTERM, self.signal_handler)

        self.setup_pipeline()

    def set_recording_flag(self, flag):
        self.shared_buffer[:4] = struct.pack('i', int(flag))

    def signal_handler(self, signum, frame):
        """Handle signals"""
        if signum == signal.SIGUSR1:
            print("\nToggling recording...")
            self.toggle_recording()
        elif signum in [signal.SIGINT, signal.SIGTERM]:
            print("\nShutting down...")
            if self.main_loop:
                self.main_loop.quit()

    def make_element(self, plugin, name):
        element = Gst.ElementFactory.make(plugin, name)
        if not element:
            print(f'No such element or plugin "{plugin}"')
            raise GstElementError(plugin)
        return element

    def setup_pipeline(self):
        """Create the main GStreamer pipeline"""
        # Create pipeline
        self.pipeline = Gst.Pipeline.new("rtp-viewer-pipeline")

        # RTP receiver elements
        self.udpsrc = self.make_element("udpsrc", "udp-source")
        self.udpsrc.set_property("port", self.port)
        self.udpsrc.set_property("caps", 
            Gst.Caps.from_string(f"application/x-rtp,payload={self.payload_type}"))

        # RTP depayloader (adjust based on codec)
        if self.codec.upper() == 'H264':
            self.rtpdepay = self.make_element("rtph264depay", "rtp-depay")
            self.parser = self.make_element("h264parse", "parser")
            self.decoder = self.make_element("avdec_h264", "decoder")
        elif self.codec.upper() == 'H265':
            self.rtpdepay = self.make_element("rtph265depay", "rtp-depay")
            self.parser = self.make_element("h265parse", "parser")
            self.decoder = self.make_element("avdec_h265", "decoder")
        else:
            print(f"Unsupported codec: {self.codec}")
            sys.exit(1)

        # Tee for splitting stream
        self.tee = self.make_element("tee", "tee")

        # Display branch
        self.queue_display = self.make_element("queue", "queue-display")
        self.videoconvert = self.make_element("videoconvert", "video-convert")

        sink_name = "fbdevsink"
        self.videosink = self.make_element(sink_name, "video-sink")
        if self.videosink:
            print(f"Using video sink: {sink_name}")

        if not self.videosink:
            print("Could not create video sink")
            sys.exit(1)

        # Add elements to pipeline
        elements = [
            self.udpsrc, self.rtpdepay, self.parser, self.decoder,
            self.tee, self.queue_display, self.videoconvert, 
            self.videosink
        ]

        for element in elements:
            if not element:
                print(f"Could not create element")
                sys.exit(1)
            self.pipeline.add(element)

        # Link main display chain
        if not self.udpsrc.link(self.rtpdepay):
            print("Could not link udpsrc to rtpdepay")
            sys.exit(1)
        if not self.rtpdepay.link(self.parser):
            print("Could not link rtpdepay to parser")
            sys.exit(1)
        if not self.parser.link(self.tee):
            print("Could not link parser to tee")
            sys.exit(1)

        # Link display branch
        tee_src_pad = self.tee.get_request_pad("src_%u")
        queue_sink_pad = self.queue_display.get_static_pad("sink")
        if not tee_src_pad.link(queue_sink_pad) == Gst.PadLinkReturn.OK:
            print("Could not link tee to queue_display")
            sys.exit(1)

        if not self.queue_display.link(self.decoder):
            print("Could not link queue_display to decoder")
            sys.exit(1)

        if not self.decoder.link(self.videoconvert):
            print("Could not link decoder to videoconvert")
            sys.exit(1)

        if not self.videoconvert.link(self.videosink):
            print("Could not link videoconvert to videoscale")
            sys.exit(1)

        # Set up message bus
        bus = self.pipeline.get_bus()
        self.bus_id = bus.add_signal_watch()
        bus.connect("message", self.on_message)

    def toggle_recording(self):
        """Toggle recording on/off"""
        if not self.is_recording:
            self.start_recording()
        else:
            self.stop_recording()

    def start_recording(self):
        """Start recording to file"""
        if self.is_recording:
            return

        if not os.path.isfile(MOUNT_HELPER_PATH):
            return
        with open(MOUNT_HELPER_PATH, 'r') as f:
            mount_point = f.read().strip()
        if not os.path.isdir(mount_point):
            return
        # Generate filename with timestamp
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = os.path.join(mount_point, f"recording_{timestamp}.mp4")

        # Create recording branch elements
        self.queue_record = self.make_element("queue", "queue-record")
        videoconvert_record = self.make_element("videoconvert", "videoconvert-record")

        mp4mux = self.make_element("mp4mux", "muxer")
        filesink = self.make_element("filesink", "file-sink")
        filesink.set_property("location", filename)

        # Store recording elements
        self.record_elements = [self.queue_record, videoconvert_record, mp4mux, filesink]

        # Add recording elements to pipeline
        for element in self.record_elements:
            if not element:
                print("Could not create recording element")
                return
            self.pipeline.add(element)

        # Link recording branch
        tee_src_pad = self.tee.get_request_pad("src_%u")
        queue_sink_pad = self.queue_record.get_static_pad("sink")

        if not tee_src_pad.link(queue_sink_pad) == Gst.PadLinkReturn.OK:
            print("Could not link tee to recording queue")
            return

        if not self.queue_record.link(mp4mux):
            print("Could not link recording elements")
            return
        if not mp4mux.link(filesink):
            print("Could not link recording elements")
            return

        # Sync state with pipeline
        for element in self.record_elements:
            element.sync_state_with_parent()

        self.is_recording = True
        self.set_recording_flag(True)
        GPIO.output(LED_PIN, GPIO.HIGH)
        print(f"Started recording to: {filename}")

    def stop_recording(self):
        """Stop recording"""
        if not self.is_recording or not self.record_elements:
            return

        # Send EOS to recording branch
        pad = self.queue_record.get_static_pad("sink")
        pad.send_event(Gst.Event.new_eos())

        # Wait a bit for EOS to propagate then clean up
        GLib.timeout_add(10, self.cleanup_recording_branch)
        GPIO.output(LED_PIN, GPIO.LOW)

    def cleanup_recording_branch(self):
        """Clean up recording branch elements"""
        if not self.is_recording:
            return False

        # Set recording elements to NULL state
        for element in self.record_elements:
            if element:
                element.set_state(Gst.State.NULL)
                self.pipeline.remove(element)

        self.record_elements = []
        self.is_recording = False
        print("Recording stopped")
        ret = self.pipeline.set_state(Gst.State.READY)
        self.set_recording_flag(False)
        if ret == Gst.StateChangeReturn.FAILURE:
            print("Unable to set pipeline to playing state")
            sys.exit(1)
        ret = self.pipeline.set_state(Gst.State.PLAYING)
        if ret == Gst.StateChangeReturn.FAILURE:
            print("Unable to set pipeline to playing state")
            sys.exit(1)
        return False  # Don't repeat timeout

    def on_message(self, bus, message):
        """Handle GStreamer messages"""
        t = message.type
        if t == Gst.MessageType.EOS:
            print("End-of-stream")
        elif t == Gst.MessageType.ERROR:
            err, debug = message.parse_error()
            print(f"Error: {err}, {debug}")
            source_name = message.src.get_name() if message.src else "unknown"
            if source_name == 'file-sink':
                self.stop_recording()

    def run(self):
        """Start the application"""
        print(f"Starting RTP stream viewer on port {self.port}")
        print(f"Send SIGUSR1 signal (kill -USR1 {os.getpid()}) to toggle recording")
        print("Press Ctrl+C to quit")
        print("Waiting for RTP stream...")
        with open(PID_FILE_PATH, "w") as pidfile:
            pidfile.write(f'{os.getpid()}')
        # Start pipeline
        ret = self.pipeline.set_state(Gst.State.PLAYING)
        if ret == Gst.StateChangeReturn.FAILURE:
            print("Unable to set pipeline to playing state")
            sys.exit(1)

        # Create and run main loop
        self.main_loop = GLib.MainLoop()
        try:
            self.main_loop.run()
        except KeyboardInterrupt:
            print("Interrupted by user")

        # Clean up
        if self.is_recording:
            self.stop_recording()

        if self.pipeline:
            self.pipeline.set_state(Gst.State.NULL)
        self.shared_buffer.close()

def main():
    parser = argparse.ArgumentParser(description='GStreamer RTP Stream Viewer with Recording (CLI)')
    parser.add_argument('--port', type=int, default=5600, help='UDP port to listen on (default: 5600)')
    parser.add_argument('--payload', type=int, default=96, help='RTP payload type (default: 96)')
    parser.add_argument('--codec', default='H264', choices=['H264', 'H265'], help='Video codec (default: H264)')

    args = parser.parse_args()

    GPIO.setmode(GPIO.BCM)
    GPIO.setup(LED_PIN, GPIO.OUT)
    viewer = RTPStreamViewerCLI(port=args.port, payload_type=args.payload, codec=args.codec)
    viewer.run()

if __name__ == '__main__':
    main()
