#include "config.h"
#include "stats.h"
#include "api.h"
#include "networking.h"
#include <rte_eal.h>
#include <rte_timer.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

static volatile int force_quit = 0;

static void signal_handler(int sig)
{
    (void)sig;
    force_quit = 1;
}

// Run xstats on a worker lcore to avoid blocking RX/RTT on main lcore
// xstats worker thread
static int xstats_worker(__rte_unused void *arg)
{
    uint64_t last = 0;
    const uint64_t interval = rte_get_tsc_hz() * 5; // 5s
    while (!force_quit) {
        uint64_t now = rte_get_tsc_cycles();
        if (now - last >= interval) {
            struct sense_unified_snapshot snap;
            if (sense_get_unified_snapshot_latest(sense_config.port_id, &snap) == 0) {
                for (uint32_t i = 0; i < snap.xstats.count; i++) {
                    printf("[XSTATS] %s = %lu\n", snap.xstats.names[i], snap.xstats.values[i]);
                }
                for (uint32_t peer = 1; peer <= sense_config.node_num; peer++) {
                    double avg = snap.rtt.avg_us[peer];
                    if (avg >= 0.0)
                        printf("[SENSE] snapshot avg RTT to %u = %.3f us\n", peer, avg);
                }

                sense_publish_stats(&snap);
            }
            last = now;
        }
        rte_pause();
    }
    return 0;
}

// main function with xstats worker thread
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

    if (sense_stats_init(sense_config.node_id, sense_config.node_num) != 0) {
        rte_exit(EXIT_FAILURE, "Failed to initialize RTT stats table\n");
    }

    // Open API: enable RTT snapshot every 1000ms, window 5000ms
    if (sense_snapshot_enable(1000, 5000) != 0) {
        printf("[WARN] RTT snapshot enable failed; fallback to on-demand RTT.\n");
    }

    net_init();

    printf("[SENSE] Node %u starting on port %u, total nodes=%u\n",
           sense_config.node_id, sense_config.port_id, sense_config.node_num);

    // Launch xstats collection on a worker lcore
    unsigned worker = RTE_MAX_LCORE;
    RTE_LCORE_FOREACH_WORKER(worker) { break; }
    if (worker != RTE_MAX_LCORE) {
        int rc = rte_eal_remote_launch(xstats_worker, NULL, worker);
        if (rc != 0) {
            printf("[WARN] Failed to launch xstats worker (rc=%d); xstats disabled on main lcore.\n", rc);
        }
    } else {
        printf("[WARN] No worker lcore available; xstats disabled to protect RTT.\n");
    }

    uint64_t last_ping_cycles = rte_get_tsc_cycles();
    const uint64_t ping_interval_cycles = rte_get_tsc_hz(); // ~1 second

    while (!force_quit) {
        process_rx();

        rte_timer_manage();

        uint64_t now = rte_get_tsc_cycles();
        if (now - last_ping_cycles >= ping_interval_cycles) {
            for (uint32_t peer = 1; peer <= sense_config.node_num; peer++) {
                if (peer == sense_config.node_id) continue;
                send_ping_packet(peer);
            }

            // RTT average over a long window (200s)
            struct sense_rtt_snapshot rtt_snap;
            sense_get_rtt_avg_all(200000, &rtt_snap);
            for (uint32_t peer = 1; peer <= sense_config.node_num; peer++) {
                double avg = rtt_snap.avg_us[peer];
                if (avg >= 0.0)
                    printf("[SENSE] avg RTT(200000ms) to %u = %.3f us\n", peer, avg);
            }

            last_ping_cycles = now;
        }

        rte_pause();
    }

    rte_eal_cleanup();
    return 0;
}
