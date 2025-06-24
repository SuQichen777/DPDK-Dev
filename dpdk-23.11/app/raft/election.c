#include "election.h"
#include "networking.h"
#include <stdio.h>
#include <string.h>
#include <rte_timer.h>

#define ELECTION_TIMEOUT_MS 1500

static void broadcast_raft_packet(struct raft_packet *pkt) {
    for (uint32_t peer = 1; peer <= NUM_NODES; peer++) {
        if (peer == self_id) continue;
        send_raft_packet(pkt, peer);
    }
}

void raft_init(uint32_t id) {
    self_id = id;
    current_state = STATE_FOLLOWER;
    current_term = 0;
    voted_for = 0;
    vote_granted = 0;
    last_heard_ms = 0;
    printf("Raft init: node_id=%u\n", self_id);
    init_timers();
}

void raft_tick(uint64_t now_ms) {
    if (current_state == STATE_LEADER) return;
    
    uint64_t elapsed_ms = now_ms - last_heard_ms;
    if (elapsed_ms > ELECTION_TIMEOUT_MS) {
        current_state = STATE_CANDIDATE;
        current_term++;
        voted_for = self_id;
        vote_granted = 1;
        last_heard_ms = now_ms;

        printf("Node %u starting election for term %u\n", self_id, current_term);

        struct raft_packet pkt = {
            .msg_type = MSG_VOTE_REQUEST,
            .term = current_term,
            .node_id = self_id,
            .rtt_ms = 0,
        };
        broadcast_raft_packet(&pkt);
    }
}

void raft_handle_packet(const struct raft_packet * pkt, uint16_t port) {
    if (pkt->term > current_term) {
        current_term = pkt->term;
        current_state = STATE_FOLLOWER;
        voted_for = 0;
    }

    switch (pkt->msg_type) {
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
            send_raft_packet(&resp, pkt->node_id);
            printf("Node %u granted vote to %u in term %u\n",
                   self_id, pkt->node_id, current_term);
        }
        break;

    case MSG_VOTE_RESPONSE:
        if (current_state == STATE_CANDIDATE && pkt->term == current_term) {
            vote_granted++;
            printf("Node %u received vote from %u (total: %u/%u)\n",
                   self_id, pkt->node_id, vote_granted, NUM_NODES);
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
            printf("Node %u received heartbeat from leader %u\n", 
                   self_id, pkt->node_id);
        }
        break;
    }
}

void raft_send_heartbeat(void) {
    if (current_state != STATE_LEADER) return;
    
    struct raft_packet pkt = {
        .msg_type = MSG_HEARTBEAT,
        .term = current_term,
        .node_id = self_id,
        .rtt_ms = 0,
    };
    broadcast_raft_packet(&pkt);
}

// TODO
static void election_timeout_cb(__rte_unused struct rte_timer *timer, __rte_unused void *arg) {
    uint64_t now_ms = rte_get_timer_cycles() * 1000 / rte_get_timer_hz();
    raft_tick(now_ms);
}

// TODO
void init_timers(void) {
    rte_timer_init(&election_timer);
    rte_timer_reset(&election_timer, 
                   rte_get_timer_hz() * ELECTION_TIMEOUT_MS / 1000,
                   PERIODICAL, 
                   rte_lcore_id(), 
                   election_timeout_cb, 
                   NULL);
}