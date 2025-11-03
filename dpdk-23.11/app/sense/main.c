#include "config.h"
#include "networking.h"
#include <rte_eal.h>
#include <rte_timer.h>
#include <rte_cycles.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static volatile int force_quit = 0;

static void signal_handler(int sig) {
    (void)sig;
    force_quit = 1;
}

int main(int argc, char **argv)
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (sense_load_config("config.json") < 0) {
        rte_exit(EXIT_FAILURE, "Failed to load config.json\n");
    }

    int eal_args = rte_eal_init(argc, argv);
    if (eal_args < 0)
        rte_exit(EXIT_FAILURE, "EAL init failed\n");

    rte_timer_subsystem_init();

    net_init();

    if (sense_stats_init(sense_config.node_id, sense_config.node_num) < 0) {
        rte_exit(EXIT_FAILURE, "Failed to init sense stats memzone\n");
    }

    // Optional: enable snapshot every 500ms, window 1000ms
    // If not needed, comment out this line
    sense_snapshot_enable(500, 1000);

    printf("[SENSE] Node %u starting on port %u, total nodes=%u\n",
           sense_config.node_id, sense_config.port_id, sense_config.node_num);

    uint64_t last_ping_cycles = rte_get_tsc_cycles();
    const uint64_t ping_interval_cycles = rte_get_tsc_hz(); // ~1 second

    while (!force_quit) {
        process_rx();

        uint64_t now = rte_get_tsc_cycles();
        if (now - last_ping_cycles >= ping_interval_cycles) {
            struct sense_unified_snapshot snap;
            int ret = sense_get_unified_snapshot_latest(sense_config.port_id, &snap);
            if (ret == 0) {
                for (uint32_t peer = 1; peer <= sense_config.node_num; peer++) {
                    double avg = snap.rtt.avg_us[peer];
                    if (avg >= 0.0)
                        printf("[SENSE] latest avg RTT to %u = %.3f us\n", peer, avg);
                }
                for (uint32_t i = 0; i < snap.xstats.count && i < 5; i++) {
                    printf("[XSTATS] %s = %lu\n", snap.xstats.names[i], snap.xstats.values[i]);
                }
            }
            last_ping_cycles = now;
        }
        rte_delay_us_block(10);
    }

    rte_eal_cleanup();
    return 0;
}