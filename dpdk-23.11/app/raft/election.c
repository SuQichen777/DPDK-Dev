// election.c
#include "election.h"
#include "networking.h"
#include <stdio.h>
#include <string.h>
#include <rte_timer.h>
#include <rte_cycles.h>
#include <rte_time.h>
#include "timeout.h"
#include "config.h"

// typedef struct {
//     uint32_t self_id;
//     uint32_t current_term;
//     uint32_t voted_for;
//     uint32_t vote_granted;
//     raft_state_t current_state;
//     uint64_t last_heard_µs;
// } raft_node_t;
static int test_auto_fail_enabled = 0;
static uint64_t election_start_time = 0;
static raft_node_t raft_node;
static struct rte_timer election_timer;
static void election_timeout_cb(struct rte_timer *t, void *arg);

/* Implementing Test Auto Fail */
static struct rte_timer fail_timer;    /* trigger auto-fail */
static struct rte_timer recover_timer; /* trigger auto-recovery */
static void test_auto_fail_enable(int enabled)
{
    test_auto_fail_enabled = enabled;
    printf("Test auto-fail %s\n", enabled ? "enabled" : "disabled");
}

static void fail_disable_cb(__rte_unused struct rte_timer *t, void *arg)
{
    (void)arg;
    test_auto_fail_enable(0);
}

static void fail_enable_cb(__rte_unused struct rte_timer *t, void *arg)
{
    (void)arg;
    test_auto_fail_enable(1);
    uint64_t cycles = (uint64_t)global_config.test_auto_fail_duration_ms *
                      rte_get_timer_hz() / 1000;
    rte_timer_init(&recover_timer);
    rte_timer_reset(&recover_timer, cycles, SINGLE, rte_lcore_id(),
                    fail_disable_cb, NULL);
}

/* End of Test Auto Fail */

/* Timer for microsecond */
static inline uint64_t monotonic_us(void)
{
    return rte_get_timer_cycles() * 1000000ULL / rte_get_timer_hz();
}
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
    raft_node.last_heard_µs = 0;
    printf("Raft init: node_id=%u\n", raft_node.self_id);
    timeout_init(global_config.election_timeout_min_ms, global_config.election_timeout_max_ms);
    timeout_start_election(&election_timer, election_timeout_cb, NULL);
}
static void start_election()
{

    raft_node.current_state = STATE_CANDIDATE;
    raft_node.current_term++;
    raft_node.voted_for = raft_node.self_id;
    raft_node.vote_granted = 1;
    raft_node.last_heard_µs = monotonic_us();

    printf("Node %u starting election for term %u\n", raft_node.self_id, raft_node.current_term);

    struct raft_packet pkt = {
        .msg_type = MSG_VOTE_REQUEST,
        .term = raft_node.current_term,
        .node_id = raft_node.self_id,
    };
    broadcast_raft_packet(&pkt);
    timeout_start_election(&election_timer, election_timeout_cb, NULL);
}
void raft_handle_packet(const struct raft_packet *pkt, uint16_t port)
{
    (void)port;
    if (test_auto_fail_enabled)
        return; // Suppose this node is down for testing
    uint64_t now_µs = monotonic_us();
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
            raft_node.last_heard_µs = now_µs;
            timeout_start_election(&election_timer,
                                   election_timeout_cb,
                                   NULL);

            struct raft_packet resp = {
                .msg_type = MSG_VOTE_RESPONSE,
                .term = raft_node.current_term,
                .node_id = raft_node.self_id,
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
                uint64_t elect_time = monotonic_us();
                printf("[RAFT] Node %u: Elected as leader, T_elect = %lu µs, [Election latency] is %lu µs\n",
                       raft_node.self_id, elect_time, elect_time - election_start_time);
                FILE *fp = fopen("failover_stats.csv", "a");
                fprintf(fp, "elect ,%lu,%lu,%lu\n",
                        raft_node.self_id,
                        elect_time,
                        elect_time - election_start_time);
                fclose(fp);

                raft_send_heartbeat();
                if (raft_node.vote_granted > NUM_NODES)
                    raft_node.vote_granted = NUM_NODES;
                timeout_stop(&election_timer);
                if (global_config.test_auto_fail)
                {
                    uint64_t delay = global_config.test_auto_fail_timeout_ms + rte_rand() % (global_config.test_auto_fail_timeout_ms +1);
                    uint64_t cycles = (uint64_t)delay *
                                      rte_get_timer_hz() / 1000;
                    rte_timer_init(&fail_timer);
                    rte_timer_reset(&fail_timer, cycles, SINGLE, rte_lcore_id(),
                                    fail_enable_cb, NULL);
                }
            }
        }
        break;

    case MSG_HEARTBEAT:
        if (pkt->term >= raft_node.current_term)
        {
            raft_node.current_state = STATE_FOLLOWER;
            raft_node.current_term = pkt->term;
            raft_node.last_heard_µs = now_µs;

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
    if (raft_node.current_state != STATE_LEADER || test_auto_fail_enabled)
        return;

    struct raft_packet pkt = {
        .msg_type = MSG_HEARTBEAT,
        .term = raft_node.current_term,
        .node_id = raft_node.self_id,
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

    uint64_t detect_time = monotonic_us();
    if (raft_node.current_state != STATE_LEADER)
    {
        printf("[RAFT] Node %u: Detected leader failure, T_detect = %lu µs, [Detection latency] is %lu µs\n",
               raft_node.self_id, detect_time, detect_time - raft_node.last_heard_µs);
        election_start_time = detect_time;
        if (raft_node.last_heard_µs != 0)
        {
            FILE *fp = fopen("failover_stats.csv", "a");
            fprintf(fp, "detect,%lu,%lu,%lu\n",
                    raft_node.self_id,
                    detect_time,
                    detect_time - raft_node.last_heard_µs);
            fclose(fp);
        }

        start_election();
    }
}
