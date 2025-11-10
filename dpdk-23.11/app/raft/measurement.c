#include "measurement.h"
#include "metadata.h"
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <rte_common.h>
#include <rte_timer.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_ethdev.h>
#include <sys/stat.h>
#include <sys/types.h>

#define DEFAULT_MEASUREMENT_PERIOD_MS 2000U
#define MEASUREMENT_SAMPLE_TARGET 30U
#define MEASUREMENT_DATA_DIR "app/raft/data_storage"
#define MEASUREMENT_SAMPLE_FILE MEASUREMENT_DATA_DIR "/measurement_samples.csv"
#define MEASUREMENT_SUMMARY_FILE MEASUREMENT_DATA_DIR "/measurement_summary.txt"

struct measurement_state {
    struct rte_timer timer;
    measurement_job_fn job;
    void *job_arg;
    uint64_t period_cycles;
    bool initialized;
    bool active;
    uint32_t guard;
};

struct cpu_snapshot {
    uint64_t total_jiffies;
    uint64_t idle_jiffies;
    uint64_t ctxt_switches;
};

struct measurement_accumulator {
    uint32_t sample_count;
    uint64_t last_timestamp_us;
    struct cpu_snapshot prev_cpu;
    struct rte_eth_stats prev_eth;
    bool prev_eth_valid;
    double cpu_busy_sum;
    double ctxt_rate_sum;
    double rx_mbps_sum;
    double tx_mbps_sum;
    FILE *sample_fp;
    bool baseline_ready;
};

static void measurement_job(uint64_t timestamp_us, void *arg);
static void measurement_timer_cb(struct rte_timer *tim, void *arg);
static inline uint64_t ms_to_cycles(uint32_t period_ms);
static int read_cpu_snapshot(struct cpu_snapshot *snap);
static double compute_cpu_busy_pct(const struct cpu_snapshot *cur,
                                   const struct cpu_snapshot *prev);
static double compute_ctxt_rate(const struct cpu_snapshot *cur,
                                const struct cpu_snapshot *prev,
                                uint64_t delta_us);
static double compute_mbps(uint64_t cur_bytes, uint64_t prev_bytes,
                           uint64_t delta_us);
static int ensure_data_dir(void);
static void log_sample(struct measurement_accumulator *acc,
                       uint64_t timestamp_us,
                       double cpu_busy,
                       double ctxt_rate,
                       double rx_mbps,
                       double tx_mbps);
static void finalize_measurements(struct measurement_accumulator *acc);

static struct measurement_state g_measurement = {
    .job = measurement_job,
};
static struct measurement_accumulator g_accumulator;

int measurement_scheduler_init(const struct measurement_config *cfg)
{
    if (cfg == NULL)
        return -EINVAL;

    uint32_t period_ms = cfg->period_ms ? cfg->period_ms : DEFAULT_MEASUREMENT_PERIOD_MS;

    g_measurement.period_cycles = ms_to_cycles(period_ms);
    g_measurement.job = cfg->job ? cfg->job : measurement_job;
    g_measurement.job_arg = cfg->job_arg;
    g_measurement.guard = 0;
    rte_timer_init(&g_measurement.timer);
    g_measurement.initialized = true;
    memset(&g_accumulator, 0, sizeof(g_accumulator));

    printf("[MEASURE] Scheduler initialized (period=%ums)\n", period_ms);
    return 0;
}

int measurement_scheduler_start(void)
{
    if (!g_measurement.initialized)
        return -EINVAL;

    unsigned int lcore = rte_get_main_lcore();
    int rc = rte_timer_reset(&g_measurement.timer,
                             g_measurement.period_cycles,
                             PERIODICAL,
                             lcore,
                             measurement_timer_cb,
                             &g_measurement);
    if (rc == 0)
        g_measurement.active = true;
    return rc;
}

void measurement_scheduler_stop(void)
{
    if (!g_measurement.initialized)
        return;

    rte_timer_stop(&g_measurement.timer);
    g_measurement.active = false;
}

bool measurement_scheduler_active(void)
{
    return g_measurement.active;
}

static inline uint64_t ms_to_cycles(uint32_t period_ms)
{
    uint64_t hz = rte_get_timer_hz();
    return (uint64_t)period_ms * hz / 1000ULL;
}

static inline uint64_t monotonic_us(void)
{
    return rte_get_timer_cycles() * 1000000ULL / rte_get_timer_hz();
}

static void measurement_timer_cb(struct rte_timer *tim, void *arg)
{
    struct measurement_state *state = arg;
    RTE_SET_USED(tim);

    if (state == NULL || state->job == NULL)
        return;

    uint32_t expected = 0;
    if (!__atomic_compare_exchange_n(&state->guard, &expected, 1, false,
                                     __ATOMIC_ACQUIRE, __ATOMIC_RELAXED))
        return;

    uint64_t timestamp_us = monotonic_us();
    state->job(timestamp_us, state->job_arg);

    __atomic_store_n(&state->guard, 0, __ATOMIC_RELEASE);
}

static int read_cpu_snapshot(struct cpu_snapshot *snap)
{
    FILE *fp = fopen("/proc/stat", "r");
    if (!fp)
        return -errno;

    char line[256];
    if (!fgets(line, sizeof(line), fp))
    {
        fclose(fp);
        return -EIO;
    }

    unsigned long long user = 0, nice = 0, system = 0, idle = 0;
    unsigned long long iowait = 0, irq = 0, softirq = 0, steal = 0, guest = 0, guest_nice = 0;
    int fields = sscanf(line, "cpu  %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                        &user, &nice, &system, &idle,
                        &iowait, &irq, &softirq, &steal, &guest, &guest_nice);
    if (fields < 4)
    {
        fclose(fp);
        return -EIO;
    }

    uint64_t total = user + nice + system + idle + iowait + irq + softirq + steal + guest + guest_nice;
    snap->total_jiffies = total;
    snap->idle_jiffies = idle + iowait;
    snap->ctxt_switches = 0;

    while (fgets(line, sizeof(line), fp))
    {
        if (strncmp(line, "ctxt ", 5) == 0)
        {
            unsigned long long ctxt = 0;
            if (sscanf(line + 5, "%llu", &ctxt) == 1)
                snap->ctxt_switches = ctxt;
            break;
        }
    }

    fclose(fp);
    return 0;
}

static double compute_cpu_busy_pct(const struct cpu_snapshot *cur,
                                   const struct cpu_snapshot *prev)
{
    if (cur->total_jiffies <= prev->total_jiffies)
        return 0.0;

    double delta_total = (double)(cur->total_jiffies - prev->total_jiffies);
    double delta_idle = (double)(cur->idle_jiffies - prev->idle_jiffies);
    if (delta_total <= 0.0)
        return 0.0;

    double busy = delta_total - delta_idle;
    if (busy < 0.0)
        busy = 0.0;
    return (busy / delta_total) * 100.0;
}

static double compute_ctxt_rate(const struct cpu_snapshot *cur,
                                const struct cpu_snapshot *prev,
                                uint64_t delta_us)
{
    if (delta_us == 0 || cur->ctxt_switches < prev->ctxt_switches)
        return 0.0;
    double delta = (double)(cur->ctxt_switches - prev->ctxt_switches);
    double seconds = (double)delta_us / 1e6;
    if (seconds == 0.0)
        return 0.0;
    return delta / seconds;
}

static double compute_mbps(uint64_t cur_bytes, uint64_t prev_bytes, uint64_t delta_us)
{
    if (delta_us == 0 || cur_bytes < prev_bytes)
        return 0.0;
    double delta = (double)(cur_bytes - prev_bytes);
    double seconds = (double)delta_us / 1e6;
    if (seconds == 0.0)
        return 0.0;
    return (delta * 8.0) / (seconds * 1e6); // convert to Mbps
}

static void ensure_sample_file(struct measurement_accumulator *acc)
{
    if (acc->sample_fp)
        return;

    if (ensure_data_dir() != 0)
        return;

    acc->sample_fp = fopen(MEASUREMENT_SAMPLE_FILE, "w");
    if (!acc->sample_fp)
        return;

    fprintf(acc->sample_fp, "timestamp_us,cpu_busy_pct,ctxt_per_sec,rx_mbps,tx_mbps\n");
}

static void log_sample(struct measurement_accumulator *acc,
                       uint64_t timestamp_us,
                       double cpu_busy,
                       double ctxt_rate,
                       double rx_mbps,
                       double tx_mbps)
{
    ensure_sample_file(acc);
    if (!acc->sample_fp)
        return;

    fprintf(acc->sample_fp,
            "%" PRIu64 ",%.2f,%.2f,%.3f,%.3f\n",
            timestamp_us,
            cpu_busy,
            ctxt_rate,
            rx_mbps,
            tx_mbps);
    fflush(acc->sample_fp);
}

static void summarize_measurements(struct measurement_accumulator *acc)
{
    if (acc->sample_count == 0)
        return;

    if (ensure_data_dir() != 0)
        return;

    FILE *fp = fopen(MEASUREMENT_SUMMARY_FILE, "w");
    if (!fp)
        return;

    double cpu_avg = acc->cpu_busy_sum / acc->sample_count;
    double ctxt_avg = acc->ctxt_rate_sum / acc->sample_count;
    double rx_avg = acc->rx_mbps_sum / acc->sample_count;
    double tx_avg = acc->tx_mbps_sum / acc->sample_count;

    fprintf(fp, "samples,%u\n", acc->sample_count);
    fprintf(fp, "cpu_busy_pct_avg,%.2f\n", cpu_avg);
    fprintf(fp, "ctxt_per_sec_avg,%.2f\n", ctxt_avg);
    fprintf(fp, "rx_mbps_avg,%.3f\n", rx_avg);
    fprintf(fp, "tx_mbps_avg,%.3f\n", tx_avg);
    fclose(fp);
}

static void finalize_measurements(struct measurement_accumulator *acc)
{
    summarize_measurements(acc);
    if (acc->sample_fp)
    {
        fclose(acc->sample_fp);
        acc->sample_fp = NULL;
    }
    measurement_scheduler_stop();
    printf("[MEASURE] Completed %u samples, scheduler stopped\n", acc->sample_count);
}

static int ensure_data_dir(void)
{
    static int dir_ready;
    if (dir_ready)
        return 0;

    struct stat st;
    if (stat(MEASUREMENT_DATA_DIR, &st) == 0)
    {
        if (S_ISDIR(st.st_mode))
        {
            dir_ready = 1;
            return 0;
        }
        printf("[MEASURE] %s exists but is not a directory\n", MEASUREMENT_DATA_DIR);
        return -ENOTDIR;
    }

    if (mkdir(MEASUREMENT_DATA_DIR, 0755) != 0 && errno != EEXIST)
    {
        printf("[MEASURE] Failed to create %s (errno=%d)\n", MEASUREMENT_DATA_DIR, errno);
        return -errno;
    }

    dir_ready = 1;
    return 0;
}

static void measurement_job(uint64_t timestamp_us, void *arg)
{
    struct stats_lcore_params *st = arg;
    uint16_t port_id = 0;
    if (st && st->app_params)
        port_id = st->app_params->port_id;

    struct cpu_snapshot cpu_now;
    if (read_cpu_snapshot(&cpu_now) != 0)
    {
        printf("[MEASURE] Failed to read /proc/stat\n");
        return;
    }

    struct rte_eth_stats eth_now;
    memset(&eth_now, 0, sizeof(eth_now));
    bool eth_valid = (rte_eth_stats_get(port_id, &eth_now) == 0);
    if (!eth_valid)
        printf("[MEASURE] Failed to read port %u stats\n", port_id);

    struct measurement_accumulator *acc = &g_accumulator;

    if (!acc->baseline_ready)
    {
        acc->prev_cpu = cpu_now;
        if (eth_valid)
        {
            acc->prev_eth = eth_now;
            acc->prev_eth_valid = true;
        }
        acc->last_timestamp_us = timestamp_us;
        acc->baseline_ready = true;
        return;
    }

    if (timestamp_us <= acc->last_timestamp_us)
        return;

    uint64_t delta_us = timestamp_us - acc->last_timestamp_us;
    double cpu_busy = compute_cpu_busy_pct(&cpu_now, &acc->prev_cpu);
    double ctxt_rate = compute_ctxt_rate(&cpu_now, &acc->prev_cpu, delta_us);
    double rx_mbps = 0.0;
    double tx_mbps = 0.0;

    if (eth_valid && acc->prev_eth_valid)
    {
        rx_mbps = compute_mbps(eth_now.ibytes, acc->prev_eth.ibytes, delta_us);
        tx_mbps = compute_mbps(eth_now.obytes, acc->prev_eth.obytes, delta_us);
    }

    log_sample(acc, timestamp_us, cpu_busy, ctxt_rate, rx_mbps, tx_mbps);

    acc->cpu_busy_sum += cpu_busy;
    acc->ctxt_rate_sum += ctxt_rate;
    acc->rx_mbps_sum += rx_mbps;
    acc->tx_mbps_sum += tx_mbps;
    acc->sample_count++;

    acc->prev_cpu = cpu_now;
    if (eth_valid)
    {
        acc->prev_eth = eth_now;
        acc->prev_eth_valid = true;
    }
    acc->last_timestamp_us = timestamp_us;

    if (acc->sample_count >= MEASUREMENT_SAMPLE_TARGET)
        finalize_measurements(acc);
}
