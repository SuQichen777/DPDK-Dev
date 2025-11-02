#pragma once
#include <stdint.h>

#define SENSE_PORT 9998

#define MSG_PING_RTT  10
#define MSG_PONG_RTT  11

struct sense_ping_packet {
    uint8_t  msg_type;     // MSG_PING_RTT
    uint32_t src_id;       // sender node id
    uint64_t send_ts;      // tsc cycles at send
    uint64_t tsc_hz;       // tsc frequency
} __attribute__((packed));

struct sense_pong_packet {
    uint8_t  msg_type;     // MSG_PONG_RTT
    uint32_t dst_id;       // intended receiver node id (echo back)
    uint32_t src_id;       // actual sender node id
    uint64_t echoed_ts;    // same as send_ts in ping
    uint64_t tsc_hz;       // echo tsc frequency
} __attribute__((packed));