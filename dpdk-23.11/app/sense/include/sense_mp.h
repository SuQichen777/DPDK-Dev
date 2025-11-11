#pragma once

#include <stdint.h>
#include <rte_mempool.h>

#define SENSE_MP_INFO_ZONE "SENSE_MP_INFO"

#define SENSE_PRIMARY_RXQ 0
#define SENSE_PRIMARY_TXQ 0
#define SENSE_SECONDARY_RXQ 1
#define SENSE_SECONDARY_TXQ 1

struct sense_mp_info {
    uint16_t port_id;
    uint16_t primary_rxq;
    uint16_t primary_txq;
    uint16_t secondary_rxq;
    uint16_t secondary_txq;
    char     mempool_name[RTE_MEMPOOL_NAMESIZE];
};

