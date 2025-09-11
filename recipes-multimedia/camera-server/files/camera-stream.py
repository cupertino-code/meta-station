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
PID_FILE = "/tmp/camera-stream.pid"

class GstElementError(Exception):
    def __init__(self, plugin, errors):
        # Call the base class constructor with the parameters it needs
        super().__init__(f'No such element or plugin "{plugin}"')

        # Now for your custom code...
        self.errors = errors

class VideoStreamer:
    def __init__(self, address: str, port: int):
        self.address = address
        self.port = port
        self.pipeline = None
        self.restart_pipeline_flag = False
        self.run_pipeline_flag = False
        self.last_buffer_time = 0
        self.count = 0

    def make_element(self, plugin, name):
        element = Gst.ElementFactory.make(plugin, name)
        if not element:
            print(f'No such element or plugin "{plugin}"')
            raise GstElementError(plugin)
        return element

    def create_pipeline(self, address: str, port: int):
        """
        Creates and returns a new GStreamer pipeline.
        """
        print("Creating new GStreamer pipeline...")

        # Create the pipeline and its elements
        pipeline = Gst.Pipeline.new("camera-h264-streamer")
        src = self.make_element("v4l2src", "source")
        convert = self.make_element("videoconvert", "converter")
        capsfilter = self.make_element("capsfilter", "capsfilter")
        encoder = self.make_element("v4l2h264enc", "encoder")
        # Create caps for the encoder output
        encoder_caps = Gst.Caps.from_string("video/x-h264,profile=main,level=(string)4")
        encoder_capsfilter = self.make_element("capsfilter", "encoder-capsfilter")
        encoder_capsfilter.set_property("caps", encoder_caps)
        payloader = self.make_element("rtph264pay", "payloader")
        sink = self.make_element("udpsink", "sink")

        # Check if elements were created successfully
        if not all([pipeline, src, capsfilter, convert, encoder, encoder_capsfilter, payloader, sink]):
            print("Error: Failed to create one or more GStreamer elements!")
            return None


        # Set element properties
        src.set_property("device", "/dev/video0")
        src.set_property("norm", "PAL")
        src.set_property("name", "source")

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
        if not src.link(capsfilter):
            print("Can't link src to capsfilter")
            return None
        if not capsfilter.link(convert):
            print("Can't link capsfilter to videoconver")
            return None
        if not convert.link(encoder):
            print("Can't link videoconvert to encoder")
            return None
        if not encoder.link(encoder_capsfilter):
            print("Can't link encoder to capsfilter")
            return None
        if not encoder_capsfilter.link(payloader):
            print("Can't capsfilter to payloader")
            return None
        if not payloader.link(sink):
            print("Can't link payloader to sink")
            return None
        return pipeline

    def bus_call(self, bus, message):
        """
        Handles messages from the GStreamer bus.
        """
        t = message.type
        if t == Gst.MessageType.EOS:
            sys.stdout.write("End-Of-Stream reached.\n")
            self.loop.quit()
        elif t == Gst.MessageType.ERROR:
            err, debug = message.parse_error()
            sys.stderr.write("GStreamer Error: %s: %s\n" % (err, debug))
            self.loop.quit()
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
            src_name = message.src.get_name() if message.src else "unknown"
#            print(f"QoS: [{src_name}] live={live} running_time={running_time} stream_time={stream_time}, timestamp={timestamp}, duration={duration}")
        return True

    def signal_handler_usr1(self, sig, frame):
        """
        Signal handler for SIGUSR1. It sets a flag to restart the pipeline.
        """
        print("Received SIGUSR1. Preparing to restart the pipeline...")
        self.restart_pipeline_flag = True
        self.start_pipeline()

    def signal_handler_usr2(self, sig, frame):
        """
        Signal handler for SIGUSR2. It sets a flag to stop the pipeline.
        """
        print("Received SIGUSR2. Preparing to stop the pipeline...")
        self.stop_pipeline()

    def probe_buffer_cb(self, pad, info):
        self.last_buffer_time = Gst.util_get_timestamp()
        self.count += 1
        return Gst.PadProbeReturn.OK

    def setup_pipeline(self):
        if self.pipeline:
            bus = self.pipeline.get_bus()
            bus.add_signal_watch()
            bus.connect("message", self.bus_call)
            self.pipeline.set_state(Gst.State.PLAYING)
            print("Pipeline created. Starting to play...")
            src = self.pipeline.get_by_name("source")
            src.get_static_pad("src").add_probe(Gst.PadProbeType.BUFFER, self.probe_buffer_cb)
            GLib.timeout_add(WATCHDOG_CHECK_MS, self.check_pipeline_activity)

    def check_pipeline_activity(self):
        if not self.run_pipeline_flag:
            return GLib.SOURCE_REMOVE
        if self.last_buffer_time == 0:
            return GLib.SOURCE_CONTINUE
        current_time = Gst.util_get_timestamp()
        time_diff = current_time - self.last_buffer_time
        self.count = 0

        if time_diff > WATCHDOG_TIMEOUT_US * 1000: # Час у nanoseconds
            print("Preparing to restart the pipeline...")
            self.restart_pipeline_flag = True
            return GLib.SOURCE_REMOVE

        return GLib.SOURCE_CONTINUE

    def start_pipeline(self):
        if self.restart_pipeline_flag:
            print("Restarting pipeline as requested...")
            self.restart_pipeline_flag = False
            self.stop_pipeline()
            time.sleep(0.2)
        self.run_pipeline_flag = True
        if self.pipeline == None:
            self.pipeline = self.create_pipeline(self.address, self.port)
            self.setup_pipeline()
            return
        if self.pipeline:
            if self.pipeline.get_state(0).state != Gst.State.PLAYING:
                self.setup_pipeline()

    def stop_pipeline(self):
        self.run_pipeline_flag = False
        if self.pipeline and self.pipeline.get_state(0).state != Gst.State.NULL:
            self.pipeline.set_state(Gst.State.NULL)
            bus = self.pipeline.get_bus()
            bus.remove_signal_watch()

    def run(self):
        signal.signal(signal.SIGUSR1, self.signal_handler_usr1)
        signal.signal(signal.SIGUSR2, self.signal_handler_usr2)
        # Create a GLib main loop
        self.loop = GLib.MainLoop()
        print(f"Script started. PID: {os.getpid()}")
        with open(PID_FILE, 'w') as f:
            f.write(str(os.getpid()) + '\n')
        print(f"PID written to {PID_FILE}")

        try:
            while True:
                if not self.run_pipeline_flag:
                    if self.pipeline and self.pipeline.get_state(0).state != Gst.State.NULL:
                        print("Stopping current pipeline...")
                        bus = self.pipeline.get_bus()
                        bus.remove_signal_watch()
                        self.pipeline.set_state(Gst.State.NULL)
                    GLib.MainContext.default().iteration(False)
                    time.sleep(2)
                    continue
                if self.pipeline is None:
                    # Create and start a new pipeline
                    self.pipeline = self.create_pipeline(self.address, self.port)
                    self.setup_pipeline()
                    if self.pipeline is None:
                        print("Failed to create pipeline. Retrying in 5 seconds...")
                        time.sleep(5)
                        continue
                    self.restart_pipeline_flag = False
                if self.restart_pipeline_flag:
                    self.stop_pipeline()
                    time.sleep(0.2)
                    self.start_pipeline()
                    self.restart_pipeline_flag = False
                    print("Restarting pipeline as requested...")

                # Check for messages and handle events in a non-blocking way
                GLib.MainContext.default().iteration(False)
                time.sleep(0.1)

        except KeyboardInterrupt:
            print("Received Ctrl+C. Exiting...")
        finally:
            if self.pipeline:
                self.pipeline.set_state(Gst.State.NULL)

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
