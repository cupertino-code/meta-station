#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <poll.h>
#include <getopt.h>
#include <time.h>
#include <sys/mman.h>
#include "circ_buf.h"
#include "shmem.h"
#include "utils.h"
#include "crsf_protocol.h"

#define UART_DEVICE "/dev/ttyS0"
#define BAUD_RATE 115200

#define UDP_PORT 7300

#define BUFFER_SIZE 64

#define VERSION "1.0"

int verbose = 0;
int tx_mode = 0; // TX mode flag
int diagnostic = 0; // Diagnostic flag

typedef void (*process_func_t)(int uart_fd, int udp_sock, const char *ip_addr, uint16_t udp_port);

process_func_t process_connection_func = NULL;

#define DSCP_EF 0x2e // 46
#define IPTOS_EF (DSCP_EF << 2)

#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif

#define STATE_SOURCE    0
#define STATE_LENGTH    1
#define STATE_TYPE      2
#define STATE_PAYLOAD   3
#define STATE_CRC       4

struct parser_state {
    int state;
    int length;
    int expected_length;
    int payload_length;
    int payload_pos;
    uint64_t packets;   // parsed packets
    uint64_t errs;      // unparsed packets
    uint8_t buffer[CRSF_MAX_PACKET_SIZE];
    uint8_t type;
    uint8_t *payload;
};

struct parser_state net_parser;
struct parser_state uart_parser;
struct circular_buf cbuf;
struct cbuf_item {
    uint8_t buf[CRSF_MAX_PACKET_SIZE];
    size_t len;
};

struct shared_memory shm;

#define CBUF_NUM 3

#define MAYBE_UNUSED __attribute__((unused))
void help()
{
    printf("Usage: connector [options] <peer IP>\n");
    printf("Options:\n");
    printf("  -u, --uart <device>       Set UART device (default: %s)\n", UART_DEVICE);
    printf("  -p, --udp-port <port>     Set UDP port (default: %d)\n", UDP_PORT);
    printf("  -b, --baudrate <rate>     Set UART baud rate (default: %d)\n", BAUD_RATE);
    printf("  -t, --tx mode             Enable TX mode\n");
    printf("  -v, --verbose             Increase verbosity level (can be used multiple times)\n");
    printf("  -d, --diag                Output diagnostic data (packet counters)\n");
    printf("  -h, --help                Show this help message\n");
    printf("  -V, --version             Show version information\n");
}

volatile int run;
volatile int *sock;

void sigint_handler(int sig) {
    printf("\nGot signal SIGINT (Ctrl+C). Exiting...\n");
    run = 0;
    if (sock && *sock) {
        close(*sock);
        sock = NULL;
    }
}

#if 1
int setup_uart(char *device, int MAYBE_UNUSED baud_rate)
{
    int uart_fd;
    struct termios tty_config;

    uart_fd = open(device, O_RDWR | O_NOCTTY | O_SYNC);
    if (uart_fd < 0) {
        perror("Error opening UART");
        return -1;
    }
    if (tcgetattr(uart_fd, &tty_config) != 0) {
        perror("Error tcgetattr");
        close(uart_fd);
        return -1;
    }

    tty_config.c_cflag = B115200 | CS8 | CLOCAL | CREAD;
    tty_config.c_iflag = IGNPAR;
    tty_config.c_oflag = 0;
    tty_config.c_lflag = 0;
    tcflush(uart_fd, TCIFLUSH);
    if (tcsetattr(uart_fd, TCSANOW, &tty_config) != 0) {
        perror("Error tcsetattr");
        close(uart_fd);
        return -1;
    }
    int flags = fcntl(uart_fd, F_GETFL, 0);
    if (flags != -1) {
        flags |= O_NONBLOCK;
        if (fcntl(uart_fd, F_SETFL, flags) == -1)
            perror("UART: Set non-blocking mode");
    } else {
        perror("UART: Get descriptor flags");
    }
    if (verbose)
        printf("UART configured on %s with baud rate %d\n", device, baud_rate);
    return uart_fd;
}
#else
int setup_uart(char *device, int baud_rate)
{
    int uart_fd;
    struct termios tty_config;
    // You should save the original settings to restore them later
    struct termios tty_old_config;

    uart_fd = open(device, O_RDWR | O_NOCTTY | O_SYNC);
    if (uart_fd < 0)
    {
        perror("Error opening UART");
        return -1;
    }

    if (tcgetattr(uart_fd, &tty_config) != 0)
    {
        perror("Error tcgetattr");
        close(uart_fd);
        return -1;
    }
    // Save old settings for restoration
    tty_old_config = tty_config;

    // Set input and output baud rate
    cfsetispeed(&tty_config, baud_rate);
    cfsetospeed(&tty_config, baud_rate);

    // Raw mode: no input or output processing
    tty_config.c_cflag |= (CLOCAL | CREAD); // Enable receiver, ignore modem status lines
    tty_config.c_cflag &= ~PARENB;          // No parity
    tty_config.c_cflag &= ~CSTOPB;          // 1 stop bit
    tty_config.c_cflag &= ~CSIZE;           // Clear character size mask
    tty_config.c_cflag |= CS8;              // 8 data bits

    tty_config.c_iflag = 0; // Disable all input processing
    tty_config.c_oflag = 0; // Disable all output processing
    tty_config.c_lflag = 0; // Disable all local modes

    // Set control characters for non-blocking read
    tty_config.c_cc[VMIN] = 1;
    tty_config.c_cc[VTIME] = 0;

    // Apply the changes immediately
    if (tcsetattr(uart_fd, TCSANOW, &tty_config) != 0)
    {
        perror("Error tcsetattr");
        close(uart_fd);
        return -1;
    }
    // Flush any pending data after setting the new configuration
    tcflush(uart_fd, TCIFLUSH);

    // This part is for demonstration and would need the restore logic
    if (verbose)
    {
        printf("UART configured on %s with baud rate %d\n", device, baud_rate);
    }
    return uart_fd;
}
#endif

void parser_init(struct parser_state *parser)
{
    parser->state = STATE_SOURCE;
    parser->length = 0;
    parser->expected_length = 0;
}

static inline uint64_t get_timestamp()
{
    struct timespec ts;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int parser(struct parser_state *parser, uint8_t byte)
{
    uint64_t timestamp;
    static uint64_t last_timestamp = 0;

    timestamp = get_timestamp();
    if (last_timestamp) {
        if (parser->state != STATE_SOURCE && (timestamp - last_timestamp) > 10) {
            parser->state = STATE_SOURCE;
            parser->errs++;
        }
    }
    last_timestamp = timestamp;

    switch (parser->state) {
        case STATE_SOURCE:
            if (byte == CRSF_ADDRESS_RADIO_TRANSMITTER ||
                byte == CRSF_ADDRESS_CRSF_TRANSMITTER) {
                parser->buffer[0] = byte;
                parser->state = STATE_LENGTH;
            }
            break;
        case STATE_LENGTH:
            if (byte < 3 || byte > (CRSF_MAX_PAYLOAD_LEN + 2)) {
                parser->length = 0;
                parser->state = STATE_SOURCE;
                parser->errs++;
                break;
            }
            parser->expected_length = byte;
            parser->buffer[1] = byte;
            parser->length = 2;
            parser->expected_length = byte + 2;
            parser->payload_length = byte - 2;
            parser->payload_pos = 0;
            parser->state = STATE_TYPE;
            break;
        case STATE_TYPE:
            parser->buffer[parser->length++] = byte;
            parser->type = byte;
            parser->state = STATE_PAYLOAD;
            break;
        case STATE_PAYLOAD:
            if (!parser->payload_pos) {
                parser->payload = &parser->buffer[parser->length];
            }
            parser->payload_pos++;
            parser->buffer[parser->length++] = byte;
            if (parser->payload_pos >= parser->payload_length) {
                // Message complete
                parser->state = STATE_CRC;
                parser->packets++;
            }
            break;
        case STATE_CRC:
            parser->buffer[parser->length++] = byte;
            parser->state = STATE_SOURCE;
            uint8_t crc = crc8_data(&parser->buffer[2], parser->payload_length + 1);
            if (crc == byte) {
                parser->packets++;
                return parser->length; // Return length of complete message
            } else {
                parser->errs++;
                return -parser->length; // Return length of complete message
            }

        default:
            parser->state = STATE_SOURCE; // Reset state on error
            parser->errs++;
            break;
    }
    return 0; // Message not complete yet
}

void process_tx_packet(struct shared_memory *shm, struct parser_state *parser)
{
    if (shm->ptr) {
        struct shared_buffer *buf = (struct shared_buffer *)shm->ptr;
        if (parser->type == 0x16 && parser->payload_length >= 22) {
            memcpy(&buf->channels, parser->payload, parser->payload_length);
            buf->flag = 1; // Indicate new data available
        }
    }
}

void process_connection_tx(int uart_fd, int udp_sock, const char *ip_addr, uint16_t udp_port)
{
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    struct sockaddr_in from, to;
    socklen_t from_len;

    struct pollfd fds[2];
    fds[0].fd = uart_fd;
    fds[0].events = POLLIN;
    fds[1].fd = udp_sock;
    fds[1].events = POLLIN;

    to.sin_family = AF_INET;
    to.sin_port = htons(udp_port);
    if (inet_pton(AF_INET, ip_addr, &to.sin_addr) <= 0)
    {
        perror("Invalid IP address");
        close(udp_sock);
        return;
    }
    tcflush(uart_fd, TCIOFLUSH);

    if (verbose)
        printf("Waiting for data...\n");
    while (run) {
        int ret = poll(fds, 2, 1000);
        if (ret < 0) {
            perror("Error polling");
            break;
        }
        if (diagnostic) {
            printf("UDP  packets: %llu errors: %llu\n", net_parser.packets, net_parser.errs);
            printf("UART packets: %llu errors: %llu\r\033[A",
                   uart_parser.packets, uart_parser.errs);
        }
        if (ret == 0)
            continue;

        if (fds[0].revents & POLLIN) {
            bytes_read = read(uart_fd, buffer, sizeof(buffer));
            if (bytes_read > 0) {
                for (int i = 0; i < bytes_read; i++) {
                    int packet_length = parser(&uart_parser, buffer[i]);
                    if (packet_length) {
                        if (sendto(udp_sock, uart_parser.buffer, uart_parser.length, 0,
                                   (struct sockaddr *)&to, sizeof(to)) < 0) {
                            perror("Error sending over UDP");
                            continue;
                        }
                        if (packet_length > 0) {
                            process_tx_packet(&shm, &uart_parser);
                        }
                        if (verbose) {
                            printf("UART -> UDP: sent %d bytes\n", uart_parser.length);
                            if (verbose > 1 )
                                dump("UART data", uart_parser.buffer, uart_parser.length);
                        }
                        if (!cbuf_empty(&cbuf)) {
                            uint8_t *buf;

                            buf = cbuf_get_ptr(&cbuf);
                            if (verbose) {
                                printf("Writing to UART %d bytes\n", buf[1]);
                                if (verbose > 1)
                                    dump("Data", buf, buf[1]);
                            }
                            if (write(uart_fd, buf, buf[1] + 2) < 0)
                                perror("UART write error");
                            cbuf_drop(&cbuf);
                        }
                    }
                }
            } else if (bytes_read == 0) {
                printf("UART closed the connection.\n");
                break;
            } else {
                perror("Error reading from UART");
                break;
            }
        }

        if (fds[1].revents & POLLIN) {
            bytes_read = recvfrom(udp_sock, buffer, sizeof(buffer), 0, (struct sockaddr *)&from,
                                 &from_len);
            if (bytes_read > 0) {
                for (int i = 0; i < bytes_read; i++) {
                    int len = parser(&net_parser, buffer[i]);
                    if (len > 0)
                    {
                        cbuf_put(&cbuf, net_parser.buffer);
                        if (verbose)
                        {
                            printf("UDP(from %s): received %d bytes\n", inet_ntoa(from.sin_addr),
                                   net_parser.length);
                            if (verbose > 1)
                                dump("UDP data", net_parser.buffer, net_parser.length);
                        }
                    }
                }
            } else {
                perror("Error reading from UDP");
                break;
            }
        }
    }
    if (!run) {
        printf("Connection closed by user.\n");
    }
    close(udp_sock);
}

void process_connection(int uart_fd, int udp_sock, const char *ip_addr, uint16_t udp_port)
{
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;
    struct sockaddr_in from, to;
    socklen_t from_len;

    struct pollfd fds[2];
    fds[0].fd = uart_fd;
    fds[0].events = POLLIN;
    fds[1].fd = udp_sock;
    fds[1].events = POLLIN;

    to.sin_family = AF_INET;
    to.sin_port = htons(udp_port);
    if (inet_pton(AF_INET, ip_addr, &to.sin_addr) <= 0)
    {
        perror("Invalid IP address");
        close(udp_sock);
        return;
    }
    tcflush(uart_fd, TCIOFLUSH);

    if (verbose)
        printf("Waiting for data...\n");
    while (run) {
        int ret = poll(fds, 2, 1000);
        if (ret < 0) {
            perror("Error polling");
            break;
        }
        
        if (ret == 0) {
            continue;
        }

        if (fds[0].revents & POLLIN) {
            bytes_read = read(uart_fd, buffer, sizeof(buffer));
            if (bytes_read > 0) {
                if (sendto(udp_sock, buffer, bytes_read, 0, (struct sockaddr *)&to, sizeof(to)) < 0)
                {
                    perror("Error sending over UDP");
                    continue;
                }
                if (verbose) {
                    printf("UART -> UDP: sent %zd bytes\n", bytes_read);
                    if (verbose > 1 )
                        dump("UART data", buffer, bytes_read);
                }
            } else if (bytes_read == 0) {
                printf("UART closed the connection.\n");
                break;
            } else {
                perror("Error reading from UART");
                break;
            }
        }

        if (fds[1].revents & POLLIN) {
            bytes_read = recvfrom(udp_sock, buffer, sizeof(buffer), 0,
                                  (struct sockaddr *)&from, &from_len);
            if (bytes_read > 0) {
                if (write(uart_fd, buffer, bytes_read) < 0) {
                    perror("Error sending over UART");
                    break;
                }
                if (verbose) {
                    printf("UDP(from %s) -> UART: sent %zd bytes\n",
                           inet_ntoa(from.sin_addr), bytes_read);
                    if (verbose > 1)
                        dump("UDP data", buffer, bytes_read);
                }
            } else {
                perror("Error reading from UDP");
                break;
            }
        }
    }
    if (!run) {
        printf("Connection closed by user.\n");
    }
    close(udp_sock);
}

int main_loop(char *peer_ip, uint16_t udp_port, int uart_fd)
{
    struct sockaddr_in sock_addr;
    int udp_sock;
    int rc = 0;

    while (run) {
        udp_sock = socket(AF_INET, SOCK_DGRAM, 0);
        sock = &udp_sock;
        if (udp_sock < 0) {
            perror("Socket creation error");
            close(uart_fd);
            return -1;
        }
        int flags = fcntl(udp_sock, F_GETFL, 0);
        if (flags != -1) {
            flags |= O_NONBLOCK;
            if (fcntl(udp_sock, F_SETFL, flags) == -1)
                perror("UDP: Set non-blocking mode");
        } else {
            perror("UDP: Get descriptor flags");
        }
        int tos = IPTOS_EF;
        // Setup Expedited Forwarding (EF).
        if (setsockopt(udp_sock, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)) < 0)
            perror("setsockopt IP_TOS failed");
        int priority = 6;

        if (setsockopt(udp_sock, SOL_SOCKET, SO_PRIORITY, &priority, sizeof(priority)) < 0)
            perror("setsockopt SO_PRIORITY failed");
        sock_addr.sin_family = AF_INET;
        sock_addr.sin_port = htons(udp_port);
        sock_addr.sin_addr.s_addr = INADDR_ANY;
        rc = bind(udp_sock, (const struct sockaddr *)&sock_addr, sizeof(sock_addr));
        if (rc < 0) {
            perror("Error binding UDP socket");
            close(udp_sock);
            run = 0;
            break;
        }
        process_connection_func(uart_fd, udp_sock, peer_ip, udp_port);
        close(udp_sock);
    }
    if (verbose)
        printf("Client stopped\n");
    sock = NULL;
    return rc;
}

int main(int argc, char *argv[])
{
    int uart_fd;
    int opt;
    char uart_device[255];
    uint16_t udp_port = UDP_PORT;
    int baud_rate = BAUD_RATE;
    int ret;
    uint8_t *cbuffers;

    static struct option long_options[] = {
        {"uart", required_argument, NULL, 'u'},
        {"baudrate", required_argument, NULL, 'b'},
        {"tcp-port", required_argument, NULL, 'p'},
        {"tx mode", no_argument, NULL, 't'},
        {"diag", no_argument, NULL, 'd'},
        {"help", no_argument, NULL, 'h'},
        {"version", no_argument, NULL, 'V'},
        {"verbose", no_argument, NULL, 'v'},
        {NULL, 0, NULL, 0}
    };
    int long_index = 0;
    strncpy(uart_device, UART_DEVICE, sizeof(uart_device) - 1);
    while ((opt = getopt_long(argc, argv, "vVhu:p:b:td", long_options, &long_index)) != -1) {
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
            case 'd': // Diagnostic
                diagnostic = 1;
                break;
            case 'u': // UART device
                strncpy(uart_device, optarg, sizeof(uart_device) - 1);
                printf("UART device set to: %s\n", optarg);
                break;
            case 'p': // UDP port
                udp_port = atoi(optarg);
                printf("UDP port set to: %d\n", udp_port);
                break;
            case 'b': // Baud rate
                baud_rate = atoi(optarg);
                printf("Baud rate set to: %d\n", baud_rate);
                break;
            case 't': // TX mode
                tx_mode = 1;
                printf("TX mode enabled.\n");
                process_connection_func = process_connection_tx;
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

    char *peer_ip = argv[optind];

    if (!process_connection_func) {
        process_connection_func = process_connection;
    }
    parser_init(&net_parser);
    parser_init(&uart_parser);
    sock = NULL;
    if (tx_mode) {
        printf("Starting in TX mode\n");
        init_shared(DEFAULT_SHARED_NAME, &shm);
    }

    net_parser.errs = 0; net_parser.packets = 0;
    uart_parser.errs = 0; uart_parser.packets = 0;

    cbuffers = (uint8_t *)malloc(CRSF_MAX_PACKET_SIZE * CBUF_NUM * sizeof(uint8_t));
    cbuf_init(&cbuf, cbuffers, CBUF_NUM, CRSF_MAX_PACKET_SIZE * sizeof(uint8_t));

    uart_fd = setup_uart(uart_device, baud_rate);
    if (uart_fd < 0) {
        fprintf(stderr, "Error configuring UART.\n");
        return -1;
    }
    signal(SIGINT, sigint_handler);
    run = 1;
    ret = main_loop(peer_ip, udp_port, uart_fd);
    printf("Exiting...\n");
    if (tx_mode)
        deinit_shared(&shm);
    free(cbuffers);
    close(uart_fd);
    return ret;
}
