// include/measurement.h
#ifndef MEASUREMENT_H
#define MEASUREMENT_H

#include <stdint.h>
#include <stdbool.h>

typedef void (*measurement_job_fn)(uint64_t timestamp_us, void *arg);

struct measurement_config {
    uint32_t period_ms;            /* Scheduler period in milliseconds */
    measurement_job_fn job;        /* Optional measurement job */
    void *job_arg;                 /* Context passed to the job */
};

int measurement_scheduler_init(const struct measurement_config *cfg);
int measurement_scheduler_start(void);
void measurement_scheduler_stop(void);
bool measurement_scheduler_active(void);

#endif /* MEASUREMENT_H */
