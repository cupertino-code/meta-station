#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <linux/gpio.h>
#include <sys/ioctl.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <pthread.h>
#include <sys/time.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <getopt.h>

#include "protocol.h"
#include "visualisation.h"
#include "common.h"

// Chip 0 on older Pi models, chip 4 on Pi 5.
#define CHIP "/dev/gpiochip0"

// GPIO line numbers.
#define GPIO_PIN_A 17
#define GPIO_PIN_B 18
#define GPIO_PIN_BTN 23
#define GPIO_PIN_MASTER 25
#define GPIO_PIN_GST 22

#define RISING_EDGE GPIO_V2_LINE_EVENT_RISING_EDGE
#define FALLING_EDGE GPIO_V2_LINE_EVENT_FALLING_EDGE
#define GPIO_V2_LINE_FLAG_EVENT_BOTH_EDGES (GPIO_V2_LINE_FLAG_EDGE_RISING | GPIO_V2_LINE_FLAG_EDGE_FALLING)

#ifndef CONSUMER
#define CONSUMER "antenna_control"
#endif

#define BUFFER_SIZE 100

#define STATUS_TIMEOUT 2000

struct buffer {
    struct rotator_protocol protocol_msg;
    union protocol {
        struct rotator_status status;
        struct rotator_command command;
    } payload;
};

struct gpio_data;
typedef void (*cb_t)(struct gpio_data *data, int pin, int edge);
int verbose = 0;

struct pin_data {
    int gpio;
    int pin_flags;
    int btn_num;
};

struct watch_pin {
    int gpio;
    int state;
    cb_t callback;
    struct pin_data *pin_data;
    int fd;
};

struct encoder_data {
    int last_a, last_b;
    uint64_t prev_timestamp;
    int counter;
    int diff_counter;
};

struct pin_data pin_a_data = {
    .gpio = GPIO_PIN_A,
    .pin_flags = 0,
};

struct pin_data pin_b_data = {
    .gpio = GPIO_PIN_B,
    .pin_flags = 0,
};

struct pin_data enc_btn_data = {
    .gpio = GPIO_PIN_BTN,
    .pin_flags = 0,
    .btn_num = SWITCH_ENCODER_NUM,
};

struct pin_data gst_sw_data = {
    .gpio = GPIO_PIN_GST,
    .pin_flags = 0,
    .btn_num = SWITCH_GST_NUM,
};

struct pin_data master_sw_data = {
    .gpio = GPIO_PIN_MASTER,
    .pin_flags = GPIO_V2_LINE_FLAG_BIAS_PULL_UP,
    .btn_num = SWITCH_ANTENNA_NUM,
};

static void encoder_callback(struct gpio_data *data, int index, int edge);
static void button_callback(struct gpio_data *data, int index, int edge);

struct watch_pin pins[] = {
    {GPIO_PIN_A, 0, encoder_callback, &pin_a_data, -1},
    {GPIO_PIN_B, 0, encoder_callback, &pin_b_data, -1},
    {GPIO_PIN_BTN, 0, button_callback, &enc_btn_data, -1},
    {GPIO_PIN_MASTER, 0, button_callback, &master_sw_data, -1},
    {GPIO_PIN_GST, 0, button_callback, &gst_sw_data, -1}
};

struct gpio_data {
    int fd;
    uint32_t switch_status;
    struct encoder_data encoder;
    uint32_t sw_timestamp[SWITCH_NUM];
    uint32_t sw_change;
    uint8_t sw_index[SWITCH_NUM];       // Index of the switch in pins array
    struct watch_pin *pins;
};

static volatile int run = 0;
static uint64_t status_timestamp = 0;

struct antenna_status antenna_status;

#define NUM_PINS (sizeof(pins) / sizeof(pins[0]))

#define PIN_A 0
#define PIN_B 1

static void send_message(struct gpio_data *data);

static void help()
{
    printf("Usage: station [options]\n");
    printf("Options:\n");
    printf("  -p, --IP port             Set UART device (default: %d)\n", DEFAULT_PORT);
    printf("  -v, --verbose             Increase verbosity level (can be used multiple times)\n");
    printf("  -h, --help                Show this help message\n");
    printf("  -V, --version             Show version information\n");
}

static void encoder_callback(struct gpio_data *data, int index, int edge)
{
    struct timespec ts;
    uint64_t timestamp;
    int curr_counter = 0;
    
    if (!(data->switch_status & SWITCH_ANTENNA))
        return;
    if (edge == RISING_EDGE)
        pins[index].state = 1;
    else
        pins[index].state = 0;
    if (pins[PIN_A].state != data->encoder.last_a || pins[PIN_B].state != data->encoder.last_b) {

        if (index == PIN_A) {
            if (pins[PIN_A].state == pins[PIN_B].state)
                curr_counter--;
            else
                curr_counter++;
        }
        else if (index == PIN_B)
        {
            if (pins[PIN_A].state == pins[PIN_B].state)
                curr_counter++;
            else
                curr_counter--;
        }
        data->encoder.last_a = pins[PIN_A].state;
        data->encoder.last_b = pins[PIN_B].state;
        clock_gettime(CLOCK_MONOTONIC, &ts);
        timestamp = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
        if (timestamp - data->encoder.prev_timestamp > 5) {
            data->encoder.counter += curr_counter;
            data->encoder.diff_counter += curr_counter;
            LOG1("Encoder counter %d\n", data->encoder.counter);
            send_message(data);
        }
        data->encoder.prev_timestamp = timestamp;
    }
}

static void button_callback(struct gpio_data *data, int index, int edge)
{
    struct timespec ts;
    uint64_t timestamp;
    static uint64_t prev_timestamp = 0;
    long pin_num = data->pins[index].pin_data->btn_num;
    long pin = BIT(pin_num);

    if (edge == RISING_EDGE) {
        pins[index].state = 1;
        data->switch_status &= ~pin;
    } else {
        pins[index].state = 0;
        data->switch_status |= pin;
    }
    LOG2("Edge %s on  %ld\n", edge == RISING_EDGE? "RISING":"FALLING", pin);
    clock_gettime(CLOCK_MONOTONIC, &ts);
    timestamp = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    if (timestamp - prev_timestamp < 5) {
        prev_timestamp = timestamp;
        LOG2("Button pin %d ignored due to debounce.\n", pins[index].gpio);
        return;
    }
    prev_timestamp = timestamp;
    data->sw_change |= pin;
    data->sw_timestamp[pin_num] = timestamp;
}

int check_switch(struct gpio_data *data, int sw_index)
{
    struct timespec ts;
    uint32_t timestamp;
    uint8_t pin_index = data->sw_index[sw_index];

    clock_gettime(CLOCK_MONOTONIC, &ts);
    timestamp = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    if (timestamp - data->sw_timestamp[sw_index] < 5) {
        LOG2("Switch %d ignored due to debounce.\n", sw_index);
        return 0;
    }
    data->sw_change &= ~BIT(sw_index);
    if (data->pins[pin_index].state) {
        LOG1("Button pin %d released.\n", data->pins[pin_index].gpio);
    } else {
        LOG1("Button pin %d pressed.\n", data->pins[pin_index].gpio);
    }
    send_message(data);
    return 1;
}

void *reader(void *arg)
{
    struct gpio_data *data = (struct gpio_data *)arg;
    struct pollfd poll_descriptors[NUM_PINS];
    struct gpio_v2_line_event event;
    unsigned i;

    for (i = 0; i < NUM_PINS; i++) {
        poll_descriptors[i].fd = data->pins[i].fd;
        poll_descriptors[i].events = POLLIN;
    }

    while (run) {

        int poll_result = poll(poll_descriptors, NUM_PINS, 100); // time out after 100 milliseconds

        if (poll_result == 0) {
            if (data->encoder.diff_counter) {
                LOG1("Diff counter %d\n", data->encoder.diff_counter);
                data->encoder.diff_counter = 0;
            }
            if (data->sw_change) {
                if (data->sw_change & SWITCH_ENCODER) {
                    check_switch(data, SWITCH_ENCODER_NUM);
                    LOG1("Encoder switch changed: 0x%x\n", data->switch_status);
                    data->sw_change &= ~SWITCH_ENCODER;
                }
                if (data->sw_change & SWITCH_ANTENNA)
                {
                    check_switch(data, SWITCH_ANTENNA_NUM);
                    LOG1("Antenna switch changed: 0x%x\n", data->switch_status);
                    data->sw_change &= ~SWITCH_ANTENNA;
                }
                if (data->sw_change) {
                    LOG1("Switches changed: 0x%x\n", data->sw_change);
                    data->sw_change = 0;
                }
                send_message(data);
            }
            continue;
        }
        
        if (poll_result < 0) {
            continue;
        }
        
        for (i = 0; i < NUM_PINS; i++) {
            if (poll_descriptors[i].revents & POLLIN) {

                int read_result = read(poll_descriptors[i].fd, &event, sizeof(event));

                if (read_result == -1) {
                    // printf("Read error.\n");
                    continue;
                }

                if (event.id == GPIO_V2_LINE_EVENT_RISING_EDGE ||
                    event.id == GPIO_V2_LINE_EVENT_FALLING_EDGE) {
                    if (data->pins[i].callback) {
                        data->pins[i].callback(data, i, event.id);
                    }
                }
            }
        }
    }
    for (i = 0; i < NUM_PINS; i++) 
        close(poll_descriptors[i].fd);

    return 0;
}

static void send_message(struct gpio_data *data)
{
    uint8_t buffer[sizeof(struct rotator_protocol) + sizeof(struct rotator_command) + 1];
    struct rotator_protocol *msg = (struct rotator_protocol *)buffer;
    struct rotator_command *command = (struct rotator_command *)&msg->payload;
    uint8_t *crc = buffer + sizeof(struct rotator_protocol) + sizeof(struct rotator_command);
    struct timespec ts;

    if (!antenna_status.connect_status)
        return;
    msg->start_byte = PROTOCOL_START_BYTE;
    msg->version = PROTOCOL_VERSION;
    msg->type = MESSAGE_TYPE_COMMAND;
    msg->length = sizeof(struct rotator_command);
    clock_gettime(CLOCK_MONOTONIC, &ts);
    msg->timestamp = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    command->position = data->encoder.counter;
    command->switches = data->switch_status;

    *crc = crc8_data(&buffer[offsetof(struct rotator_protocol, timestamp)], 
                     offsetof(struct rotator_protocol, payload) -
                     offsetof(struct rotator_protocol, timestamp) +
                     sizeof(struct rotator_command));

    ssize_t bytes_written = write(data->fd, buffer,
        sizeof(struct rotator_protocol) + sizeof(struct rotator_command) + 1);

    LOG2("Sent %d bytes message: length=%d, CRC=0x%x\n", (int)bytes_written, msg->length, *crc);
    dump(buffer, sizeof(struct rotator_protocol) + sizeof(struct rotator_command) + 1);
    if (bytes_written < 0)
        perror("Error writing to pipe");
}

void process_message(struct buffer *buf)
{
    if (buf->protocol_msg.type == MESSAGE_TYPE_STATUS) {
        LOG1("Angle: %d Power = %d\n", buf->payload.status.angle, buf->payload.status.status);
        antenna_status.angle = buf->payload.status.angle;
        antenna_status.power_status = buf->payload.status.status;
        antenna_status.updated = 1;
        antenna_status.vbat = buf->payload.status.vbat;
    }
}

#define RESET state = STATE_START_BYTE
int parse_byte(uint8_t byte)
{
    static enum {
        STATE_START_BYTE,
        STATE_VERSION,
        STATE_TYPE,
        STATE_LENGTH,
        STATE_TIMESTAMP,
        STATE_PAYLOAD,
        STATE_CRC
    } state = STATE_START_BYTE;
    static int expected_length;
    static int current_length;
    static struct buffer buffer;
    static int wrong_message = 0;
    
    switch (state) {
        case STATE_START_BYTE:
            if (byte != PROTOCOL_START_BYTE)
                break;
            buffer.protocol_msg.start_byte = byte;
            expected_length = 1;
            state = STATE_VERSION;
            break;
        case STATE_VERSION:
            wrong_message = 0;
            memset(&buffer, 0, sizeof(struct buffer));
            if (byte != PROTOCOL_VERSION) {
                fprintf(stderr, "Unsupported protocol version: %d\n", byte);
                RESET;
                break;
            }
            buffer.protocol_msg.version = byte;
            expected_length = 1;
            state = STATE_TYPE;
            break;
        case STATE_TYPE:
            if (byte != MESSAGE_TYPE_STATUS && byte != MESSAGE_TYPE_COMMAND) {
                fprintf(stderr, "Unsupported message type: %d\n", byte);
                RESET; // reset state
                break;
            }
            if (byte != MESSAGE_TYPE_STATUS) {
                fprintf(stderr, "Type status only allowed here\n");
                wrong_message = 1;
            }
            buffer.protocol_msg.type = byte;
            state = STATE_LENGTH;
            break;
        case STATE_LENGTH:
            buffer.protocol_msg.length = byte;
            if (buffer.protocol_msg.length != sizeof(struct rotator_status)) {
                fprintf(stderr, "Invalid payload length: %d\n", buffer.protocol_msg.length);
                RESET; // reset state
                break;
            }
            state = STATE_TIMESTAMP;
            expected_length = 4; // 4 bytes for timestamp
            current_length = 0;
            break;
        case STATE_TIMESTAMP:
            buffer.protocol_msg.timestamp = byte << 24 | buffer.protocol_msg.timestamp >> 8;
            current_length++;
            if (expected_length == current_length) {
                state = STATE_PAYLOAD;
                expected_length = buffer.protocol_msg.length;
                current_length = 0;
            }
            break;
        case STATE_PAYLOAD:
        {
            uint8_t *buf = (uint8_t *)&buffer.payload;
            buf[current_length++] = byte;
            if (current_length == expected_length) {
                expected_length = 1; // CRC byte
                state = STATE_CRC;
            }
            break;
        }
        case STATE_CRC:
        {
            RESET; // reset state
            if (wrong_message)
                break;
            uint8_t *buf = (uint8_t *)&buffer.protocol_msg;
            uint8_t crc = crc8_data(&buf[offsetof(struct rotator_protocol, timestamp)],
                                    buffer.protocol_msg.length + 4);
            if (byte != crc) {
                fprintf(stderr, "Invalid CRC\n");
                break;
            }
            process_message(&buffer);
            return 1;
        }
    }
    return 0;
}
#undef RESET

int start_server(int tcp_port, int fd_recv, struct gpio_data *data)
{
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);

    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        perror("Socket creation error");
        return -1;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(tcp_port);

    if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Binding socket error");
        close(server_sock);
        return -1;
    }

    if (listen(server_sock, 5) < 0) {
        perror("Listen error");
        close(server_sock);
        return -1;
    }

    printf("TCP server started on port %d\n", tcp_port);

    while (1) {
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);
        if (client_sock < 0) {
            perror("Error accepting connection");
            continue;
        }
        printf("Client connected\n");
        antenna_status.connect_status = 1;
        send_message(data);
        while (antenna_status.connect_status) {
            uint8_t buffer[BUFFER_SIZE];
            ssize_t bytes_read;

            struct pollfd fds[2];
            fds[0].fd = fd_recv;
            fds[0].events = POLLIN;
            fds[1].fd = client_sock;
            fds[1].events = POLLIN;

            LOG1("Waiting for data...\n");
            while (1) {
                int ret = poll(fds, 2, 1000);
                if (ret < 0) {
                    perror("Error polling");
                    break;
                }
                if (ret == 0) {
                    if (status_timestamp) {
                        struct timespec ts;
                        uint64_t timestamp;

                        clock_gettime(CLOCK_MONOTONIC, &ts);
                        timestamp = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
                        if (timestamp - status_timestamp > STATUS_TIMEOUT) {
                            antenna_status.connect_status = 0;
                            printf("Status timeout\n");
                            break;
                        }
                    }
                }

                if (fds[0].revents & POLLIN) {
                    bytes_read = read(fd_recv, buffer, sizeof(buffer));
                    if (bytes_read > 0) {
                        if (write(client_sock, buffer, bytes_read) < 0) {
                            perror("Error sending over TCP");
                            break;
                        }
                        LOG2("Pipe -> TCP %d bytes\n", (int)bytes_read);
                    }
                }

                if (fds[1].revents & POLLIN) {
                    bytes_read = read(client_sock, buffer, sizeof(buffer));
                    if (bytes_read > 0) {
                        for (ssize_t i = 0; i < bytes_read; i++) {
                            if (parse_byte(buffer[i])) {
                                struct timespec ts;

                                clock_gettime(CLOCK_MONOTONIC, &ts);
                                status_timestamp = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
                            }
                        }
                        LOG2("Received %d bytes from antenna\n", (int)bytes_read);
                        dump(buffer, bytes_read);
                    } else if (bytes_read == 0) {
                        printf("TCP peer closed the connection.\n");
                        antenna_status.connect_status = 0;
                        break;
                    }
                }
            }
        }
        antenna_status.connect_status = 0;

        close(client_sock);
        printf("Client disconnected\n");
    }

    close(server_sock);
    printf("Server stopped\n");
    return 0;
}

int main(int argc, char *argv[])
{
    int res;
    struct gpio_v2_line_request req[NUM_PINS];
    struct gpio_v2_line_values values;
    int pipe_fds[2];
    int file_descriptor;
    struct gpio_data data;
    int tcp_port;
    int opt, long_index;

    static struct option long_options[] = {
        {"tcp-port", required_argument, NULL, 'p'},
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'V'},
        {"verbose", no_argument, NULL, 'v'},
        {NULL, 0, NULL, 0}
    };

    tcp_port = DEFAULT_PORT;
    while ((opt = getopt_long(argc, argv, "vVhp:", long_options, &long_index)) != -1) {
        switch (opt) {
            case 'V':
                printf("Version %s\n", VERSION);
                return 0;
            case 'v':
                verbose++;
                break;
            case 'h':
                help();
                return 0;
            case 'p': // TCP port
                tcp_port = atoi(optarg);
                printf("TCP port set to: %d\n", tcp_port);
                break;
            case '?':
                break;
            default:
                printf("Unknown option: %s\n", long_options[long_index].name);
                return 1;
        }
    }
    file_descriptor = open(CHIP, O_RDONLY);
    if (file_descriptor < 0) {
        perror("Failed opening GPIO chip.\n");
        return 1;
    }

    for (unsigned i = 0; i < NUM_PINS; i++)
    {
        memset(&req[i], 0, sizeof(req[i]));
        req[i].num_lines = 1;
        req[i].offsets[0] = pins[i].gpio;
        req[i].config.flags = GPIO_V2_LINE_FLAG_INPUT | GPIO_V2_LINE_FLAG_EVENT_BOTH_EDGES |
                              pins[i].pin_data->pin_flags;
        strcpy(req[i].consumer, CONSUMER);
        res = ioctl(file_descriptor, GPIO_V2_GET_LINE_IOCTL, &req[i]);
        if (res < 0) {
            perror("Failed requesting events.\n");
            close(file_descriptor);
            return 1;
        }
        pins[i].fd = req[i].fd;
    }
    if (pipe(pipe_fds) < 0) {
        perror("Pipe creation error");
        close(file_descriptor);
        return 1;
    }
    memset(&data, 0, sizeof(data));
    data.pins = pins;
    data.fd = pipe_fds[1];
    data.sw_index[SWITCH_ANTENNA_NUM] = 3;
    data.sw_index[SWITCH_ENCODER_NUM] = 2;
    values.mask = 1;
    res = ioctl(pins[data.sw_index[SWITCH_ANTENNA_NUM]].fd, GPIO_V2_LINE_GET_VALUES_IOCTL,
                &values);
    if (res < 0) {
        perror("Failed to get GPIO value");
        close(pins[data.sw_index[SWITCH_ANTENNA_NUM]].fd);
        return 1;
    }
    if (!(values.bits & 1))
        data.switch_status |= SWITCH_ANTENNA;
    memset(&data.encoder, 0, sizeof(data.encoder));
    memset(&antenna_status, 0, sizeof(antenna_status));
    pthread_t reader_thread;
    run = 1;
    pthread_create(&reader_thread, NULL, &reader, (void *)&data);
    visualisation_init();
    if (start_server(tcp_port, pipe_fds[0], &data) != 0)
        run = 0;
    pthread_join(reader_thread, NULL);
    return 0;
}
