#include "config.h"
#include <stdio.h>
#include <string.h>
#include <jansson.h>

raft_config_t global_config;

// typedef struct {
//     uint32_t node_id;
//     uint32_t port_id;
//     char ip_map[MAX_NODES][16];              
//     struct rte_ether_addr mac_map[MAX_NODES];
//     uint32_t election_timeout_min_ms;
//     uint32_t election_timeout_max_ms;
//     uint32_t heartbeat_interval_ms;
//     uint32_t test_auto_fail_timeout_ms;      /**< Timeout for auto-fail test */
//     uint32_t test_auto_fail_duration_ms;     /**< Duration for auto-fail test */
//     bool test_auto_fail;                /**< Enable auto-fail test */
// } raft_config_t;

static int parse_mac(const char *str, struct rte_ether_addr *mac) {
    return sscanf(str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
        &mac->addr_bytes[0], &mac->addr_bytes[1], &mac->addr_bytes[2],
        &mac->addr_bytes[3], &mac->addr_bytes[4], &mac->addr_bytes[5]) == 6;
}

/*make global.config available*/
int load_config(const char *filename) {
    json_error_t error;
    json_t *root = json_load_file(filename, 0, &error);
    if (!root) {
        fprintf(stderr, "JSON parse error: %s\n", error.text);
        return -1;
    }
    global_config.node_num = json_integer_value(json_object_get(root, "node_num"));
    global_config.node_id = json_integer_value(json_object_get(root, "node_id"));
    global_config.port_id = json_integer_value(json_object_get(root, "port_id"));
    global_config.election_timeout_min_ms = json_integer_value(json_object_get(root, "election_timeout_min_ms"));
    global_config.election_timeout_max_ms = json_integer_value(json_object_get(root, "election_timeout_max_ms"));
    global_config.heartbeat_interval_ms = json_integer_value(json_object_get(root, "heartbeat_interval_ms"));
    global_config.test_auto_fail_timeout_ms = json_integer_value(json_object_get(root, "test_auto_fail_timeout_ms"));
    global_config.test_auto_fail_duration_ms = json_integer_value(json_object_get(root, "test_auto_fail_duration_ms"));
    global_config.test_auto_fail = json_is_true(json_object_get(root, "test_auto_fail"));

    json_t *ip_map = json_object_get(root, "ip_map");
    json_t *mac_map = json_object_get(root, "mac_map");

    for (int i = 1; i <= MAX_NODES; i++) {
        char key[4];
        snprintf(key, sizeof(key), "%d", i);
        json_t *ip = json_object_get(ip_map, key);
        json_t *mac = json_object_get(mac_map, key);
        if (!ip || !mac) continue; // skip null

        strncpy(global_config.ip_map[i], json_string_value(ip), 16);
        parse_mac(json_string_value(mac), &global_config.mac_map[i]);
    }

    json_decref(root);
    return 0;
}
