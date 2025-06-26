// election.c
#include "election.h"
#include "networking.h"
#include <stdio.h>
#include <string.h>
#include <rte_timer.h>
#include "timeout.h"
#include "config.h"

// typedef struct {
//     uint32_t self_id;
//     uint32_t current_term;
//     uint32_t voted_for;
//     uint32_t vote_granted;
//     raft_state_t current_state;
//     uint64_t last_heard_ms;
// } raft_node_t;

static raft_node_t raft_node;
static struct rte_timer election_timer;
static void election_timeout_cb(struct rte_timer *t, void *arg);

static void broadcast_raft_packet(struct raft_packet *pkt)
{
    for (uint32_t peer = 1; peer <= NUM_NODES; peer++)
    {
        if (peer == raft_node.self_id)
            continue;
        send_raft_packet(pkt, peer);
    }
}

void raft_init(uint32_t id)
{
    memset(&raft_node, 0, sizeof(raft_node));
    raft_node.self_id = id;
    raft_node.current_state = STATE_FOLLOWER;
    raft_node.current_term = 0;
    raft_node.voted_for = 0;
    raft_node.vote_granted = 0;
    raft_node.last_heard_ms = 0;
    printf("Raft init: node_id=%u\n", raft_node.self_id);
    timeout_init(global_config.election_timeout_min_ms, global_config.election_timeout_max_ms);
    timeout_start_election(&election_timer, election_timeout_cb, NULL);
}

static void start_election(uint64_t now_ms)
{

    raft_node.current_state = STATE_CANDIDATE;
    raft_node.current_term++;
    raft_node.voted_for = raft_node.self_id;
    raft_node.vote_granted = 1;
    raft_node.last_heard_ms = now_ms;

    printf("Node %u starting election for term %u\n", raft_node.self_id, raft_node.current_term);

    struct raft_packet pkt = {
        .msg_type = MSG_VOTE_REQUEST,
        .term = raft_node.current_term,
        .node_id = raft_node.self_id,
        .rtt_ms = 0,
    };
    broadcast_raft_packet(&pkt);
    timeout_start_election(&election_timer, election_timeout_cb, NULL);
}

void raft_handle_packet(const struct raft_packet *pkt, uint16_t port)
{
    (void)port;

    uint64_t now_ms = rte_get_timer_cycles() * 1000 / rte_get_timer_hz();
    if (pkt->term > raft_node.current_term)
    {
        raft_node.current_term = pkt->term;
        raft_node.current_state = STATE_FOLLOWER;
        raft_node.voted_for = 0;
        timeout_start_election(&election_timer,
                        election_timeout_cb,
                        NULL);
    }

    switch (pkt->msg_type)
    {
    case MSG_VOTE_REQUEST:
        if (raft_node.current_state == STATE_FOLLOWER &&
            (raft_node.voted_for == 0 || raft_node.voted_for == pkt->node_id))
        {
            raft_node.voted_for = pkt->node_id;
            raft_node.last_heard_ms = now_ms;
            timeout_start_election(&election_timer,
                       election_timeout_cb,
                       NULL);

            struct raft_packet resp = {
                .msg_type = MSG_VOTE_RESPONSE,
                .term = raft_node.current_term,
                .node_id = raft_node.self_id,
                .rtt_ms = 0,
            };
            send_raft_packet(&resp, pkt->node_id);
            printf("Node %u granted vote to %u in term %u\n",
                   raft_node.self_id, pkt->node_id, raft_node.current_term);
        }
        break;

    case MSG_VOTE_RESPONSE:
        if (raft_node.current_state == STATE_CANDIDATE && pkt->term == raft_node.current_term)
        {
            raft_node.vote_granted++;
            printf("Node %u received vote from %u (total: %u/%u)\n",
                   raft_node.self_id, pkt->node_id, raft_node.vote_granted, NUM_NODES);
            if (raft_node.vote_granted > NUM_NODES / 2)
            {
                raft_node.current_state = STATE_LEADER;
                raft_send_heartbeat(); 
                if (raft_node.vote_granted > NUM_NODES) raft_node.vote_granted = NUM_NODES;
                printf("Node %u elected as leader in term %u\n",
                       raft_node.self_id, raft_node.current_term);
                timeout_stop(&election_timer); 
            }
        }
        break;

    case MSG_HEARTBEAT:
        if (pkt->term >= raft_node.current_term)
        {
            raft_node.current_state = STATE_FOLLOWER;
            raft_node.current_term = pkt->term;
            raft_node.last_heard_ms = now_ms;

            timeout_start_election(&election_timer,
                                   election_timeout_cb,
                                   NULL);
            printf("Node %u received heartbeat from leader %u\n",
                   raft_node.self_id, pkt->node_id);
        }
        break;
    }
}

void raft_send_heartbeat(void)
{
    if (raft_node.current_state != STATE_LEADER)
        return;

    struct raft_packet pkt = {
        .msg_type = MSG_HEARTBEAT,
        .term = raft_node.current_term,
        .node_id = raft_node.self_id,
        .rtt_ms = 0,
    };
    broadcast_raft_packet(&pkt);
}

uint32_t raft_get_node_id(void)
{
    return raft_node.self_id;
}
uint32_t raft_get_term(void)
{
    return raft_node.current_term;
}
raft_state_t raft_get_state(void)
{
    return raft_node.current_state;
}
static void election_timeout_cb(struct rte_timer *t, void *arg)
{
    (void)t; 
    (void)arg;
    uint64_t now_ms = rte_get_timer_cycles() * 1000 / rte_get_timer_hz();
    if (raft_node.current_state != STATE_LEADER)
        start_election(now_ms);
}