#include <stdio.h>
#include <stdint.h>
#include <unistd.h>     // for sleep
#include <time.h>
#include <stdlib.h>
#include "election.h"
#include "packet.h"


static uint64_t get_time_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ((uint64_t)ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

// TODO: Implement the packet handling logic
static void simulate_incoming_vote_request(uint32_t from_node, uint32_t term) {
    struct raft_packet pkt = {
        .msg_type = MSG_VOTE_REQUEST,
        .term = term,
        .node_id = from_node,
        .rtt_ms = 0,
    };
    raft_handle_packet(&pkt, 0); // port 0 for now
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <node_id>\n", argv[0]);
        return 1;
    }

    uint32_t node_id = atoi(argv[1]);
    raft_init(node_id);

    uint64_t last_heartbeat = 0;

    while (1) {
        uint64_t now = get_time_ms();
        raft_tick(now);

        if (raft_get_state() == STATE_LEADER) {
            if (now - last_heartbeat > 500) {
                raft_send_heartbeat();
                last_heartbeat = now;
            }
        }

        // Test Local
        if (now % 10000 < 10 && node_id != 1) {
            simulate_incoming_vote_request(1, 1);
        }

        usleep(1000); // 1ms tick
    }

    return 0;
}
