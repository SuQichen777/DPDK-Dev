//include/packet.h
#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>
#define RAFT_PORT 9999
#define RAFT_PACKET_SIZE sizeof(struct raft_packet)

// three message types for Raft protocol
#define MSG_VOTE_REQUEST   1
#define MSG_VOTE_RESPONSE  2
#define MSG_HEARTBEAT      3

// Additional message types for RTT testing and PS broadcast
#define MSG_PING_RTT       4
#define MSG_PONG_RTT       5
#define MSG_PS_BROADCAST   6

struct raft_packet {
    uint8_t  msg_type;   // defined above
    uint32_t term;       // current term
    uint32_t node_id;    // this node's ID
} __attribute__((packed));

// RTT ping packet structure
struct rtt_ping_packet {
    uint8_t  msg_type;   // MSG_PING_RTT
    uint32_t src_id;     // source node ID
    uint64_t send_ts;    // timestamp when packet was sent
    uint64_t tsc_hz;     // TSC frequency for time conversion
} __attribute__((packed));

// RTT pong packet structure
struct rtt_pong_packet {
    uint8_t  msg_type;   // MSG_PONG_RTT
    uint32_t dst_id;     // destination node ID
    uint64_t echoed_ts;  // echoed timestamp from ping
    uint64_t tsc_hz;     // TSC frequency
} __attribute__((packed));

// PS broadcast packet structure
struct ps_broadcast_packet {
    uint8_t  msg_type;   // MSG_PS_BROADCAST
    uint32_t node_id;    // sender node ID
    uint64_t tx_ts;      // transmission timestamp
    uint32_t remote_hz;  // remote TSC frequency
    double   penalty;    // penalty value
} __attribute__((packed));

#endif // PACKET_H