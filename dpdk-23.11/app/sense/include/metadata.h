#pragma once
#include <stdint.h>

#define SENSE_MAX_XSTATS 256
#define SENSE_XSTAT_NAME_MAX 64

struct sense_xstats_snapshot {
    uint32_t port_id;
    uint32_t count;
    char     names[SENSE_MAX_XSTATS][SENSE_XSTAT_NAME_MAX];
    uint64_t values[SENSE_MAX_XSTATS];
};

// extra: fetch xstats snapshot for a port
int sense_metadata_snapshot(uint16_t port_id, struct sense_xstats_snapshot *out);