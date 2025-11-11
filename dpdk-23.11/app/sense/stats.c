#include "stats.h"
#include "config.h"
#include <rte_memzone.h>
#include <rte_eal.h>
#include <rte_cycles.h>
#include <rte_timer.h>
#include <string.h>

static struct sense_rtt_table *rtt_tbl = NULL;
static struct sense_rtt_sample sample_ring[SENSE_MAX_NODES + 1][SENSE_MAX_SAMPLES];
static uint16_t ring_head[SENSE_MAX_NODES + 1];
static uint16_t ring_count[SENSE_MAX_NODES + 1];

static struct sense_rtt_snapshot snapshot;
static struct rte_timer snapshot_timer;
static uint32_t snapshot_window_ms = 1000; // the window size for snapshot

int sense_stats_init(uint32_t node_id, uint32_t node_num)
{
    const struct rte_memzone *mz =
        rte_memzone_reserve(SENSE_RTT_MEMZONE,
                            sizeof(struct sense_rtt_table),
                            rte_socket_id(),
                            RTE_MEMZONE_2MB | RTE_MEMZONE_SIZE_HINT_ONLY);
    if (mz == NULL) {
        mz = rte_memzone_lookup(SENSE_RTT_MEMZONE);
        if (mz == NULL)
            return -1;
    }

    rtt_tbl = (struct sense_rtt_table *)mz->addr;
    memset(rtt_tbl, 0, sizeof(*rtt_tbl));
    rtt_tbl->node_id = node_id;
    rtt_tbl->node_num = node_num;

    memset(sample_ring, 0, sizeof(sample_ring));
    memset(ring_head, 0, sizeof(ring_head));
    memset(ring_count, 0, sizeof(ring_count));
    memset(&snapshot, 0, sizeof(snapshot));
    return 0;
}

static inline int peer_invalid(uint32_t peer_id)
{
    return (peer_id == 0 || peer_id > SENSE_MAX_NODES);
}

void sense_stats_record_ping(uint32_t peer_id)
{
    if (!rtt_tbl || peer_invalid(peer_id))
        return;
    struct sense_rtt_entry *entry = &rtt_tbl->entries[peer_id];
    entry->last_ping_tsc = rte_get_tsc_cycles();
    entry->ping_sent++;
    if (entry->ping_sent < entry->pong_recv) {
        entry->ping_sent = entry->pong_recv;
    }
    entry->loss_count = entry->ping_sent - entry->pong_recv;
}

void sense_stats_update(uint32_t peer_id, double rtt_us)
{
    if (!rtt_tbl || peer_invalid(peer_id))
        return;

    uint64_t now = rte_get_tsc_cycles();
    struct sense_rtt_entry *entry = &rtt_tbl->entries[peer_id];
    entry->last_rtt_us = rtt_us;
    if (entry->ewma_rtt_us <= 0.0) {
        entry->ewma_rtt_us = rtt_us;
    } else {
        entry->ewma_rtt_us = (SENSE_EWMA_ALPHA * rtt_us) +
                             ((1.0 - SENSE_EWMA_ALPHA) * entry->ewma_rtt_us);
    }
    entry->last_sample_tsc = now;
    entry->last_seen_tsc = now;
    entry->pong_recv++;
    if (entry->pong_recv > entry->ping_sent) {
        entry->ping_sent = entry->pong_recv;
    }
    entry->loss_count = entry->ping_sent - entry->pong_recv;
}

void sense_samples_append(uint32_t peer_id, double rtt_us)
{
    if (peer_invalid(peer_id))
        return;
    uint16_t head = ring_head[peer_id];
    sample_ring[peer_id][head].tsc = rte_get_tsc_cycles();
    sample_ring[peer_id][head].rtt_us = (float)rtt_us;
    ring_head[peer_id] = (uint16_t)((head + 1) % SENSE_MAX_SAMPLES);
    if (ring_count[peer_id] < SENSE_MAX_SAMPLES)
        ring_count[peer_id]++;
}

static double avg_in_window(uint32_t peer_id, uint32_t window_ms)
{
    if (ring_count[peer_id] == 0)
        return -1.0;

    uint64_t now = rte_get_tsc_cycles();
    uint64_t hz = rte_get_tsc_hz();
    uint64_t window_cycles = (uint64_t)window_ms * hz / 1000ULL;

    double sum = 0.0;
    uint32_t cnt = 0;

    // 从最新样本往回走
    int idx = ring_head[peer_id] - 1;
    if (idx < 0) idx = SENSE_MAX_SAMPLES - 1;
    for (uint16_t i = 0; i < ring_count[peer_id]; i++) {
        const struct sense_rtt_sample *s = &sample_ring[peer_id][idx];
        if (s->tsc == 0) break;
        if (now - s->tsc > window_cycles) break;
        sum += s->rtt_us;
        cnt++;
        if (--idx < 0) idx = SENSE_MAX_SAMPLES - 1;
    }
    if (cnt == 0) return -1.0;
    return sum / (double)cnt;
}

double sense_get_rtt_avg(uint32_t peer_id, uint32_t window_ms)
{
    if (peer_id == 0 || peer_id > SENSE_MAX_NODES)
        return -1.0;
    return avg_in_window(peer_id, window_ms);
}

void sense_get_rtt_avg_all(uint32_t window_ms, struct sense_rtt_snapshot *out)
{
    memset(out, 0, sizeof(*out));
    for (uint32_t p = 1; p <= SENSE_MAX_NODES; p++)
        out->avg_us[p] = avg_in_window(p, window_ms);
    out->last_tsc = rte_get_tsc_cycles();
}

static void snapshot_cb(__rte_unused struct rte_timer *t, __rte_unused void *arg)
{
    for (uint32_t p = 1; p <= SENSE_MAX_NODES; p++)
        snapshot.avg_us[p] = avg_in_window(p, snapshot_window_ms);
    snapshot.last_tsc = rte_get_tsc_cycles();
}

int sense_snapshot_enable(uint32_t interval_ms, uint32_t window_ms)
{
    snapshot_window_ms = window_ms;
    uint64_t hz = rte_get_tsc_hz();
    uint64_t cycles = (uint64_t)interval_ms * hz / 1000ULL;

    rte_timer_init(&snapshot_timer);
    int ret = rte_timer_reset(&snapshot_timer, cycles, PERIODICAL,
                              rte_lcore_id(), snapshot_cb, NULL);
    return ret;
}

const struct sense_rtt_snapshot* sense_snapshot_get(void)
{
    return &snapshot;
}

int sense_stats_build_report(const struct sense_rtt_snapshot *snapshot_in,
                             struct sense_stats_report_packet *out)
{
    if (!snapshot_in || !out || !rtt_tbl)
        return -1;

    memset(out, 0, sizeof(*out));
    out->msg_type = MSG_STATS_REPORT;
    out->src_id = sense_config.node_id;

    uint32_t peer_count = 0;
    for (uint32_t peer = 1; peer <= sense_config.node_num && peer_count < SENSE_MAX_NODES; peer++) {
        struct sense_peer_stats_entry *entry = &out->peers[peer_count];
        entry->peer_id = peer;
        double avg = snapshot_in->avg_us[peer];
        entry->avg_rtt_us = (avg >= 0.0) ? (float)avg : -1.0f;
        entry->loss_count = rtt_tbl->entries[peer].loss_count;
        peer_count++;
    }
    out->peer_count = (uint8_t)peer_count;
    return 0;
}
