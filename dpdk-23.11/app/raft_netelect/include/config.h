#pragma once
#include <stdint.h>
#include <rte_ether.h>

#define MAX_NODES 16

typedef struct {
    uint32_t node_id;
    uint32_t port_id;
    char ip_map[MAX_NODES][16];              
    struct rte_ether_addr mac_map[MAX_NODES];
    uint32_t election_timeout_min_ms;
    uint32_t election_timeout_max_ms;
} raft_config_t;

extern raft_config_t global_config;

int load_config(const char *filename);
