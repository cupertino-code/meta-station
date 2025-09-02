#!/usr/bin/env python3
"""
Simple GStreamer RTP Stream Viewer with Recording (Command Line Version)

This application receives an RTP stream and displays it on screen.
Press Ctrl+C to quit, or send SIGUSR1 to toggle recording.

Usage:
    python3 rtp_viewer_cli.py --port 5000 --payload 96
"""

import gi
import sys
import os
import argparse
import signal
import threading
import time
from datetime import datetime

gi.require_version('Gst', '1.0')
from gi.repository import Gst, GObject, GLib

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
        
        # Set up signal handlers
        signal.signal(signal.SIGUSR1, self.signal_handler)
        signal.signal(signal.SIGINT, self.signal_handler)
        signal.signal(signal.SIGTERM, self.signal_handler)
        
        self.setup_pipeline()
        
    def signal_handler(self, signum, frame):
        """Handle signals"""
        if signum == signal.SIGUSR1:
            print("\nToggling recording...")
            self.toggle_recording()
        elif signum in [signal.SIGINT, signal.SIGTERM]:
            print("\nShutting down...")
            if self.main_loop:
                self.main_loop.quit()
        
    def setup_pipeline(self):
        """Create the main GStreamer pipeline"""
        # Create pipeline
        self.pipeline = Gst.Pipeline.new("rtp-viewer-pipeline")
        
        # RTP receiver elements
        self.udpsrc = Gst.ElementFactory.make("udpsrc", "udp-source")
        self.udpsrc.set_property("port", self.port)
        self.udpsrc.set_property("caps", 
            Gst.Caps.from_string(f"application/x-rtp,payload={self.payload_type}"))
        
        # RTP depayloader (adjust based on codec)
        if self.codec.upper() == 'H264':
            self.rtpdepay = Gst.ElementFactory.make("rtph264depay", "rtp-depay")
            self.parser = Gst.ElementFactory.make("h264parse", "parser")
            self.decoder = Gst.ElementFactory.make("avdec_h264", "decoder")
        elif self.codec.upper() == 'H265':
            self.rtpdepay = Gst.ElementFactory.make("rtph265depay", "rtp-depay")
            self.parser = Gst.ElementFactory.make("h265parse", "parser")
            self.decoder = Gst.ElementFactory.make("avdec_h265", "decoder")
        else:
            print(f"Unsupported codec: {self.codec}")
            sys.exit(1)
        
        # Tee for splitting stream
        self.tee = Gst.ElementFactory.make("tee", "tee")
        
        # Display branch
        self.queue_display = Gst.ElementFactory.make("queue", "queue-display")
        self.videoconvert = Gst.ElementFactory.make("videoconvert", "video-convert")
        self.videoscale = Gst.ElementFactory.make("videoscale", "video-scale")
        
        # Video sink (try hardware acceleration first)
        self.videosink = None
        for sink_name in ["xvimagesink", "ximagesink", "autovideosink"]:
            try:
                self.videosink = Gst.ElementFactory.make(sink_name, "video-sink")
                if self.videosink:
                    print(f"Using video sink: {sink_name}")
                    break
            except:
                continue
        
        if not self.videosink:
            print("Could not create video sink")
            sys.exit(1)
        
        # Add elements to pipeline
        elements = [
            self.udpsrc, self.rtpdepay, self.parser, self.decoder,
            self.tee, self.queue_display, self.videoconvert, 
            self.videoscale, self.videosink
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
        if not self.parser.link(self.decoder):
            print("Could not link parser to decoder")
            sys.exit(1)
        if not self.decoder.link(self.tee):
            print("Could not link decoder to tee")
            sys.exit(1)
        
        # Link display branch
        tee_src_pad = self.tee.get_request_pad("src_%u")
        queue_sink_pad = self.queue_display.get_static_pad("sink")
        if not tee_src_pad.link(queue_sink_pad) == Gst.PadLinkReturn.OK:
            print("Could not link tee to queue_display")
            sys.exit(1)
        
        if not self.queue_display.link(self.videoconvert):
            print("Could not link queue_display to videoconvert")
            sys.exit(1)
        if not self.videoconvert.link(self.videoscale):
            print("Could not link videoconvert to videoscale")
            sys.exit(1)
        if not self.videoscale.link(self.videosink):
            print("Could not link videoscale to videosink")
            sys.exit(1)
        
        # Set up message bus
        bus = self.pipeline.get_bus()
        bus.add_signal_watch()
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
        
        # Generate filename with timestamp
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        filename = f"recording_{timestamp}.mp4"
        
        # Create recording branch elements
        queue_record = Gst.ElementFactory.make("queue", "queue-record")
        videoconvert_record = Gst.ElementFactory.make("videoconvert", "videoconvert-record")
        
        # Choose encoder based on codec
        if self.codec.upper() == 'H264':
            encoder = Gst.ElementFactory.make("x264enc", "encoder")
            if encoder:
                encoder.set_property("tune", "zerolatency")
                encoder.set_property("speed-preset", "fast")
        else:
            encoder = Gst.ElementFactory.make("avenc_h264", "encoder")
        
        mp4mux = Gst.ElementFactory.make("mp4mux", "muxer")
        filesink = Gst.ElementFactory.make("filesink", "file-sink")
        filesink.set_property("location", filename)
        
        # Store recording elements
        self.record_elements = [queue_record, videoconvert_record, encoder, mp4mux, filesink]
        
        # Add recording elements to pipeline
        for element in self.record_elements:
            if not element:
                print("Could not create recording element")
                return
            self.pipeline.add(element)
        
        # Link recording branch
        tee_src_pad = self.tee.get_request_pad("src_%u")
        queue_sink_pad = queue_record.get_static_pad("sink")
        
        if not tee_src_pad.link(queue_sink_pad) == Gst.PadLinkReturn.OK:
            print("Could not link tee to recording queue")
            return
        
        if not queue_record.link(videoconvert_record):
            print("Could not link recording elements")
            return
        if not videoconvert_record.link(encoder):
            print("Could not link recording elements")
            return
        if not encoder.link(mp4mux):
            print("Could not link recording elements")
            return
        if not mp4mux.link(filesink):
            print("Could not link recording elements")
            return
        
        # Sync state with pipeline
        for element in self.record_elements:
            element.sync_state_with_parent()
        
        self.is_recording = True
        print(f"Started recording to: {filename}")
    
    def stop_recording(self):
        """Stop recording"""
        if not self.is_recording or not self.record_elements:
            return
        
        # Send EOS to recording branch
        pad = self.record_elements[0].get_static_pad("sink")
        pad.send_event(Gst.Event.new_eos())
        
        # Wait a bit for EOS to propagate then clean up
        GLib.timeout_add(1000, self.cleanup_recording_branch)
        
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
        
        return False  # Don't repeat timeout
    
    def on_message(self, bus, message):
        """Handle GStreamer messages"""
        t = message.type
        if t == Gst.MessageType.EOS:
            print("End-of-stream")
            self.main_loop.quit()
        elif t == Gst.MessageType.ERROR:
            err, debug = message.parse_error()
            print(f"Error: {err}, {debug}")
            self.main_loop.quit()
    
    def run(self):
        """Start the application"""
        print(f"Starting RTP stream viewer on port {self.port}")
        print(f"Send SIGUSR1 signal (kill -USR1 {os.getpid()}) to toggle recording")
        print("Press Ctrl+C to quit")
        print("Waiting for RTP stream...")
        
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

def main():
    import os
    
    parser = argparse.ArgumentParser(description='GStreamer RTP Stream Viewer with Recording (CLI)')
    parser.add_argument('--port', type=int, default=5600, help='UDP port to listen on (default: 5600)')
    parser.add_argument('--payload', type=int, default=96, help='RTP payload type (default: 96)')
    parser.add_argument('--codec', default='H264', choices=['H264', 'H265'], help='Video codec (default: H264)')
    
    args = parser.parse_args()
    
    viewer = RTPStreamViewerCLI(port=args.port, payload_type=args.payload, codec=args.codec)
    viewer.run()

if __name__ == '__main__':
    main()
