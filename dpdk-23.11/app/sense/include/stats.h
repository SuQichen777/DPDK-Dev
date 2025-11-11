#pragma once
#include <stdint.h>
#include "packet.h"

#define SENSE_MAX_NODES   16
#define SENSE_RTT_MEMZONE "SENSE_RTT_TABLE"
#define SENSE_MAX_SAMPLES 2048
#define SENSE_EWMA_ALPHA  0.2

struct sense_rtt_entry {
    double   last_rtt_us;
    double   ewma_rtt_us;
    uint64_t last_sample_tsc;
    uint64_t last_seen_tsc;
    uint64_t last_ping_tsc;
    uint32_t ping_sent;
    uint32_t pong_recv;
    uint32_t loss_count;
};

struct sense_rtt_table {
    uint32_t node_id;
    uint32_t node_num;
    struct sense_rtt_entry entries[SENSE_MAX_NODES + 1]; // 1-based indexing
};

struct sense_rtt_sample {
    uint64_t tsc;
    float    rtt_us;
};

struct sense_rtt_snapshot {
    double   avg_us[SENSE_MAX_NODES + 1]; // average RTT for each peer
    uint64_t last_tsc;
};

// Initialize shared table (optionally readable by other processes)
int sense_stats_init(uint32_t node_id, uint32_t node_num);

// Record an RTT (called when packet is received)
void sense_stats_update(uint32_t peer_id, double rtt_us);

// Append sample to per-process ring buffer (called when packet is received)
void sense_samples_append(uint32_t peer_id, double rtt_us);

// Track ping transmissions to derive pending/loss metrics
void sense_stats_record_ping(uint32_t peer_id);

// On-demand: return average RTT for peer in the past window_ms (<0 if no samples)
double sense_get_rtt_avg(uint32_t peer_id, uint32_t window_ms);

// On-demand: iterate all peers and write avg_us
void sense_get_rtt_avg_all(uint32_t window_ms, struct sense_rtt_snapshot *out);

// Enable periodic snapshot: compute average over window_ms every interval_ms
int sense_snapshot_enable(uint32_t interval_ms, uint32_t window_ms);

// Get the most recent snapshot
const struct sense_rtt_snapshot* sense_snapshot_get(void);

int sense_stats_build_report(const struct sense_rtt_snapshot *snapshot,
                             struct sense_stats_report_packet *out);
