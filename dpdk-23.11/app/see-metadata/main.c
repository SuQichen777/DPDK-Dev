#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <inttypes.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_launch.h>
#include <rte_mbuf.h>
#include <rte_mempool.h>

struct app_config_params { uint16_t port_id; };
struct stats_lcore_params { struct app_config_params *app_params; };

static void print_stats(struct stats_lcore_params *stats_lcore)
{
	unsigned int port_id = stats_lcore->app_params->port_id;
	int len, ret, i;

	struct rte_eth_xstat *xstats = NULL;
	struct rte_eth_xstat_name *xstats_names = NULL;
	static const char *stats_border = "_______";

	printf("PORT STATISTICS:\n================\n");
	len = rte_eth_xstats_get(port_id, NULL, 0);
	if (len < 0)
		rte_exit(EXIT_FAILURE,
				"rte_eth_xstats_get(%u) failed: %d", port_id,
				len);

	xstats = calloc(len, sizeof(*xstats));
	if (xstats == NULL)
		rte_exit(EXIT_FAILURE,
				"Failed to calloc memory for xstats");

	ret = rte_eth_xstats_get(port_id, xstats, len);
	if (ret < 0 || ret > len) {
		free(xstats);
		rte_exit(EXIT_FAILURE,
				"rte_eth_xstats_get(%u) len%i failed: %d",
				port_id, len, ret);
	}

	xstats_names = calloc(len, sizeof(*xstats_names));
	if (xstats_names == NULL) {
		free(xstats);
		rte_exit(EXIT_FAILURE,
				"Failed to calloc memory for xstats_names");
	}

	ret = rte_eth_xstats_get_names(port_id, xstats_names, len);
	if (ret < 0 || ret > len) {
		free(xstats);
		free(xstats_names);
		rte_exit(EXIT_FAILURE,
				"rte_eth_xstats_get_names(%u) len%i failed: %d",
				port_id, len, ret);
	}

	for (i = 0; i < len; i++) {
		if (xstats[i].value > 0)
			printf("Port %u: %s %s:\t\t%"PRIu64"\n",
					port_id, stats_border,
					xstats_names[i].name,
					xstats[i].value);
	}
	free(xstats);
	free(xstats_names);
}

int main(int argc, char **argv) {
    printf("Start\n");

    int ret = rte_eal_init(argc, argv);
    if (ret < 0){
        rte_exit(EXIT_FAILURE, "EAL init failed\n");
    }

    uint16_t port_id = 0;
    if (!rte_eth_dev_is_valid_port(port_id))
        rte_exit(EXIT_FAILURE, "invalid port %u\n", port_id);

    // if 0 rx/tx queues are configured, some drivers may not start
    struct rte_eth_conf port_conf = {0};
    // if (rte_eth_dev_configure(port_id, 0, 0, &port_conf) < 0) {
    //     printf("Failed to configure port\n");
    //     return 1;
    // }
    if (rte_eth_dev_configure(port_id, 1, 0, &port_conf) < 0) {
        printf("Failed to configure port\n");
        return 1;
    }
    struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", 4096, 256, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (!mbuf_pool) {
        rte_exit(EXIT_FAILURE, "Failed to create mbuf pool\n");
        return 1;
    }
    rte_eth_rx_queue_setup(port_id, 0, 128, rte_eth_dev_socket_id(port_id), NULL, mbuf_pool);

    if (rte_eth_dev_start(port_id) < 0) {
        printf("Failed to start port\n");
        return 1;
    }

    printf("Port started, calling print_eth_xstats\n");
    struct app_config_params app_params = { .port_id = port_id };
    struct stats_lcore_params stats_lcore = { .app_params = &app_params };
    print_stats(&stats_lcore);
    rte_eal_cleanup();

    printf("Done.\n");

    return 0;
}

