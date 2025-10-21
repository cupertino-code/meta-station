#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <cairo/cairo.h>
#include <math.h>
#include <pthread.h>

#include "visualisation.h"
#include "common.h"
#include "utils.h"
#include "config.h"

static volatile int run;

static pthread_t pthread;

#define PADDING_H  5.0
#define PADDING_V  3.0

static void *thread(void *arg MAYBE_UNUSED)
{
    int fbfd = 0;
    struct fb_var_screeninfo vinfo;
    struct fb_fix_screeninfo finfo;
    long int screensize = 0;
    uint8_t* fbp = NULL;
    cairo_t* cr;
    cairo_surface_t *surface;
    uint8_t* temp_fbp = NULL;
    cairo_t* temp_cr;
    cairo_surface_t *temp_surface;
    uint64_t timestamp = 0;

    // open the frame buffer file for reading & writing
    fbfd = open ( "/dev/fb0", O_RDWR );
    if (!fbfd) {
        printf ("Error: can't open framebuffer device.\n");
        exit (1);
    }

    if (ioctl (fbfd, FBIOGET_FSCREENINFO, &finfo)) {
        printf ("Error reading fixed information\n");
        close (fbfd);
        exit (2);
    }

    if (ioctl (fbfd, FBIOGET_VSCREENINFO, &vinfo)) {
        printf ("Error reading variable information\n");
        close (fbfd);
        exit (3);
    }
    // print info about the buffer
    LOG1("Frame buffer %dx%d, %dbpp\n", vinfo.xres, vinfo.yres, vinfo.bits_per_pixel);

    // calculates size
    screensize = vinfo.xres * vinfo.yres * vinfo.bits_per_pixel / 8;

    // map the device to memory 
    fbp = (uint8_t*) mmap (0, screensize, PROT_READ | PROT_WRITE, MAP_SHARED, fbfd, 0);
    if (fbp == (void *)-1) {
        printf ("Error: failed to map framebuffer device to memory\n");
        close (fbfd);
        exit (4);
    }
    temp_fbp = (uint8_t*)malloc(AREA_WIDTH * AREA_HEIGHT * vinfo.bits_per_pixel / 8);
    if (!temp_fbp) {
        printf("Error: unable to allocate area\n");
        munmap (fbp, screensize);
        close (fbfd);
        exit(4);
    }

    surface = cairo_image_surface_create_for_data (fbp, CAIRO_FORMAT_RGB16_565, 
        vinfo.xres, vinfo.yres, finfo.line_length);
    cr = cairo_create (surface);
    temp_surface = cairo_image_surface_create_for_data (temp_fbp, CAIRO_FORMAT_RGB16_565, 
        AREA_WIDTH, AREA_HEIGHT, AREA_WIDTH * vinfo.bits_per_pixel / 8);
    temp_cr = cairo_create (temp_surface);
    while(run) {
        double rad;
        double sn, cs;
        double x, y;
        char buf[100];
        cairo_text_extents_t te;
        int temp_angle;
        int temp_power;
        double circle_radius;

        temp_angle = antenna_status.angle;
        temp_power = antenna_status.power_status;
        cairo_set_source_rgb (temp_cr, 0, 0, 0);
        cairo_rectangle (temp_cr, 0, 0, 280, 280);
        cairo_fill (temp_cr);
        cairo_set_source_rgb (temp_cr, 0.5, 0.5, 0.5);
        cairo_set_line_width (temp_cr, 1);
        for (int angle = -120; angle <= 120; angle += 10) {
            rad = (double)angle * M_PI / 180.0;
            sn = sin(rad);
            cs = cos(rad);
            x = X_OFFSET + RADIUS + RADIUS * sn;
            y = Y_OFFSET - RADIUS * cs;
            cairo_move_to(temp_cr, x, y);
            x = X_OFFSET + RADIUS + (RADIUS + MARK_LEN) * sn;
            y = Y_OFFSET - (RADIUS + MARK_LEN) * cs;
            cairo_line_to(temp_cr, x, y);
        }
        if (antenna_status.updated) {
            rad = (double)temp_angle * M_PI / 180.0;
            sn = sin(rad);
            cs = cos(rad);
            cairo_set_line_width (temp_cr, 3);
            x = X_OFFSET + RADIUS + 5 * sn;
            y = Y_OFFSET - 5 * cs;
            cairo_move_to(temp_cr, x, y);
            x = X_OFFSET + RADIUS + (RADIUS + MARK_LEN + 3) * sn;
            y = Y_OFFSET - (RADIUS + MARK_LEN + 3) * cs;
            cairo_line_to(temp_cr, x, y);
        }
        cairo_stroke(temp_cr);
        sprintf(buf, "%d", temp_angle);
        x = X_OFFSET + RADIUS + RADIUS * sin(-120.0 * M_PI / 180);
        double x1 = X_OFFSET + RADIUS + RADIUS * sin(120.0 * M_PI / 180);
        cairo_set_source_rgb (temp_cr, 1, 1, 1);
        cairo_select_font_face (temp_cr, "Sans Serif",
            CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size (temp_cr, 16);
        cairo_text_extents (temp_cr, buf, &te);
        x = x + (x1 - x - te.width) / 2;
        y = Y_OFFSET - RADIUS * cos(120.0 * M_PI / 180);
        cairo_move_to (temp_cr, x - te.x_bearing, y - te.y_bearing);
        cairo_show_text (temp_cr, buf);
        y += te.height + 10;
        circle_radius = te.height / 2;
        sprintf(buf, "%s", antenna_status.updated ? (temp_power ? "Powered" : "POWER OFF") : "UNKNOWN");
        if (temp_power || !antenna_status.updated)
            cairo_set_source_rgb (temp_cr, 1, 1, 1);
        else
            cairo_set_source_rgb (temp_cr, 0.9, 0, 0);
        cairo_set_font_size (temp_cr, 12);
        cairo_text_extents (temp_cr, buf, &te);
        cairo_move_to (temp_cr, 20 - te.x_bearing, y - te.y_bearing);
        cairo_show_text (temp_cr, buf);
        y += te.height + 10;
        if (!antenna_status.connect_status) {
            cairo_set_source_rgb (temp_cr, 0.9, 0, 0);
            sprintf(buf, "NO CONNECTION");
            cairo_text_extents (temp_cr, buf, &te);
            cairo_move_to (temp_cr, 20 - te.x_bearing, y - te.y_bearing);
            cairo_show_text (temp_cr, buf);
        }
#if 0
        y += te.height + 10;
        if (antenna_status.updated) {
            sprintf(buf, "Battery  %d.%d", antenna_status.vbat / 1000, antenna_status.vbat % 1000 / 100);
            cairo_set_source_rgb (temp_cr, 0.8, 0.8, 0.8);
            cairo_text_extents (temp_cr, buf, &te);
            cairo_move_to (temp_cr, 20 - te.x_bearing, y - te.y_bearing);
            cairo_show_text (temp_cr, buf);
        }
#endif
        if (temp_power && antenna_status.shm.ptr) {
            struct shared_buffer *shbuf = (struct shared_buffer *)antenna_status.shm.ptr;
            struct channel_data data[CHANNELS_CNT];
            unsigned int size = CHANNELS_CNT;

            if (shbuf->flag) {
                if (get_chan_info(&shbuf->channels, data, &size)) {
                    cairo_set_font_size (temp_cr, 16);
                    for (unsigned int i=0; i < size; i++) {
                        y += te.height + 10;
                        sprintf(buf, "%s:%d", data[i].band_name, data[i].freq);
                        cairo_text_extents(temp_cr, buf, &te);
                        if (data[i].selected) {
                            double rect_width = te.width + 2 * PADDING_H;
                            double rect_height = te.height + 2 * PADDING_V;
                            cairo_set_source_rgb(temp_cr, 0.8, 0.8, 0.8);
                            cairo_rectangle(temp_cr,
                                    20 - te.x_bearing - PADDING_H, y - PADDING_V,
                                    rect_width, rect_height);
                            cairo_fill(temp_cr);
                            cairo_set_source_rgb(temp_cr, 0, 0, 0);
                        } else {
                            cairo_set_source_rgb(temp_cr, 0.8, 0.8, 0.8);
                        }
                        cairo_move_to(temp_cr, 20 - te.x_bearing, y - te.y_bearing);
                        cairo_show_text(temp_cr, buf);
                    }
                }
            }
            if (shbuf->recording) {
                uint64_t now = get_timestamp();
                if (!timestamp)
                    timestamp = now;
                if (now - timestamp <= 500) {
                    cairo_set_source_rgb(temp_cr, 1.0, 0.0, 0.0);
                    cairo_arc(temp_cr, circle_radius, circle_radius, circle_radius, 0, 2 * M_PI);
                    cairo_fill(temp_cr);
                } else if (now - timestamp >= 1000) {
                    timestamp = 0;
                }
            } else {
                timestamp = 0;
            }
        }
        cairo_set_source_surface(cr, temp_surface, 0, 0);
        cairo_set_operator (cr, CAIRO_OPERATOR_SOURCE);
        cairo_rectangle (cr, 0, 0, AREA_WIDTH, AREA_HEIGHT);
        cairo_fill (cr);
        cairo_surface_flush(surface); 
        sleep(0);
    }
    munmap (fbp, screensize);
    close (fbfd);
    return NULL;
}

int visualisation_init(void)
{
    int ret;

    run = 1;
    ret = pthread_create(&pthread, NULL, thread,NULL);
    if (ret) {
        fprintf(stderr, "pthread_create error %d\n", ret);
        run = 0;
        return 1;
    }
    return ret;
}

void visualisation_stop(void)
{
    if (!run)
        return;
    run = 0;
    pthread_join(pthread, NULL);
}
