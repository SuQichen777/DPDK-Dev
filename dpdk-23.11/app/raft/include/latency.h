// include/latency.h
#ifndef LATENCY_H
#define LATENCY_H

#include <stdint.h>
#include "config.h"

struct latency_sample {
    uint32_t peer_id;
    double rtt_ms;
};

struct latency_poll_result {
    struct latency_sample samples[MAX_NODES];
    uint32_t count;
};

int latency_init(uint16_t port_id, uint32_t self_id);
int latency_poll(uint16_t port_id, struct latency_poll_result *result);

#endif /* LATENCY_H */
