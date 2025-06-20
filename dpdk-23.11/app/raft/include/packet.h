#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>

// three message types for Raft protocol
#define MSG_VOTE_REQUEST   1
#define MSG_VOTE_RESPONSE  2
#define MSG_HEARTBEAT      3

// will be used in election and heartbeat messages
struct raft_packet {
    uint8_t  msg_type;   // defined above
    uint32_t term;       // current term
    uint32_t node_id;    // this node's ID
    uint32_t rtt_ms;     // RTT(optional)
} __attribute__((packed));

#endif // PACKET_H
