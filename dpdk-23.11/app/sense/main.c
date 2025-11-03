#include "config.h"
#include "stats.h"
#include "api.h"
#include "networking.h"
#include <rte_eal.h>
#include <rte_timer.h>
#include <rte_cycles.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static volatile int force_quit = 0;

static void signal_handler(int sig)
{
    (void)sig;
    force_quit = 1;
}

int main(int argc, char **argv)
{
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    if (sense_load_config("config.json") < 0)
    {
        rte_exit(EXIT_FAILURE, "Failed to load config.json\n");
    }

    int eal_args = rte_eal_init(argc, argv);
    if (eal_args < 0)
        rte_exit(EXIT_FAILURE, "EAL init failed\n");

    rte_timer_subsystem_init();

    net_init();

    printf("[SENSE] Node %u starting on port %u, total nodes=%u\n",
           sense_config.node_id, sense_config.port_id, sense_config.node_num);

    uint64_t last_ping_cycles = rte_get_tsc_cycles();
    const uint64_t ping_interval_cycles = rte_get_tsc_hz(); // ~1 second
    // xstats 每 5 秒抓一次，避免阻塞 RX
    uint64_t last_xstats_cycles = 0;
    const uint64_t xstats_interval_cycles = rte_get_tsc_hz() * 5;

    while (!force_quit) {
        process_rx();

        rte_timer_manage();

        uint64_t now = rte_get_tsc_cycles();
        if (now - last_ping_cycles >= ping_interval_cycles) {
            for (uint32_t peer = 1; peer <= sense_config.node_num; peer++) {
                if (peer == sense_config.node_id) continue;
                send_ping_packet(peer);
            }

            // RTT computed every 200000
            struct sense_rtt_snapshot rtt_snap;
            sense_get_rtt_avg_all(200000, &rtt_snap);
            for (uint32_t peer = 1; peer <= sense_config.node_num; peer++) {
                double avg = rtt_snap.avg_us[peer];
                if (avg >= 0.0)
                    printf("[SENSE] avg RTT(200000ms) to %u = %.3f us\n", peer, avg);
            }

            // XSTATS enable 5s interval
            if (now - last_xstats_cycles >= xstats_interval_cycles) {
                struct sense_xstats_snapshot xsnap;
                if (sense_metadata_snapshot(sense_config.port_id, &xsnap) == 0) {
                    for (uint32_t i = 0; i < xsnap.count && i < 5; i++) {
                        printf("[XSTATS] %s = %lu\n", xsnap.names[i], xsnap.values[i]);
                    }
                }
                last_xstats_cycles = now;
            }

            last_ping_cycles = now;
        }

        rte_pause();
        rte_delay_us_block(10);
    }

    rte_eal_cleanup();
    return 0;
}