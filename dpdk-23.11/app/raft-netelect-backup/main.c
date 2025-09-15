#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <rte_eal.h>
#include <rte_launch.h>
#include <rte_lcore.h>
#include <rte_timer.h>
#include <rte_pause.h>	
#include "election.h"
#include "networking.h"
#include "packet.h"
#include "config.h"
#include "metadata.h"
#include "latency.h"

// current time
static uint64_t get_time_ms(void) {
    return rte_get_timer_cycles() * 1000 / rte_get_timer_hz();
}

static void
stats_timer_cb(__rte_unused struct rte_timer *tim, void *arg)
{
    struct stats_lcore_params *st = arg;
    print_stats(st);
}


// DPDK work thread main function
static int lcore_main(void *arg)
{
    struct stats_lcore_params *st = arg;
    static struct rte_timer   stats_timer;
    static struct rte_timer ps_timer;
    static int timer_init_done = 0;

    if (!timer_init_done) {
        uint64_t hz = rte_get_timer_hz();
        rte_timer_init(&stats_timer);
        rte_timer_reset(&stats_timer, hz * 10, PERIODICAL,
                        rte_lcore_id(), stats_timer_cb, st);
        rte_timer_init(&ps_timer);
        rte_timer_reset(&ps_timer, hz / 200, PERIODICAL,
                        rte_lcore_id(),
                        (rte_timer_cb_t)ps_broadcast_send,
                        (void *)(uintptr_t)global_config.port_id);
        timer_init_done = 1;
    }

    uint64_t last_heartbeat = 0;
    printf("lcore_main running on lcore %u\n", rte_lcore_id());

    for (;;) {
        process_packets();
        rte_timer_manage();

        if (raft_get_state() == STATE_LEADER) {
            uint64_t now = rte_get_timer_cycles() * 1000 / rte_get_timer_hz();
            if (now - last_heartbeat >= 500) {
                raft_send_heartbeat();
                last_heartbeat = now;
            }
        }
        rte_pause();
    }
    return 0;
}

int main(int argc, char **argv) {
    // initialize the Environment Abstraction Layer (EAL)
    
    if (load_config("config.json") < 0){
        rte_exit(EXIT_FAILURE, "Failed to load config.json\n");
    }

    int ret = rte_eal_init(argc, argv);
    if (ret < 0){
        rte_exit(EXIT_FAILURE, "EAL init failed\n");
    }
    // uint32_t id = atoi(argv[1]);
    rte_timer_subsystem_init();
    net_init();
    raft_init(global_config.node_id);
    struct app_config_params app = { .port_id = global_config.port_id };
    struct stats_lcore_params st = { .app_params = &app };
    print_stats(&st);
    
    printf("Node %u starting...\n", raft_get_node_id());
    
    rte_eal_mp_remote_launch(lcore_main, &st, CALL_MAIN);
    rte_eal_mp_wait_lcore();
    
    return 0;
}