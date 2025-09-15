// include/timeout.h
#ifndef TIMEOUT_H
#define TIMEOUT_H

#include <stdint.h>
#include <rte_timer.h>

typedef void (*timeout_cb)(struct rte_timer *timer, void *arg);


void timeout_init(uint32_t min_ms, uint32_t max_ms);

void timeout_start_election(struct rte_timer *t,
                            timeout_cb cb,
                            void *arg);

void timeout_stop(struct rte_timer *t);
void sense_update(uint32_t peer, double sample_ms);
double get_rto(uint32_t peer);
void record_ps_rx(uint32_t peer, uint64_t tsc);
double compute_penalty(uint32_t self_id);
#endif
