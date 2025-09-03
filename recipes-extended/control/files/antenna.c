#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <linux/gpio.h>
#include <gpiod.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <ctype.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <sys/time.h>
#include <getopt.h>
#include "protocol.h"
#include "common.h"
 
#define BUFFER_SIZE 256
#define PWM_CHIP_PATH "/sys/class/pwm/pwmchip0"
#define PWM_CHANNEL 0
#define PWM_PERIOD 20000000 // 20ms period for 50Hz frequency
#define PWM_DUTY_CYCLE_MIN 600000 // 600us pulse width
#define PWM_DUTY_CYCLE_MAX 2400000 // 2.4ms pulse width
#define ANGLE_MIN  -120
#define ANGLE_MAX  120
#define PWM_CENTER (PWM_DUTY_CYCLE_MIN + PWM_DUTY_CYCLE_MAX) / 2
#define INI_FILE_NAME "/etc/control.ini"
// Chip 0 on older Pi models, chip 4 on Pi 5.
#define GPIO_CHIP_NAME "gpiochip0"
#define GPIO_CHIP_PATH "/dev/"GPIO_CHIP_NAME
#define GPIO_MASTER_SW 25

#ifndef CONSUMER
#define CONSUMER "antenna_control"
#endif

static int current_pwm = PWM_CENTER;
static int stored_pwm = 0;

struct switch_status {
    struct gpiod_line *line;
    int sw_status;
    int power_status;
};

int verbose = 0;
#define SOURCE_PWM      0
#define SOURCE_SWITCH   1

static struct switch_status master_sw;

static void help()
{
    printf("Usage: station [options] <server ip>\n");
    printf("Options:\n");
    printf("  -p, --IP port             Set UART device (default: %d)\n", DEFAULT_PORT);
    printf("  -v, --verbose             Increase verbosity level (can be used multiple times)\n");
    printf("  -h, --help                Show this help message\n");
    printf("  -V, --version             Show version information\n");
}

int nv_get_pwm()
{
    FILE *fp;
    int pwm = PWM_CENTER;
    char buf[100];

    fp = fopen(INI_FILE_NAME, "rt");
    if (!fp) {
        fprintf(stderr, "Can't open ini file %s\n", INI_FILE_NAME);
        return PWM_CENTER;
    }
    while (fgets(buf, sizeof(buf), fp)) {
        char *val;
        char *name = buf;
        char *temp;

        while (isspace(*name))
            name++;
        if (buf[0] == '#')
            continue;
        val = strchr(name, '=');
        if (!val)
            continue;
        temp = val - 1;
        while (isspace(*temp))
            *temp-- = 0;
        *val++ = 0;
        if (strcmp(name, "pwm"))
            continue;
        pwm = atol(val);
        if (pwm < PWM_DUTY_CYCLE_MIN || pwm > PWM_DUTY_CYCLE_MAX)
            pwm = PWM_CENTER;
        break;
    }
    fclose(fp);
    stored_pwm = pwm;
    return pwm;
}

void nv_set_pwm(int pwm)
{
    FILE *fp;

    fp = fopen(INI_FILE_NAME, "w");
    if (!fp) {
        fprintf(stderr, "Open %s error %d\n", INI_FILE_NAME, errno);
        return;
    }
    fputs("# DO NOT EDIT\n", fp);
    fprintf(fp, "pwm = %d\n", pwm);
    fclose(fp);
    stored_pwm = pwm;
}

#define set_timeout(sec) _set_timeout_implement(sec, __func__)

void _set_timeout_implement(int sec, const char *func)
{
    struct itimerval interval;
    int ret;

    interval.it_value.tv_sec = sec;
    interval.it_value.tv_usec = 0;
    interval.it_interval.tv_sec = 0;
    interval.it_interval.tv_usec = 0;
    ret = setitimer(ITIMER_REAL, &interval, NULL);
    if (ret)
        ret = errno;
    LOG2("%s: set timer. Result %d\n", func, ret);
}

void set_pwm(int duty_cycle)
{
    char buf[256];

    printf("%s(%d)\n", __func__, duty_cycle);
    if (stored_pwm != duty_cycle) {
        snprintf(buf, sizeof(buf), "%s/pwm%d/duty_cycle", PWM_CHIP_PATH, PWM_CHANNEL);
        FILE *fp = fopen(buf, "w");
        if (!fp) {
            perror("Failed to set PWM duty cycle");
            return;
        }
        fprintf(fp, "%d\n", duty_cycle);
        fclose(fp);
        set_timeout(5);
    }
}

void set_switch(struct switch_status *sw)
{
    if (master_sw.sw_status) {
        gpiod_line_set_value(sw->line, sw->sw_status);
        sw->power_status = sw->sw_status;
    } else {
        set_timeout(5);
    }
}

void signal_handler(int sig)
{
    if (sig == SIGALRM) {
        LOG2("SIGALARM %d %d\n", current_pwm, master_sw.sw_status);
        if (stored_pwm != current_pwm) {
            LOG1("Store pwm value %d.\n", current_pwm);
            nv_set_pwm(current_pwm);
        }
        gpiod_line_set_value(master_sw.line, master_sw.sw_status);
        master_sw.power_status = master_sw.sw_status;
    }
}

static void send_status(int fd)
{
    uint8_t buffer[sizeof(struct rotator_protocol) + sizeof(struct rotator_status) + 1];
    struct rotator_protocol *msg = (struct rotator_protocol *)buffer;
    struct rotator_status *status = (struct rotator_status *)&msg->payload;
    uint8_t *crc = buffer + sizeof(struct rotator_protocol) + sizeof(struct rotator_status);
    struct timespec ts;

    msg->start_byte = PROTOCOL_START_BYTE;
    msg->version = PROTOCOL_VERSION;
    msg->type = MESSAGE_TYPE_STATUS;
    msg->length = sizeof(struct rotator_status);
    clock_gettime(CLOCK_MONOTONIC, &ts);
    msg->timestamp = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    status->angle = ANGLE_MIN + (current_pwm - PWM_DUTY_CYCLE_MIN) /
                    ((PWM_DUTY_CYCLE_MAX - PWM_DUTY_CYCLE_MIN) / (ANGLE_MAX - ANGLE_MIN));
    status->status = master_sw.power_status;
    status->vbat = 0;

    *crc = crc8_data(&buffer[offsetof(struct rotator_protocol, timestamp)],
                     offsetof(struct rotator_protocol, payload) -
                     offsetof(struct rotator_protocol, timestamp) +
                     sizeof(struct rotator_status));

    ssize_t bytes_written =
        write(fd, buffer, sizeof(struct rotator_protocol) + sizeof(struct rotator_status) + 1);
    LOG2("Sent %d bytes message: length=%d, CRC=0x%x\n", bytes_written, msg->length, *crc);
    dump(buffer, sizeof(struct rotator_protocol) + sizeof(struct rotator_status) + 1);
    if (bytes_written < 0)
        perror("Error writing to pipe");
}

void process_command(struct rotator_command *command)
{
    static int first = 1;
    static int last_position = 0;
    int delta = command->position - last_position;
    int m_sw_status = (command->switches >> SWITCH_ANTENNA_NUM) & 1;

    if (_likely(!first))
    {
        int increment = command->switches & SWITCH_ENCODER ? 10 : 1;
        increment *= 3000;
        current_pwm += delta * increment;
    } else {
        first = 0;
        last_position = command->position;
        set_pwm(current_pwm);
        master_sw.sw_status = m_sw_status;
        LOG2("First switch\n");
        set_switch(&master_sw);
    }
    if (delta) {
        if (current_pwm < PWM_DUTY_CYCLE_MIN) {
             current_pwm = PWM_DUTY_CYCLE_MIN;
        } else if (current_pwm > PWM_DUTY_CYCLE_MAX) {
             current_pwm = PWM_DUTY_CYCLE_MAX;
        }
        set_pwm(current_pwm);
    }
    if (master_sw.sw_status != m_sw_status) {
        master_sw.sw_status = m_sw_status;
        LOG2("Set switch\n");
        set_switch(&master_sw);
    }
    LOG1("position=%d(delta %d), switches=%d duty_cycle=%d\n", command->position, delta,
           command->switches, current_pwm);
    last_position = command->position;
}

int parse_byte(uint8_t byte)
{
#define RESET state = STATE_START_BYTE
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
    static struct message {
        struct rotator_protocol protocol_msg;
        union protocol {
            struct rotator_status status;
            struct rotator_command command;
        } encoder_msg;
    } message;
    static int wrong_message = 0;
    int ret = 0;
    
    switch (state) {
        case STATE_START_BYTE:
            if (byte != PROTOCOL_START_BYTE)
                break;
            message.protocol_msg.start_byte = byte;
            expected_length = 1;
            state = STATE_VERSION;
            break;
        case STATE_VERSION:
            wrong_message = 0;
            memset(&message, 0, sizeof(struct message));
            if (byte != PROTOCOL_VERSION) {
                fprintf(stderr, "Unsupported protocol version: %d\n", byte);
                RESET;
                break;
            }
            message.protocol_msg.version = byte;
            expected_length = 1;
            state = STATE_TYPE;
            break;
        case STATE_TYPE:
            if (byte != MESSAGE_TYPE_STATUS && byte != MESSAGE_TYPE_COMMAND) {
                fprintf(stderr, "Unsupported message type: %d\n", byte);
                RESET; // reset state
                break;
            }
            if (byte != MESSAGE_TYPE_COMMAND) {
                fprintf(stderr, "Type command only allowed here\n");
                wrong_message = 1;
            }
            message.protocol_msg.type = byte;
            state = STATE_LENGTH;
            break;
        case STATE_LENGTH:
            message.protocol_msg.length = byte;
            if (message.protocol_msg.length != sizeof(struct rotator_command)) {
                fprintf(stderr, "Invalid payload length: %d\n", message.protocol_msg.length);
                state = STATE_START_BYTE; // reset state
                break;
            }
            state = STATE_TIMESTAMP;
            current_length = 0;
            expected_length = 4; // 4 bytes for timestamp
            break;
        case STATE_TIMESTAMP:
            message.protocol_msg.timestamp = byte << 24 | message.protocol_msg.timestamp >> 8;
            current_length++;
            if (expected_length == current_length) {
                if (message.protocol_msg.length != sizeof(struct rotator_command)) {
                    fprintf(stderr, "Invalid payload length: %d\n", message.protocol_msg.length);
                    RESET; // reset state
                    break;
                }
                state = STATE_PAYLOAD;
                expected_length = message.protocol_msg.length;
                current_length = 0;
            }
            break;
        case STATE_PAYLOAD:
        {
            uint8_t *buf = (uint8_t *)&message.encoder_msg;
            buf[current_length++] = byte;
            if (current_length == expected_length) {
                expected_length = 1; // CRC byte
                state = STATE_CRC;
            }
            break;
        }
        case STATE_CRC:
        {
            RESET;
            if (wrong_message)
                break;
            uint8_t *buf = (uint8_t *)&message;
            uint8_t crc = crc8_data(&buf[offsetof(struct rotator_protocol, timestamp)],
                message.protocol_msg.length + 4);
            if (byte != crc) {
                fprintf(stderr, "Have %02x Expected %02x Invalid CRC\n", byte, crc);
                break;
            }
            ret = 1;
            process_command(&message.encoder_msg.command);
            break;
        }
    }
    return ret;
}
#undef RESET
void prepare_pwm()
{
    char path[256];

    snprintf(path, sizeof(path), "%s/export", PWM_CHIP_PATH);
    FILE *fp = fopen(path, "w");
    if (fp) {
        fprintf(fp, "%d\n", PWM_CHANNEL);
        fclose(fp);
    } else {
        perror("Failed to prepare PWM");
    }

    snprintf(path, sizeof(path), "%s/pwm%d/period", PWM_CHIP_PATH, PWM_CHANNEL);
    fp = fopen(path, "w");
    if (fp) {
        fprintf(fp, "%d\n", PWM_PERIOD);
        fclose(fp);
    } else {
        perror("Failed to set PWM period");
    }

    current_pwm = nv_get_pwm();
    snprintf(path, sizeof(path), "%s/pwm%d/duty_cycle", PWM_CHIP_PATH, PWM_CHANNEL);
    fp = fopen(path, "w");
    if (fp) {
        fprintf(fp, "%d\n", current_pwm);
        fclose(fp);
    } else {
        perror("Failed to set PWM duty cycle");
    }

    snprintf(path, sizeof(path), "%s/pwm%d/enable", PWM_CHIP_PATH, PWM_CHANNEL);
    fp = fopen(path, "w");
    if (fp) {
        fprintf(fp, "1\n");
        fclose(fp);
    } else {
        perror("Failed to enable PWM channel");
    }
}

void process_connection(int tcp_sock)
{
    uint8_t buffer[BUFFER_SIZE];
    ssize_t bytes_read;


    struct pollfd fds[2];
    fds[0].fd = tcp_sock;
    fds[0].events = POLLIN;
    fds[1].fd = tcp_sock;
    fds[1].events = POLLIN;

    LOG1("Waiting for data...\n");
    while (1) {
        int status_sent = 0;

        int ret = poll(fds, 1, 1000);
        if (ret < 0) {
            perror("Error polling");
            if (errno == EINTR) {
                continue;
            }
            continue;
        }

        if (fds[0].revents & POLLIN) {
            bytes_read = read(tcp_sock, buffer, sizeof(buffer));
            if (bytes_read > 0) {
                for (ssize_t i = 0; i < bytes_read; i++) {
                    if (parse_byte(buffer[i])) {
                        send_status(tcp_sock);
                        status_sent = 1;
                    }
                }
                LOG2("Received %zd bytes\n", bytes_read);
            } else {
                printf("Zero bytes received\n");
                break;
            }
        }
        if (!status_sent) {
            send_status(tcp_sock);
            status_sent = 1;
        }
    }
    printf("Disconnecting...\n");
    close(tcp_sock);
}

int start_client(char *server_ip, uint16_t tcp_port)
{
    struct sockaddr_in server_addr;
    int tcp_sock;

    while (1) {
        tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (tcp_sock < 0) {
            perror("Socket creation error");
            return -1;
        }
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(tcp_port);
        if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
            perror("Invalid IP address");
            close(tcp_sock);
            return -1;
        }
        if (connect(tcp_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            perror("Connection error");
            sleep(5);
            continue;
        }
        printf("Connected to server %s:%d\n", server_ip, tcp_port);
        process_connection(tcp_sock);
        close(tcp_sock);
    }
    printf("Client stopped\n");
}

int main(int argc, char *argv[])
{
    int file_descriptor;
    uint16_t tcp_port = DEFAULT_PORT;
    int opt, long_index;

    static struct option long_options[] = {
        {"tcp-port", required_argument, NULL, 'p'},
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'V'},
        {"verbose", no_argument, NULL, 'v'},
        {NULL, 0, NULL, 0}
    };

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

    if (optind >= argc) {
        help();
        return 1;
    }
    file_descriptor = open(GPIO_CHIP_PATH, O_RDONLY);
    if (file_descriptor < 0) {
        perror("Failed opening GPIO chip.\n");
        return 1;
    }

    char *server_ip = argv[optind];
    signal(SIGALRM, signal_handler);
    prepare_pwm();
    struct gpiod_chip *chip;
    struct gpiod_line *line;
    int ret;

    chip = gpiod_chip_open_by_name(GPIO_CHIP_NAME);
    if (!chip) {
        perror("Open chip failed");
        return 1;
    }

    line = gpiod_chip_get_line(chip, GPIO_MASTER_SW);
    if (!line) {
        perror("Get line failed");
        gpiod_chip_close(chip);
        return 1;
    }
    master_sw.line = line;
    master_sw.sw_status = 0;
    master_sw.power_status = master_sw.sw_status;
    ret = gpiod_line_request_output(line, "master_switch", 0);
    if (ret < 0) {
        perror("Failed to set line as output");
        gpiod_line_release(line);
        gpiod_chip_close(chip);
        return 1;
    }
    if (start_client(server_ip, tcp_port) < 0) {
        fprintf(stderr, "Failed to start client\n");
        return 1;
    }

    close(file_descriptor);
    return 0;
}
