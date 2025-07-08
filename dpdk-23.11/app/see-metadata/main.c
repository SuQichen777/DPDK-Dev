#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <rte_ethdev.h>

#define MAX_XSTATS 512

void print_eth_xstats(uint16_t port_id) {
    struct rte_eth_xstat_name xstat_names[MAX_XSTATS];
    struct rte_eth_xstat xstats[MAX_XSTATS];
    int nb_xstats;


    nb_xstats = rte_eth_xstats_get_names(port_id, NULL, 0);
    if (nb_xstats > MAX_XSTATS) {
        printf("Too many xstats: %d > %d\n", nb_xstats, MAX_XSTATS);
        return;
    }


    if (rte_eth_xstats_get_names(port_id, xstat_names, nb_xstats) != nb_xstats) {
        printf("Failed to get xstat names\n");
        return;
    }

    if (rte_eth_xstats_get(port_id, xstats, nb_xstats) != nb_xstats) {
        printf("Failed to get xstat values\n");
        return;
    }

    printf("Extended stats for port %" PRIu16 ":\n", port_id);
    for (int i = 0; i < nb_xstats; i++) {
        printf("  %-32s : %" PRIu64 "\n", xstat_names[i].name, xstats[i].value);
    }
}
