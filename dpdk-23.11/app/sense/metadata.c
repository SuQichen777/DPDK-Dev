#include "metadata.h"
#include <rte_ethdev.h>
#include <string.h>

int sense_metadata_snapshot(uint16_t port_id, struct sense_xstats_snapshot *out)
{
    static uint16_t cached_port = UINT16_MAX;
    static int cached_len = 0;
    static struct rte_eth_xstat_name cached_names[SENSE_MAX_XSTATS];

    int len = rte_eth_xstats_get(port_id, NULL, 0);
    if (len <= 0) return -1;
    if (len > SENSE_MAX_XSTATS) len = SENSE_MAX_XSTATS;

    struct rte_eth_xstat xstats[SENSE_MAX_XSTATS];

    int ret = rte_eth_xstats_get(port_id, xstats, len);
    if (ret < 0) return -2;

    // 
    if (cached_len == 0 || cached_port != port_id || cached_len != len) {
        ret = rte_eth_xstats_get_names(port_id, cached_names, len);
        if (ret < 0) return -3;
        cached_port = port_id;
        cached_len = len;
    }

    memset(out, 0, sizeof(*out));
    out->port_id = port_id;
    out->count = len;
    for (int i = 0; i < len; i++) {
        // Use the cached names
        strncpy(out->names[i], cached_names[i].name, SENSE_XSTAT_NAME_MAX - 1);
        out->names[i][SENSE_XSTAT_NAME_MAX - 1] = '\0';
        out->values[i] = xstats[i].value;
    }
    return 0;
}