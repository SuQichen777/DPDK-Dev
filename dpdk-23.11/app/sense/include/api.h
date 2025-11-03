#pragma once
#include <stdint.h>
#include "stats.h"
#include "metadata.h"

struct sense_unified_snapshot {
    struct sense_rtt_snapshot   rtt;    // RTT
    struct sense_xstats_snapshot xstats; // xstats snapshot
    uint64_t                    tsc;    // the timestamp when the snapshot is generated
};

// given window_ms, return the RTT average + xstats snapshot
int sense_get_unified_snapshot(uint32_t window_ms, uint16_t port_id,
                               struct sense_unified_snapshot *out);

// Low overhead read: use the periodic RTT snapshot + xstats snapshot
int sense_get_unified_snapshot_latest(uint16_t port_id,
                                      struct sense_unified_snapshot *out);