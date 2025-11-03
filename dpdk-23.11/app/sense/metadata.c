#include "metadata.h"
#include <rte_ethdev.h>
#include <string.h>

int sense_metadata_snapshot(uint16_t port_id, struct sense_xstats_snapshot *out)
{
    int len = rte_eth_xstats_get(port_id, NULL, 0);
    if (len <= 0) return -1;
    if (len > SENSE_MAX_XSTATS) len = SENSE_MAX_XSTATS;

    struct rte_eth_xstat xstats[SENSE_MAX_XSTATS];
    struct rte_eth_xstat_name names[SENSE_MAX_XSTATS];

    int ret = rte_eth_xstats_get(port_id, xstats, len);
    if (ret < 0) return -2;

    ret = rte_eth_xstats_get_names(port_id, names, len);
    if (ret < 0) return -3;

    memset(out, 0, sizeof(*out));
    out->port_id = port_id;
    out->count = len;
    for (int i = 0; i < len; i++) {
        strncpy(out->names[i], names[i].name, SENSE_XSTAT_NAME_MAX - 1);
        out->names[i][SENSE_XSTAT_NAME_MAX - 1] = '\0';
        out->values[i] = xstats[i].value;
    }
    return 0;
}