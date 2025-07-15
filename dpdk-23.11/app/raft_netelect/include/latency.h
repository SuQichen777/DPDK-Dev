// include/latency.h
#ifndef LATENCY_H
#define LATENCY_H

#include <stdint.h>

int  latency_init_flows(uint16_t port_id, uint32_t self_id);
void latency_poll_once(uint16_t port_id);

#endif /* LATENCY_H */