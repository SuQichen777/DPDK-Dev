#include "config.h"
#include <jansson.h>
#include <string.h>
#include <rte_ether.h>
#include <stdio.h>

global_sense_config_t global_sense_config;

static int parse_mac(const char *str, struct rte_ether_addr *mac)
{
    return sscanf(str, "%hhx:%hhx:%hhx:%hhx:%hhx:%hhx",
                  &mac->addr_bytes[0], &mac->addr_bytes[1], &mac->addr_bytes[2],
                  &mac->addr_bytes[3], &mac->addr_bytes[4], &mac->addr_bytes[5]) == 6;
}

int sense_load_config(const char *filename)
{
    json_error_t error;
    json_t *root = json_load_file(filename, 0, &error);
    if (!root)
    {
        fprintf(stderr, "JSON parse error: %s\n", error.text);
        return -1;
    }
    json_t *node_id = json_object_get(root, "node_id");
    json_t *node_num = json_object_get(root, "node_num");
    json_t *port_id = json_object_get(root, "port_id");
    if (!json_is_integer(node_id) || !json_is_integer(node_num) || !json_is_integer(port_id)) {
        json_decref(root);
        return -1;
    }
    global_sense_config.node_id = (uint32_t)json_integer_value(node_id);
    global_sense_config.node_num = (uint32_t)json_integer_value(node_num);
    global_sense_config.port_id = (uint32_t)json_integer_value(port_id);

    json_t *ip_map = json_object_get(root, "ip_map");
    json_t *mac_map = json_object_get(root, "mac_map");
    if (!json_is_object(ip_map) || !json_is_object(mac_map)) {
        json_decref(root);
        return -1;
    }

    for (uint32_t i = 1; i <= global_sense_config.node_num; i++) {
        char key[16];
        snprintf(key, sizeof(key), "%u", i);
        json_t *ip = json_object_get(ip_map, key);
        json_t *mac = json_object_get(mac_map, key);
        if (!json_is_string(ip) || !json_is_string(mac)) {
            json_decref(root);
            return -1;
        }
        const char *ip_str = json_string_value(ip);
        const char *mac_str = json_string_value(mac);
        strncpy(global_sense_config.ip_map[i], ip_str, sizeof(global_sense_config.ip_map[i])-1);
        global_sense_config.ip_map[i][sizeof(global_sense_config.ip_map[i]) - 1] = '\0';
        if (parse_mac(mac_str, &global_sense_config.mac_map[i]) != 0) {
            json_decref(root);
            return -1;
        }
    }

    json_decref(root);
    return 0;
}