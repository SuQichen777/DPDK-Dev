//include/packet.h
#ifndef PACKET_H
#define PACKET_H

#include <stdint.h>
#define RAFT_PORT 9999
#define RAFT_PACKET_SIZE sizeof(struct raft_packet)
#define NUM_NODES 7 //TODO: Set the number of nodes in the cluster

// three message types for Raft protocol
#define MSG_VOTE_REQUEST   1
#define MSG_VOTE_RESPONSE  2
#define MSG_HEARTBEAT      3

struct raft_packet {
    uint8_t  msg_type;   // defined above
    uint32_t term;       // current term
    uint32_t node_id;    // this node's ID
} __attribute__((packed));

#endif // PACKET_H