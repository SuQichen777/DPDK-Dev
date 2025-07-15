#include "metadata.h"
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <rte_ethdev.h>

void print_stats(struct stats_lcore_params *stats_lcore)
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