// timeout.c
#include "timeout.h"
#include <rte_lcore.h>
#include <rte_cycles.h>
#include <rte_random.h>

static uint32_t g_min_ms, g_max_ms;

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

    rte_timer_stop(t);
    rte_timer_init(t);
    rte_timer_reset(t,
                    cycles,
                    SINGLE,
                    rte_lcore_id(),
                    cb,
                    arg);
}

void timeout_stop(struct rte_timer *t)
{
    rte_timer_stop(t);
}
