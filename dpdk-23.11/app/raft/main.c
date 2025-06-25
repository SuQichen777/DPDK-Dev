#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <rte_eal.h>
#include <rte_launch.h>
#include <rte_lcore.h>
#include <rte_timer.h>
#include "election.h"
#include "networking.h"
#include "packet.h"

// static uint32_t node_id;

// current time
static uint64_t get_time_ms(void) {
    return rte_get_timer_cycles() * 1000 / rte_get_timer_hz();
}

// DPDK work thread main function
static int lcore_main(__attribute__((unused)) void *arg) {
    uint64_t last_heartbeat = 0;
    
    while (1) {
        // looping

        process_packets();
        rte_timer_manage();
        // send heartbeat if this node is the leader
        if (raft_get_state() == STATE_LEADER) {
            uint64_t now = get_time_ms();
            if (now - last_heartbeat >= 500) {
                raft_send_heartbeat();
                last_heartbeat = now;
            }
        }
        
        // delay using DPDK's timer
        rte_pause();
    }
    return 0;
}

int main(int argc, char **argv) {
    // initialize the Environment Abstraction Layer (EAL)
    int ret = rte_eal_init(argc, argv);
    if (ret < 0) {
        rte_exit(EXIT_FAILURE, "EAL init failed\n");
    }
    
    if (argc < 2) {
        rte_exit(EXIT_FAILURE, "Usage: %s <node_id>\n", argv[0]);
    }
    uint32_t id = atoi(argv[1]);
    
    net_init(id);
    raft_init(id);
    
    printf("Node %u starting...\n", raft_get_node_id());
    
    rte_eal_mp_remote_launch(lcore_main, NULL, CALL_MAIN);
    rte_eal_mp_wait_lcore();
    
    return 0;
}