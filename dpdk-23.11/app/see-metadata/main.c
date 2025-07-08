#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include <stdlib.h>
#include <rte_eal.h>
#include <rte_ethdev.h>

void print_eth_xstats(uint16_t port_id) {
    int nb_xstats = rte_eth_xstats_get_names(port_id, NULL, 0);
    if (nb_xstats < 0) {
        printf("Failed to query xstats count\n");
        return;
    }

    struct rte_eth_xstat_name *xstat_names = calloc(nb_xstats, sizeof(*xstat_names));
    struct rte_eth_xstat *xstats = calloc(nb_xstats, sizeof(*xstats));

    if (!xstat_names || !xstats) {
        printf("Memory allocation failed\n");
        free(xstat_names);
        free(xstats);
        return;
    }

    if (rte_eth_xstats_get_names(port_id, xstat_names, nb_xstats) != nb_xstats) {
        printf("Failed to get xstat names\n");
        goto cleanup;
    }

    if (rte_eth_xstats_get(port_id, xstats, nb_xstats) != nb_xstats) {
        printf("Failed to get xstat values\n");
        goto cleanup;
    }

    printf("Extended stats for port %" PRIu16 ":\n", port_id);
    for (int i = 0; i < nb_xstats; i++) {
        printf("  %-32s : %" PRIu64 "\n", xstat_names[i].name, xstats[i].value);
    }

cleanup:
    free(xstat_names);
    free(xstats);
}

int main(int argc, char **argv) {
    if (rte_eal_init(argc, argv) < 0) {
        fprintf(stderr, "Failed to initialize EAL\n");
        return 1;
    }

    uint16_t port_id = 0;
    if (!rte_eth_dev_is_valid_port(port_id)) {
        fprintf(stderr, "Invalid port id %u\n", port_id);
        return 1;
    }

    print_eth_xstats(port_id);
    return 0;
}
