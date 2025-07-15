// include/metadata.h
#ifndef METADATA_H_
#define METADATA_H_

#include <stdint.h>         /* uint16_t   */
#include <rte_ethdev.h>     /* rte_eth_xstats*/

struct app_config_params {
    uint16_t port_id;
};

struct stats_lcore_params {
    struct app_config_params *app_params;
};

void print_stats(struct stats_lcore_params *stats_lcore);

#endif /* METADATA_H_ */