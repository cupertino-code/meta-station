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

Gst.init(None)

WATCHDOG_TIMEOUT_US = 300000
WATCHDOG_CHECK_MS = 1000
BITRATE = 2000000
PID_FILE = "/tmp/camera-stream.pid"
VIDEO_WIDTH = 720
VIDEO_HEIGHT = 576

class GstElementError(Exception):
    def __init__(self, plugin):
        super().__init__(f'No such element or plugin "{plugin}"')

class VideoStreamer:
    def __init__(self, address: str, port: int):
        self.address = address
        self.bitrate = BITRATE
        self.port = port
        self.pipeline = None
        self.bus_id = None
        self.probe_id = None
        self.timeout_id = None
        self.last_buffer_time = 0

    def make_element(self, plugin, name):
        element = Gst.ElementFactory.make(plugin, name)
        if not element:
            print(f'No such element or plugin "{plugin}"')
            raise GstElementError(plugin)
        return element

    def create_pipeline(self, address: str, port: int):
        print("Creating new GStreamer pipeline...")
        try:
            pipeline = Gst.Pipeline.new("camera-h264-streamer")
            src = self.make_element("v4l2src", "source")
            convert = self.make_element("videoconvert", "converter")
            capsfilter = self.make_element("capsfilter", "capsfilter")
            encoder = self.make_element("v4l2h264enc", "encoder")
            encoder_caps = Gst.Caps.from_string("video/x-h264,profile=main,level=(string)4")
            encoder_capsfilter = self.make_element("capsfilter", "encoder-capsfilter")
            encoder_capsfilter.set_property("caps", encoder_caps)
            payloader = self.make_element("rtph264pay", "payloader")
            sink = self.make_element("udpsink", "sink")

            src.set_property("device", "/dev/video0")
            src.set_property("norm", "PAL")
            caps = Gst.Caps.from_string(f"video/x-raw,format=UYVY,width={VIDEO_WIDTH},height={VIDEO_HEIGHT},framerate=25/1")
            capsfilter.set_property("caps", caps)
            encoder_controls = Gst.Structure.new_from_string(f"controls,video_bitrate={self.bitrate}")
            encoder.set_property("extra-controls", encoder_controls)
            payloader.set_property("config-interval", -1)
            payloader.set_property("mtu", 1450)
            payloader.set_property("aggregate-mode", 1)
            sink.set_property("host", address)
            sink.set_property("port", port)
            sink.set_property("sync", False)
            sink.set_property("loop", False)

            pipeline.add(src)
            pipeline.add(capsfilter)
            pipeline.add(convert)
            pipeline.add(encoder)
            pipeline.add(encoder_capsfilter)
            pipeline.add(payloader)
            pipeline.add(sink)

            if not Gst.Element.link(src, capsfilter): return None
            if not Gst.Element.link(capsfilter, convert): return None
            if not Gst.Element.link(convert, encoder): return None
            if not Gst.Element.link(encoder, encoder_capsfilter): return None
            if not Gst.Element.link(encoder_capsfilter, payloader): return None
            if not Gst.Element.link(payloader, sink): return None

            return pipeline
        except GstElementError:
            return None

    def bus_call(self, bus, message):
        t = message.type
        if t == Gst.MessageType.EOS or t == Gst.MessageType.ERROR:
            err, debug = message.parse_error()
            if err:
                sys.stderr.write(f"GStreamer Error: {err.message}: {debug}\n")
            print("Pipeline stopped due to EOS or error. Restarting...")
            self.stop_and_restart()
        return True

    def signal_handler_usr1(self, sig, frame):
        print("Received SIGUSR1. Restarting the pipeline...")
        self.stop_and_restart()

    def signal_handler_usr2(self, sig, frame):
        print("Received SIGUSR2. Stopping the pipeline...")
        self._teardown_pipeline()

    def probe_buffer_cb(self, pad, info):
        self.last_buffer_time = Gst.util_get_timestamp()
        return Gst.PadProbeReturn.OK

    def check_pipeline_activity(self):
        if not self.pipeline:
            return GLib.SOURCE_REMOVE

        current_time = Gst.util_get_timestamp()
        time_diff = current_time - self.last_buffer_time

        if time_diff > WATCHDOG_TIMEOUT_US * 1000:
            print("No new frames detected. Restarting pipeline...")
            self.stop_and_restart()
            return GLib.SOURCE_REMOVE

        return GLib.SOURCE_CONTINUE

    def _setup_pipeline(self):
        if not self.pipeline:
            return

        bus = self.pipeline.get_bus()
        self.bus_id = bus.add_signal_watch()
        bus.connect("message", self.bus_call)

        src = self.pipeline.get_by_name("source")
        src_pad = src.get_static_pad("src")
        self.probe_id = src_pad.add_probe(Gst.PadProbeType.BUFFER, self.probe_buffer_cb)

        self.timeout_id = GLib.timeout_add(WATCHDOG_CHECK_MS, self.check_pipeline_activity)
        self.pipeline.set_state(Gst.State.PLAYING)
        self.last_buffer_time = Gst.util_get_timestamp()
        print("Pipeline started.")

    def _teardown_pipeline(self):
        if self.pipeline:
            print("Stopping pipeline...")
            self.pipeline.set_state(Gst.State.NULL)

            # Remove signal watch and probe
            if self.bus_id:
                bus = self.pipeline.get_bus()
                bus.remove_signal_watch()
                self.bus_id = None
            if self.probe_id:
                src = self.pipeline.get_by_name("source")
                if src:
                    src.get_static_pad("src").remove_probe(self.probe_id)
                self.probe_id = None
            if self.timeout_id:
                GLib.source_remove(self.timeout_id)
                self.timeout_id = None

            self.pipeline = None
            print("Pipeline stopped and resources released.")

    def stop_and_restart(self):
        self._teardown_pipeline()
        print("Waiting for 1 second before restarting...")
        GLib.timeout_add_seconds(0.3, self.start_pipeline)

    def start_pipeline(self):
        print("Attempting to start pipeline...")
        self._teardown_pipeline()

        self.pipeline = self.create_pipeline(self.address, self.port)
        if self.pipeline:
            self._setup_pipeline()
        else:
            print("Failed to create pipeline. Retrying in 5 seconds...")
            GLib.timeout_add_seconds(5, self.start_pipeline)

    def run(self):
        signal.signal(signal.SIGUSR1, self.signal_handler_usr1)
        signal.signal(signal.SIGUSR2, self.signal_handler_usr2)

        self.loop = GLib.MainLoop()

        print(f"Script started. PID: {os.getpid()} bitrate: {self.bitrate}")
        try:
            with open(PID_FILE, 'w') as f:
                f.write(str(os.getpid()) + '\n')
            print(f"PID written to {PID_FILE}")
        except IOError as e:
            print(f"Warning: Could not write PID file: {e}")

        self.start_pipeline()

        try:
            self.loop.run()
        except KeyboardInterrupt:
            print("Received Ctrl+C. Exiting...")
        finally:
            self._teardown_pipeline()

def main():
    parser = argparse.ArgumentParser(
        prog='ProgramName',
        description='What the program does',
        epilog='Text at the bottom of help')
    parser.add_argument('address', help="Destignation IP address")
    parser.add_argument('-p', '--port', default=5600, type=int, action='store', help='UDP port')

    args = parser.parse_args()

    streamer = VideoStreamer(args.address, args.port)
    streamer.run()

if __name__ == '__main__':
    main()
