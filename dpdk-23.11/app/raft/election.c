#include "election.h"
#include <stdio.h>
#include <string.h>

#define ELECTION_TIMEOUT_MS 1500 // TODO
#define NUM_NODES 3  // Number of nodes in the cluster

static raft_state_t current_state = STATE_FOLLOWER; // initial state is follower
static uint32_t current_term = 0;
static uint32_t voted_for = 0;
static uint32_t self_id = 0;

static uint64_t last_heard_ms = 0;
static uint32_t vote_granted = 0;

// The Raft packet structure in packet.h

// #define MSG_VOTE_REQUEST   1
// #define MSG_VOTE_RESPONSE  2
// #define MSG_HEARTBEAT      3

// struct raft_packet {
//     uint8_t  msg_type;   // defined above
//     uint32_t term;       // current term
//     uint32_t node_id;    // this node's ID
//     uint32_t rtt_ms;     // RTT(optional)
// } __attribute__((packed));

static void send_raft_packet(struct raft_packet * pkt) {
    printf("SEND: type=%u, term=%u, from=%u\n",
           pkt->msg_type, pkt->term, pkt->node_id);
    // TODO: DPDK implementation for sending packet
}

void raft_init(uint32_t id) {
    self_id = id;
    current_state = STATE_FOLLOWER;
    current_term = 0;
    voted_for = 0;
    vote_granted = 0;
    last_heard_ms = 0;
    printf("Raft init: node_id=%u\n", self_id);
}

void raft_tick(uint64_t now_ms) {
    // if current state is LEADER, no need to check for election
    if (current_state == STATE_LEADER) return;
    uint64_t elapsed_ms = now_ms - last_heard_ms;
    if (elapsed_ms > ELECTION_TIMEOUT_MS) { // timeout
        current_state = STATE_CANDIDATE;
        current_term++;
        voted_for = self_id;
        vote_granted = 1;
        last_heard_ms = now_ms;

        printf("Node %u starting election for term %u\n", self_id, current_term);

        // broadcast vote request to other nodes
        for (uint32_t peer = 1; peer <= NUM_NODES; peer++) {
            if (peer == self_id) continue;
            struct raft_packet pkt = {
                .msg_type = MSG_VOTE_REQUEST,
                .term = current_term,
                .node_id = self_id,
                .rtt_ms = 0, //TODO: when netlink is completed
            };
            send_raft_packet(&pkt);
        }
    }
}

void raft_handle_packet(const struct raft_packet * pkt, uint16_t port) {
    // pkt: pointer to the received packet

    // if received packet is from a larger term, update state
    // to follower and reset vote, update term to the larger term
    if (pkt->term > current_term) {
        current_term = pkt->term;
        current_state = STATE_FOLLOWER;
        voted_for = 0;
    }

    switch (pkt->msg_type) {
    // if received packet is a vote request
    case MSG_VOTE_REQUEST:
        if (current_state == STATE_FOLLOWER &&
            (voted_for == 0 || voted_for == pkt->node_id)) {
            voted_for = pkt->node_id;
            last_heard_ms = 0;

            struct raft_packet resp = {
                .msg_type = MSG_VOTE_RESPONSE,
                .term = current_term,
                .node_id = self_id,
                .rtt_ms = 0,
            };
            send_raft_packet(&resp);
            printf("Node %u granted vote to %u in term %u\n",
                   self_id, pkt->node_id, current_term);
        }
        // if current state is candidate
        // or have already voted for another candidate,
        // do not grant vote
        break;

    case MSG_VOTE_RESPONSE:
        if (current_state == STATE_CANDIDATE &&
            pkt->term == current_term) {
            vote_granted++; // all the votes this candidate has received
            printf("Node %u received vote in term %u from %u\n, and the total num is %u\n",
                   self_id, current_term, pkt->node_id, vote_granted);
            if (vote_granted > NUM_NODES / 2) {
                current_state = STATE_LEADER;
                printf("Node %u elected as leader in term %u\n",
                       self_id, current_term);
            }
        }
        break;

    case MSG_HEARTBEAT:
        if (pkt->term >= current_term) {
            current_state = STATE_FOLLOWER;
            current_term = pkt->term;
            last_heard_ms = 0;
        }
        break;

    default:
        break;
    }
}

raft_state_t raft_get_state(void) {
    return current_state;
}

void raft_send_heartbeat(void) {
    if (current_state != STATE_LEADER) return;
    // only leader sends heartbeat
    for (uint32_t peer = 1; peer <= NUM_NODES; peer++) {
        if (peer == self_id) continue;
        struct raft_packet pkt = {
            .msg_type = MSG_HEARTBEAT,
            .term = current_term,
            .node_id = self_id,
            .rtt_ms = 0,
        };
        send_raft_packet(&pkt);
    }
}
