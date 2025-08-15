#ifndef _PROTOCOL_H_INCLUDED
#define _PROTOCOL_H_INCLUDED

#include <stdint.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
struct rotator_protocol {
    uint8_t version;      // Protocol version
    uint8_t type;         // Message type
    uint16_t length;      // Length of the payload
    uint32_t timestamp;   // Timestamp of the message
    uint8_t payload[0];   // Payload data
};
#pragma GCC diagnostic pop

struct rotator_command {
    int32_t position;     // Current position of the rotator
    int32_t switches;     // Switch status on the station
};

struct rotator_status {
    int16_t angle;  // Current angle of the antenna
    uint8_t status; // Status flags (e.g., error states)
};

#define PROTOCOL_VERSION 1

// Message types
#define MESSAGE_TYPE_STATUS 0x01
#define MESSAGE_TYPE_COMMAND 0x02

// Status
#define STATUS_POWERED  0x01
#define STATUS_SHUTDOWN 0x00

// Switches
#define SWITCH_ANTENNA_NUM  0 // Antenna switch
#define SWITCH_ENCODER_NUM  1 // Encoder switch

#define SWITCH_ANTENNA  BIT(SWITCH_ANTENNA_NUM) // Antenna switch
#define SWITCH_ENCODER  BIT(SWITCH_ENCODER_NUM) // Encoder switch
#define SWITCH_NUM 2

uint8_t crc8_data(const uint8_t *data, int len);

#undef offsetof
#define offsetof(TYPE, MEMBER) __builtin_offsetof(TYPE, MEMBER)
#define container_of(ptr, type, member) \
    ((type *)((char *)(ptr) - offsetof(type, member)))
#endif // _PROTOCOL_H_INCLUDED