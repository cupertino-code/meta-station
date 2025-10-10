#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <glib-unix.h>
#include <gio/gio.h> // Required for GUnixSignalSource
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/videodev2.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <poll.h>
#include <opencv2/imgproc.hpp>
#include <glib.h>
#include <gst/gst.h>
#include <gst/app/gstappsrc.h>

#define WATCHDOG_TIMEOUT_US 300000
#define WATCHDOG_CHECK_MS 1000
#define PID_FILE "/tmp/camera-stream.pid"

using namespace cv;
using namespace std;

const int WIDTH = 720;
const int HEIGTH = 576;
const int FPS = 50;
const int imageSize = WIDTH * HEIGTH * 2;
GstClockTime timestamp = 0;

static const char DEVICE[] = "/dev/video0";
char *incImage = new char[imageSize];

struct Buffers {
    void *start;
    size_t length;
};

/* Structure to hold all the data */
typedef struct _PipelineData {
    GstElement *pipeline;
    GstElement *src;
    GMainLoop *loop;
    gchar *address;
    gint port;
    guint bus_watch_id;
    gint pad_probe_id;
    gboolean is_running;
    guint watchdog_timer_id; // Change to guint for g_timeout_add
    gint64 last_buffer_time;
    int fd;
    struct Buffers *buffers;
    unsigned int num_buffers;
    struct v4l2_requestbuffers reqbuf;
} PipelineData;

/* Forward declarations */
static void start_pipeline(PipelineData *data);
static void stop_pipeline(PipelineData *data);

/* Bus message handler */
static gboolean bus_call(GstBus *bus, GstMessage *msg, PipelineData *data)
{
    GError *err;
    gchar *debug_info;

    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_EOS:
            g_print("End-Of-Stream reached.\n");
            stop_pipeline(data);
            start_pipeline(data);
            break;
        case GST_MESSAGE_ERROR:
            gst_message_parse_error(msg, &err, &debug_info);
            g_printerr("Error received from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
            g_printerr("Debugging info: %s\n", (debug_info) ? debug_info : "none");
            g_clear_error(&err);
            g_free(debug_info);
            stop_pipeline(data);
            start_pipeline(data);
            break;
        case GST_MESSAGE_WARNING:
            gst_message_parse_warning(msg, &err, &debug_info);
            g_printerr("Warning received from element %s: %s\n", GST_OBJECT_NAME(msg->src), err->message);
            g_printerr("Debugging info: %s\n", (debug_info) ? debug_info : "none");
            g_clear_error(&err);
            g_free(debug_info);
            break;
        case GST_MESSAGE_STATE_CHANGED:
            GstState old_state, new_state, pending_state;
            gst_message_parse_state_changed(msg, &old_state, &new_state, &pending_state);
            if (GST_IS_PIPELINE(msg->src)) {
                g_print("Pipeline state changed from %s to %s\n", 
                        gst_element_state_get_name(old_state),
                        gst_element_state_get_name(new_state));
            }
            break;
        default:
            break;
    }
    return TRUE;
}

/* Pad probe callback to get a timestamp from the buffer */
static GstPadProbeReturn probe_buffer_cb(GstPad *pad, GstPadProbeInfo *info, PipelineData *data)
{
    data->last_buffer_time = g_get_monotonic_time();
    return GST_PAD_PROBE_OK;
}

/* Watchdog timer callback */
static gboolean check_pipeline_activity(PipelineData *data)
{
    gint64 current_time;
    gint64 time_diff;

    if (!data->is_running) {
        return G_SOURCE_REMOVE;
    }
/*
    current_time = g_get_monotonic_time();
    time_diff = current_time - data->last_buffer_time;

    if (time_diff > WATCHDOG_TIMEOUT_US) {
        g_print("No new frames detected. Restarting pipeline...\n");
        stop_pipeline(data);
        start_pipeline(data);
        return G_SOURCE_REMOVE;
    }
*/
    return G_SOURCE_CONTINUE;
}

/* Pipeline creation and setup */
static GstElement *create_and_setup_pipeline(PipelineData *data)
{
    GstElement *pipeline, *src, *convert, *capsfilter, *encoder, *encoder_capsfilter, *payloader, *sink;
    GstBus *bus;
    GstCaps *caps, *encoder_caps;
    GstStructure *encoder_controls;

    g_print("Creating new GStreamer pipeline...\n");

    /* Create elements */
    pipeline = gst_pipeline_new("camera-h264-streamer");
//    src = gst_element_factory_make("v4l2src", "source");
    src = gst_element_factory_make("appsrc", "source");
    convert = gst_element_factory_make("videoconvert", "converter");
    capsfilter = gst_element_factory_make("capsfilter", "capsfilter");
    encoder = gst_element_factory_make("v4l2h264enc", "encoder");
    encoder_capsfilter = gst_element_factory_make("capsfilter", "encoder-capsfilter");
    payloader = gst_element_factory_make("rtph264pay", "payloader");
    sink = gst_element_factory_make("udpsink", "sink");

    /* Check if elements were created */
    if (!pipeline || !src || !convert || !capsfilter || !encoder || !encoder_capsfilter || !payloader || !sink) {
        g_printerr("Failed to create one or more GStreamer elements!\n");
        if (pipeline) gst_object_unref(pipeline);
        if (src) gst_object_unref(src);
        if (convert) gst_object_unref(convert);
        if (capsfilter) gst_object_unref(capsfilter);
        if (encoder) gst_object_unref(encoder);
        if (encoder_capsfilter) gst_object_unref(encoder_capsfilter);
        if (payloader) gst_object_unref(payloader);
        if (sink) gst_object_unref(sink);
        return NULL;
    }

    /* Set properties */
//    g_object_set(src, "device", "/dev/video0", "norm", "PAL", NULL);
//    caps = gst_caps_from_string("video/x-raw,format=UYVY,width=720,height=576,framerate=25/1");
    caps = gst_caps_new_simple("video/x-raw",
                               "format", G_TYPE_STRING, "BGR",
                               "width", G_TYPE_INT, WIDTH,
                               "height", G_TYPE_INT, HEIGTH,
                               "framerate", GST_TYPE_FRACTION, FPS, 1,
                               NULL);
    g_object_set(G_OBJECT(src),
                 "caps", caps,
                 "is-live", TRUE,
                 "format", GST_FORMAT_TIME,
                 "stream-type", GST_APP_STREAM_TYPE_STREAM, // Mode PUSH
                 NULL);
    g_object_set(capsfilter, "caps", caps, NULL);
    gst_caps_unref(caps);

    encoder_controls = gst_structure_new_from_string("controls,video_bitrate=1000000");
    g_object_set(encoder, "extra-controls", encoder_controls, NULL);
    gst_structure_free(encoder_controls);

    encoder_caps = gst_caps_from_string("video/x-h264,profile=main,level=(string)4");
    g_object_set(encoder_capsfilter, "caps", encoder_caps, NULL);
    gst_caps_unref(encoder_caps);

    g_object_set(payloader, "config-interval", -1, "mtu", 1450, "aggregate-mode", 1, NULL);
    g_object_set(sink, "host", data->address, "port", data->port, "sync", FALSE, "loop", FALSE, NULL);

    /* Add elements to the pipeline */
    gst_bin_add_many(GST_BIN(pipeline), src, capsfilter, convert, encoder, encoder_capsfilter, payloader, sink, NULL);

    /* Link elements */
    if (!gst_element_link_many(src, capsfilter, convert, encoder, encoder_capsfilter, payloader, sink, NULL)) {
        g_printerr("Elements could not be linked.\n");
        gst_object_unref(pipeline);
        return NULL;
    }

    /* Get the bus and add a watch */
    bus = gst_element_get_bus(pipeline);
    data->bus_watch_id = gst_bus_add_watch(bus, (GstBusFunc)bus_call, data);
    gst_object_unref(bus);

    /* Add pad probe for activity check */
    GstPad *src_pad = gst_element_get_static_pad(src, "src");
    if (src_pad) {
        data->pad_probe_id = gst_pad_add_probe(src_pad, GST_PAD_PROBE_TYPE_BUFFER, (GstPadProbeCallback)probe_buffer_cb, data, NULL);
        gst_object_unref(src_pad);
    } else {
        g_printerr("Failed to get src pad.\n");
    }
    data->src = src;
    return pipeline;
}

/* Stop pipeline and free resources */
static void stop_pipeline(PipelineData *data)
{
    if (!data->pipeline) return;

    g_print("Stopping pipeline...\n");

    /* Stop watchdog timer */
    if (data->watchdog_timer_id) {
        g_source_remove(data->watchdog_timer_id);
        data->watchdog_timer_id = 0;
    }

    /* Remove pad probe */
    if (data->pad_probe_id) {
        GstElement *src = gst_bin_get_by_name(GST_BIN(data->pipeline), "source");
        if (src) {
            GstPad *src_pad = gst_element_get_static_pad(src, "src");
            if (src_pad) {
                gst_pad_remove_probe(src_pad, data->pad_probe_id);
                gst_object_unref(src_pad);
            }
            gst_object_unref(src);
        }
        data->pad_probe_id = 0;
    }

    /* Set pipeline to NULL state */
    gst_element_set_state(data->pipeline, GST_STATE_NULL);

    /* Remove bus watch */
    if (data->bus_watch_id) {
        g_source_remove(data->bus_watch_id);
        data->bus_watch_id = 0;
    }

    /* Unref the pipeline to free all resources */
    gst_object_unref(data->pipeline);
    data->pipeline = NULL;
    data->is_running = FALSE;
    g_print("Pipeline stopped and resources released.\n");
}

/* Start pipeline */
static void start_pipeline(PipelineData *data)
{
    if (data->is_running) {
        return;
    }

    g_print("Starting pipeline...\n");
    data->pipeline = create_and_setup_pipeline(data);

    if (data->pipeline) {
        gst_element_set_state(data->pipeline, GST_STATE_PLAYING);
        data->last_buffer_time = g_get_monotonic_time();
        data->is_running = TRUE;
        data->watchdog_timer_id = g_timeout_add(WATCHDOG_CHECK_MS, (GSourceFunc)check_pipeline_activity, data);
        g_print("Pipeline started.\n");
    } else {
        g_printerr("Failed to create pipeline. Retrying in 5 seconds...\n");
        g_timeout_add_seconds(5, (GSourceFunc)start_pipeline, data);
    }
}

/* New signal handler that is a GLib callback */
static gboolean signal_handler_restart(gpointer user_data)
{
    PipelineData *data = (PipelineData *)user_data;
    g_print("Received SIGUSR1. Restarting the pipeline...\n");
    stop_pipeline(data);
    start_pipeline(data);
    return G_SOURCE_CONTINUE; // Continue monitoring for the signal
}

static gboolean signal_handler_stop(gpointer user_data)
{
    PipelineData *data = (PipelineData *)user_data;
    g_print("Received SIGUSR2. Stoping the pipeline...\n");
    stop_pipeline(data);
    return G_SOURCE_CONTINUE; // Continue monitoring for the signal
}

static int xioctl(int fd, int request, void *arg)
{
    int r;

    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);

    return r;
}

static void init_mmap(PipelineData *pipeline)
{
    pipeline->reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    pipeline->reqbuf.memory = V4L2_MEMORY_MMAP;
    pipeline->reqbuf.count = 3;
    if (xioctl(pipeline->fd, VIDIOC_REQBUFS, &pipeline->reqbuf) == -1)
    {
        perror("VIDIOC_REQBUFS");
        exit(errno);
    }

    // if (reqbuf.count < 2){
    //   printf("Not enough buffer memory\n");
    //   exit(EXIT_FAILURE);
    // }
    printf("buffers to be used %d\n", pipeline->reqbuf.count);

    pipeline->buffers = (Buffers *)calloc(pipeline->reqbuf.count, sizeof(Buffers));
    assert(pipeline->buffers != NULL);

    // Create the buffer memory maps
    struct v4l2_buffer buffer;
    for (unsigned int i = 0; i < pipeline->reqbuf.count; i++) {
        memset(&buffer, 0, sizeof(buffer));
        buffer.type = pipeline->reqbuf.type;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;

        // Note: VIDIOC_QUERYBUF, not VIDIOC_QBUF, is used here!
        if (xioctl(pipeline->fd, VIDIOC_QUERYBUF, &buffer) == -1) {
            perror("VIDIOC_QUERYBUF");
            exit(errno);
        }

        pipeline->buffers[i].length = buffer.length;
        printf("Mapping %d bytes to %d (offset %d)\n", buffer.length, pipeline->fd, buffer.m.offset);
        pipeline->buffers[i].start = mmap(NULL, buffer.length, PROT_READ | PROT_WRITE,
                                          MAP_SHARED, pipeline->fd, buffer.m.offset);

        if (pipeline->buffers[i].start == MAP_FAILED) {
            perror("mmap");
            exit(errno);
        }
    }
}

static int init_device(PipelineData *pipeline)
{
    struct v4l2_fmtdesc fmtdesc = {0};
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    int min = 0;
    // Get the format with the largest index and use it

    pipeline->fd = open(DEVICE, O_RDWR);
    if (pipeline->fd < 0) {
        perror(DEVICE);
        return errno;
    }

    while (xioctl(pipeline->fd, VIDIOC_ENUM_FMT, &fmtdesc) == 0) {
        fmtdesc.index++;
    }
    printf("\nUsing format: %s\n", fmtdesc.description);

    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    fmt.fmt.pix.width = WIDTH;
    fmt.fmt.pix.height = HEIGTH;
    fmt.fmt.pix.pixelformat = fmtdesc.pixelformat;
    fmt.fmt.pix.field = V4L2_FIELD_INTERLACED; // V4L2_FIELD_ANY;

    if (xioctl(pipeline->fd, VIDIOC_S_FMT, &fmt) == -1) {
        perror("VIDIOC_S_FMT");
        exit(errno);
    }

    char format_code[5];
    strncpy(format_code, (char *)&fmt.fmt.pix.pixelformat, 5);
    printf(
        "Set format:\n"
        " Width: %d\n"
        " Height: %d\n"
        " Pixel format: %s\n"
        " Field: %d\n\n",
        fmt.fmt.pix.width,
        fmt.fmt.pix.height,
        format_code,
        fmt.fmt.pix.field);

    init_mmap(pipeline);
    return pipeline->fd;
}

static void start_capturing(PipelineData *pipeline)
{
    enum v4l2_buf_type type;

    struct v4l2_buffer buffer;
    printf("%s\n", __func__);
    for (unsigned int i = 0; i < pipeline->reqbuf.count; i++) {
        /* Note that we set bytesused = 0, which will set it to the buffer length
         * See
         * - https://www.linuxtv.org/downloads/v4l-dvb-apis-new/uapi/v4l/vidioc-qbuf.html?highlight=vidioc_qbuf#description
         * - https://www.linuxtv.org/downloads/v4l-dvb-apis-new/uapi/v4l/buffer.html#c.v4l2_buffer
         */
        memset(&buffer, 0, sizeof(buffer));
        buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buffer.memory = V4L2_MEMORY_MMAP;
        buffer.index = i;

        // Enqueue the buffer with VIDIOC_QBUF
        if (xioctl(pipeline->fd, VIDIOC_QBUF, &buffer) == -1) {
            perror("VIDIOC_QBUF");
            exit(errno);
        }
    }

    type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (xioctl(pipeline->fd, VIDIOC_STREAMON, &type) == -1) {
        perror("VIDIOC_STREAMON");
        exit(errno);
    }
}

static void stop_capturing(PipelineData *pipeline)
{
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (xioctl(pipeline->fd, VIDIOC_STREAMOFF, &type) == -1) {
        perror("VIDIOC_STREAMOFF");
        exit(errno);
    }
}

/**
 * @param appsrc Елемент GstAppSrc.
 * @param frame Вхідний кадр OpenCV.
 * @param timestamp Поточна мітка часу (GST_CLOCK_TIME_NONE, якщо не використовується).
 * @return GST_FLOW_OK або код помилки.
 */
GstFlowReturn push_mat_to_appsrc(GstElement *appsrc, cv::Mat frame)
{
    if (frame.empty()) {
        g_warning("Try to send empty frame.");
        return GST_FLOW_ERROR;
    }

    GstFlowReturn ret;
    GstBuffer *buffer;
    GstMapInfo map;

    gsize data_size = frame.total() * frame.elemSize();
    buffer = gst_buffer_new_allocate(NULL, data_size, NULL);

    if (buffer == NULL) {
        g_warning("Can't allocate GstBuffer.");
        return GST_FLOW_ERROR;
    }

    if (gst_buffer_map(buffer, &map, GST_MAP_WRITE)) {
        memcpy(map.data, frame.data, data_size);
        gst_buffer_unmap(buffer, &map);
    } else {
        g_warning("Can't show GstBuffer for writing.");
        gst_buffer_unref(buffer);
        return GST_FLOW_ERROR;
    }

    GST_BUFFER_TIMESTAMP(buffer) = timestamp;
    GST_BUFFER_DURATION(buffer) = gst_util_uint64_scale_int(1, GST_SECOND, FPS);
    ret = gst_app_src_push_buffer(GST_APP_SRC(appsrc), buffer);

    return ret;
}

static void process_image(PipelineData *pipeline, int index, int fieldType)
{
    const int lineSize = 1440;
    char *dataBuf = (char *)pipeline->buffers->start;
    static Mat frame, image;
    
    memcpy(incImage, dataBuf, HEIGTH * lineSize);
    frame = Mat(HEIGTH, WIDTH, CV_8UC2, incImage);
    cvtColor(frame, image, COLOR_YUV2BGR_UYVY);
    push_mat_to_appsrc(pipeline->src, image.clone());
}


/**
 * Readout a frame from the buffers.
 */
static int read_frame(PipelineData *pipeline)
{
    static int fieldNum = V4L2_FIELD_ANY;
    struct v4l2_buffer buffer;
//    static int fCnt = 0;
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer.memory = V4L2_MEMORY_MMAP;
    // Dequeue a buffer
    if (xioctl(pipeline->fd, VIDIOC_DQBUF, &buffer) == -1) {
        switch (errno)
        {
        case EAGAIN:
            // No buffer in the outgoing queue
            return 0;
        case EIO:
            // fall through
        default:
            perror("VIDIOC_DQBUF");
            exit(errno);
        }
    }

    assert(buffer.index < pipeline->reqbuf.count);
    process_image(pipeline, buffer.index, buffer.field);

    // Enqueue the buffer again
    if (xioctl(pipeline->fd, VIDIOC_QBUF, &buffer) == -1) {
        perror("VIDIOC_QBUF");
        exit(errno);
    }

    return 1;
}

static void main_loop(PipelineData *pipeline)
{
    while (true) {

        struct pollfd fds[2];
        int r;
        for (;;) {
            fds[0].fd = pipeline->fd;                                                                                                                                                                                
            fds[0].events = POLLIN;
            r = poll(fds, 1, 200);

            if (r == -1) {
                if (errno == EINTR)
                    continue;

                perror("poll");
                exit(errno);
            }

            if (!r) {
                printf("poll timeout. Restart\n");
                stop_capturing(pipeline);
                init_device(pipeline);
                start_capturing(pipeline);
                continue;
            }
            read_frame(pipeline);
            break;
        }
    }
}

static void release_reader(PipelineData *pipeline)
{
    for (unsigned int i = 0; i < pipeline->reqbuf.count; i++)
        munmap(pipeline->buffers[i].start, pipeline->buffers[i].length);
    free(pipeline->buffers);
    close(pipeline->fd);
}

static void *video_reader(void *arg)
{
    PipelineData *data = (PipelineData *)arg;
    init_device(data);
    start_capturing(data);
    main_loop(data);
    stop_capturing(data);
    release_reader(data);
    return NULL;
}

int main(int argc, char *argv[])
{
    PipelineData data = {0};
    FILE *pid_file;
    GSource *signal_source_restart; // Restart
    GSource *signal_source_stop; // Stop

    /* Initialize GStreamer */
    gst_init(&argc, &argv);

    /* Parse arguments */
    if (argc < 2) {
        g_print("Usage: %s <destination_ip> [port]\n", argv[0]);
        return 1;
    }
    data.address = argv[1];
    data.port = (argc > 2) ? g_ascii_strtod(argv[2], NULL) : 5600;

    /* Create the main loop */
    data.loop = g_main_loop_new(NULL, FALSE);

    /* Write PID to file */
    pid_file = fopen(PID_FILE, "w");
    if (pid_file) {
        fprintf(pid_file, "%d\n", getpid());
        fclose(pid_file);
        g_print("PID written to %s\n", PID_FILE);
    } else {
        g_warning("Could not write PID file.");
    }

    /* Register signal handlers as a GLib source */
    signal_source_restart = g_unix_signal_source_new(SIGUSR1);
    g_source_set_callback(signal_source_restart, signal_handler_restart, &data, NULL);
    g_source_attach(signal_source_restart, g_main_loop_get_context(data.loop));
    g_source_unref(signal_source_restart);

    signal_source_stop = g_unix_signal_source_new(SIGUSR2);
    g_source_set_callback(signal_source_stop, signal_handler_stop, &data, NULL);
    g_source_attach(signal_source_stop, g_main_loop_get_context(data.loop));
    g_source_unref(signal_source_stop);

    /* Start the initial pipeline */
    g_print("Initializing pipeline and starting main loop...\n");
    start_pipeline(&data);
    pthread_t reader_thread;
    pthread_create(&reader_thread, NULL, &video_reader, (void *)&data);

    /* Run the main loop */
    g_main_loop_run(data.loop);

    /* Clean up on exit */
    g_print("Exiting...\n");
    stop_pipeline(&data);
    pthread_join(reader_thread, NULL);
    g_main_loop_unref(data.loop);

    return 0;
}
