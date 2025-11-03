#include "api.h"
#include "stats.h"
#include "metadata.h"
#include <rte_cycles.h>
#include <string.h>

int sense_get_unified_snapshot(uint32_t window_ms, uint16_t port_id,
                               struct sense_unified_snapshot *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    // Compute the RTT average for all peers in the past window_ms
    sense_get_rtt_avg_all(window_ms, &out->rtt);

    // Get the xstats snapshot
    int ret = sense_metadata_snapshot(port_id, &out->xstats);

    out->tsc = rte_get_tsc_cycles();
    return ret;
}

int sense_get_unified_snapshot_latest(uint16_t port_id,
                                      struct sense_unified_snapshot *out)
{
    if (!out) return -1;
    memset(out, 0, sizeof(*out));

    // Get the latest RTT snapshot
    const struct sense_rtt_snapshot *sr = sense_snapshot_get();
    if (sr) {
        memcpy(&out->rtt, sr, sizeof(out->rtt));
    }

    // Get the xstats snapshot
    int ret = sense_metadata_snapshot(port_id, &out->xstats);

    out->tsc = rte_get_tsc_cycles();
    return ret;
}