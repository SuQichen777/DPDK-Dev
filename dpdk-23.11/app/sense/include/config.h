#pragma once
#include <stdint.h>
#include <rte_ether.h>
#include <stdbool.h>

#define SENSE_MAX_NODES 16

typedef struct {
    uint32_t node_num;
    uint32_t node_id;
    uint32_t port_id;
    char ip_map[SENSE_MAX_NODES+1][16];
    struct rte_ether_addr mac_map[SENSE_MAX_NODES+1];
} sense_config_t;

extern sense_config_t sense_config;

int sense_load_config(const char *filename);