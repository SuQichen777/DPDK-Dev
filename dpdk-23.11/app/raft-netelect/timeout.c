// timeout.c
#include "timeout.h"
#include "packet.h"
#include <rte_lcore.h>
#include <rte_cycles.h>
#include <rte_random.h>
#include <stdio.h>
#include <math.h> 

static uint32_t g_min_ms, g_max_ms;
// RTT values for each node
static double mu[NUM_NODES + 1] = {0};     // mean value
static double sigma[NUM_NODES + 1] = {0};  // standard deviation
static uint64_t last_ps_rx_ts[NUM_NODES + 1] = {0};
// Jacobson
// will be read from config file later
static const double ALPHA = 0.125;   /* 1/8  */
static const double BETA  = 0.25;    /* 1/4  */

// Update the RTT values for a peer
void sense_update(uint32_t peer, double sample_ms)
{
    if (mu[peer] == 0) {                 
        mu[peer]    = sample_ms;
        sigma[peer] = sample_ms / 2;
        return;
    }
    double err = sample_ms - mu[peer];
    mu[peer]    += ALPHA * err;
    sigma[peer]  = (1.0 - BETA) * sigma[peer] + BETA * fabs(err);
}
// Get the current RTO for a peer
// RTO = mu + 4 * sigma
double get_rto(uint32_t peer)
{
    return mu[peer] + 4.0 * sigma[peer];
}
double compute_penalty(uint32_t self_id)
{
    double sum = 0.0;
    int    cnt = 0;

    for (uint32_t p = 1; p <= NUM_NODES; ++p) {
        if (p == self_id) continue;        // skip self
        if (mu[p] > 0.0) {
            sum += mu[p];
            cnt++;
        }
    }
    if (cnt == 0) return 0.0;              //no sample no penalty
    return sum / cnt;                      //TODO: Change to the formula of NetElect
}

void record_ps_rx(uint32_t peer, uint64_t tsc)
{
    last_ps_rx_ts[peer] = tsc;
}

void timeout_init(uint32_t min_ms, uint32_t max_ms)
{
    g_min_ms = min_ms;
    g_max_ms = max_ms;
}

// generate a random number in the range [min, max]
static inline uint32_t
rand_in_range(uint32_t min, uint32_t max)
{
    uint32_t r = rte_rand();
    return min + r % (max - min + 1);
}

void timeout_start_election(struct rte_timer *t,
                            timeout_cb cb,
                            void *arg)
{
    uint32_t ms = rand_in_range(g_min_ms, g_max_ms);

    uint64_t cycles = (uint64_t)ms * rte_get_timer_hz() / 1000;
    unsigned lcore = rte_get_main_lcore();
    rte_timer_stop(t);
    rte_timer_init(t);
    int rc = rte_timer_reset(t,
                             cycles,
                             SINGLE,
                             lcore,
                             cb,
                             arg);
    if (rc != 0) {
        printf("Timer reset failed on lcore %u (rc=%d)\n", lcore, rc);
    } else {
        printf("Election timer set for %u ms on lcore %u\n", ms, lcore);
    }
}

void timeout_stop(struct rte_timer *t)
{
    rte_timer_stop(t);
}
