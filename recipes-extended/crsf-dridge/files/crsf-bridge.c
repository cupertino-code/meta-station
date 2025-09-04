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
#include "circ_buf.h"
#include "crsf_protocol.h"

#define UART_DEVICE "/dev/ttyS0"
#define BAUD_RATE 115200

#define TCP_SERVER_IP "192.168.1.100"
#define TCP_SERVER_PORT 7300

#define BUFFER_SIZE 256

#define VERSION "1.0"

#define MODE_DEFAULT 0
#define MODE_CLIENT 1
#define MODE_SERVER 2

int verbose = 0;
int tx_mode = 0; // TX mode flag
int diagnostic = 0;

typedef void (*process_func_t)(int uart_fd, int tcp_sock);

process_func_t process_connection_func = NULL;

#define STATE_SOURCE    0
#define STATE_LENGTH    1
#define STATE_PAYLOAD   2

struct parser_state {
    int state;
    int length;
    int expected_length;
    uint64_t packets;   // parsed packets
    uint64_t errs;      // unparsed packets
    uint8_t buffer[CRSF_MAX_PACKET_SIZE];
};

struct parser_state tcp_parser;
struct parser_state uart_parser;
struct circular_buf cbuf;
struct cbuf_item {
    uint8_t buf[CRSF_MAX_PACKET_SIZE];
    size_t len;
};

#define CBUF_NUM 3

#define MAYBE_UNUSED __attribute__((unused))
void help()
{
    printf("Usage: connector -s | -c <server IP> [options]\n");
    printf("Options:\n");
    printf("  -u, --uart <device>       Set UART device (default: %s)\n", UART_DEVICE);
    printf("  -c, --client <server IP>  Run in client mode. Need server IP (default: %s)\n", TCP_SERVER_IP);
    printf("  -s, --server              Run in server mode\n");
    printf("  -p, --tcp-port <port>     Set TCP port (default: %d)\n", TCP_SERVER_PORT);
    printf("  -b, --baudrate <rate>     Set UART baud rate (default: %d)\n", BAUD_RATE);
    printf("  -t, --tx mode             Enable TX mode\n");
    printf("  -v, --verbose             Increase verbosity level (can be used multiple times)\n");
    printf("  -d, --diag                Output diagnostic data (packet counters)\n");
    printf("  -h, --help                Show this help message\n");
    printf("  -V, --version             Show version information\n");
}

void dump(const char *prefix, const char *data, size_t len)
{
    if (verbose) {
        printf("%s: ", prefix);
        for (size_t i = 0; i < len; i++) {
            printf("%02x ", (unsigned char)data[i]);
        }
        printf("\n");
    }
}

volatile int run;

void sigint_handler(int sig) {
    printf("\nGot signal SIGINT (Ctrl+C). Exiting...\n");
    run = 0;
}

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
    if (verbose)
        printf("UART configured on %s with baud rate %d\n", device, baud_rate);
    return uart_fd;
}

void parser_init(struct parser_state *parser)
{
    parser->state = STATE_SOURCE;
    parser->length = 0;
    parser->expected_length = 0;
}

int parser(struct parser_state *parser, uint8_t byte)
{
    struct timespec ts;
    uint64_t timestamp;
    static uint64_t last_timestamp = 0;

    clock_gettime(CLOCK_MONOTONIC, &ts);
    timestamp = ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    if (last_timestamp) {
        if (parser->state != STATE_SOURCE && (timestamp - last_timestamp) > 1) {
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
            parser->state = STATE_PAYLOAD;
            break;
        case STATE_PAYLOAD:
            parser->buffer[parser->length++] = byte;
            if (parser->length >= parser->expected_length) {
                // Message complete
                parser->state = STATE_SOURCE;
                parser->packets++;
                return parser->length; // Return length of complete message
            }
            break;
        default:
            parser->state = STATE_SOURCE; // Reset state on error
            parser->errs++;
            break;
    }
    return 0; // Message not complete yet
}
void process_connection_tx(int uart_fd, int tcp_sock)
{
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    struct pollfd fds[2];
    fds[0].fd = uart_fd;
    fds[0].events = POLLIN;
    fds[1].fd = tcp_sock;
    fds[1].events = POLLIN;

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
            printf("TCP  packets: %lu errors: %lu\n", tcp_parser.packets, tcp_parser.errs);
            printf("UART packets: %lu errors: %lu\r\033[A", uart_parser.packets, uart_parser.errs);
        }
        if (ret == 0) {
            continue;
        }

        if (fds[0].revents & POLLIN) {
            bytes_read = read(uart_fd, buffer, sizeof(buffer));
            if (bytes_read > 0) {
                for (int i = 0; i < bytes_read; i++)
                    if (parser(&uart_parser, buffer[i])) {
                        if (write(tcp_sock, uart_parser.buffer, uart_parser.length) < 0) {
                            perror("Error sending over TCP");
                            continue;
                        }
                        if (verbose) {
                            printf("UART -> TCP: sent %d bytes\n", uart_parser.length);
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
            } else if (bytes_read == 0) {
                printf("UART closed the connection.\n");
                break;
            } else {
                perror("Error reading from UART");
                break;
            }
        }

        if (fds[1].revents & POLLIN) {
            bytes_read = read(tcp_sock, buffer, sizeof(buffer));
            if (bytes_read > 0) {
                for (int i = 0; i < bytes_read; i++) {
                    int len = parser(&tcp_parser, buffer[i]);
                    if (len > 0)
                    {
                        cbuf_put(&cbuf, tcp_parser.buffer);
                        if (verbose)
                        {
                            printf("TCP: received %d bytes\n", tcp_parser.length);
                            if (verbose > 1)
                                dump("TCP data", tcp_parser.buffer, tcp_parser.length);
                        }
                    }
                }
            } else if (bytes_read == 0) {
                printf("TCP peer closed the connection.\n");
                break;
            } else {
                perror("Error reading from TCP");
                break;
            }
        }
    }
    if (!run) {
        printf("Connection closed by user.\n");
    }
    close(tcp_sock);
}

void process_connection(int uart_fd, int tcp_sock)
{
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

    struct pollfd fds[2];
    fds[0].fd = uart_fd;
    fds[0].events = POLLIN;
    fds[1].fd = tcp_sock;
    fds[1].events = POLLIN;

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
                if (write(tcp_sock, buffer, bytes_read) < 0) {
                    perror("Error sending over TCP");
                    break;
                }
                if (verbose) {
                    printf("UART -> TCP: sent %zd bytes\n", bytes_read);
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
            bytes_read = read(tcp_sock, buffer, sizeof(buffer));
            if (bytes_read > 0) {
                if (write(uart_fd, buffer, bytes_read) < 0) {
                    perror("Error sending over UART");
                    break;
                }
                if (verbose) {
                    printf("TCP -> UART: sent %zd bytes\n", bytes_read);
                    if (verbose > 1)
                        dump("TCP data", buffer, bytes_read);
                }
            } else if (bytes_read == 0) {
                printf("TCP peer closed the connection.\n");
                break;
            } else {
                perror("Error reading from TCP");
                break;
            }
        }
    }
    if (!run) {
        printf("Connection closed by user.\n");
    }
    close(tcp_sock);
}

int start_server(int tcp_port, int uart_fd)
{
    int server_sock, client_sock;
    struct sockaddr_in server_addr, client_addr;
    socklen_t addr_len = sizeof(client_addr);
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read;

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

    while (run) {
        client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);
        if (client_sock < 0) {
            perror("Error accepting connection");
            continue;
        }
        if (verbose)
            printf("Client connected\n");

        process_connection_func(uart_fd, client_sock);

        close(client_sock);
        if (verbose)
            printf("Client disconnected\n");
    }

    close(server_sock);
    printf("Server stopped\n");
}

int start_client(char *server_ip, uint16_t tcp_port, int uart_fd)
{
    struct sockaddr_in server_addr;
    int tcp_sock;

    while (run) {
        tcp_sock = socket(AF_INET, SOCK_STREAM, 0);
        if (tcp_sock < 0) {
            perror("Socket creation error");
            close(uart_fd);
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
        if (verbose)
            printf("Connected to server %s:%d\n", server_ip, tcp_port);
        process_connection_func(uart_fd, tcp_sock);
        close(tcp_sock);
    }
    if (verbose)
        printf("Client stopped\n");
}

int main(int argc, char *argv[])
{
    int uart_fd;
    int opt;
    char uart_device[255];
    uint16_t tcp_port = TCP_SERVER_PORT;
    int baud_rate = BAUD_RATE;
    char server_ip[255];
    int mode = MODE_DEFAULT; // MODE_DEFAULT: default, MODE_CLIENT: client, MODE_SERVER: server
    int ret;
    uint8_t *cbuffers;

    static struct option long_options[] = {
        {"uart", required_argument, NULL, 'u'},
        {"baudrate", required_argument, NULL, 'b'},
        {"client", required_argument, NULL, 'c'},
        {"server", no_argument, NULL, 's'},
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
    strncpy(server_ip, TCP_SERVER_IP, sizeof(server_ip) - 1);
    while ((opt = getopt_long(argc, argv, "vVhu:c:sp:b:td", long_options, &long_index)) != -1) {
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
            case 'c': // Client mode
                if (mode == MODE_SERVER) {
                    fprintf(stderr, "ERROR: Can't use client mode with server mode.\n");
                    return 1;
                }
                printf("Client mode enabled. Server IP: %s\n", optarg);
                strncpy(server_ip, optarg, sizeof(server_ip) - 1);
                mode = MODE_CLIENT;
                break;
            case 's': // Server mode
                if (mode == MODE_CLIENT) {
                    fprintf(stderr, "ERROR: Can't use server mode with client mode.\n");
                    return 1;
                }
                printf("Server mode enabled.\n");
                mode = MODE_SERVER;
                break;
            case 'p': // TCP port
                tcp_port = atoi(optarg);
                printf("TCP port set to: %d\n", tcp_port);
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
    if (mode == MODE_DEFAULT)
        mode = MODE_SERVER;

    if (!process_connection_func) {
        process_connection_func = process_connection;
    }
    parser_init(&tcp_parser);
    parser_init(&uart_parser);

    tcp_parser.errs = 0; tcp_parser.packets = 0;
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
    if (mode == MODE_SERVER) {
        ret = start_server(tcp_port, uart_fd);
    } else if (mode == MODE_CLIENT) {
        ret = start_client(server_ip, tcp_port, uart_fd);
    }
    printf("Exiting...\n");
    close(uart_fd);
    return ret;
}
