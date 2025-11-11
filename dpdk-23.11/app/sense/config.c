#include "config.h"
#include "packet.h"
#include <jansson.h>
#include <string.h>
#include <rte_ether.h>
#include <stdio.h>

sense_config_t sense_config;

static int parse_mac(const char *mac_str, struct rte_ether_addr *mac) {
    unsigned int b[6];
    if (sscanf(mac_str, "%x:%x:%x:%x:%x:%x",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6)
        return -1;
    for (int i = 0; i < 6; i++)
        mac->addr_bytes[i] = (uint8_t)b[i];
    return 0;
}

int sense_load_config(const char *filename)
{
    json_error_t err;
    json_t *root = json_load_file(filename, 0, &err);
    if (!root) {
        fprintf(stderr, "JSON load error: %s (line %d)\n", err.text, err.line);
        return -1;
    }

    memset(&sense_config, 0, sizeof(sense_config));
    sense_config.collector_port = SENSE_PORT;

    json_t *node_id = json_object_get(root, "node_id");
    json_t *node_num = json_object_get(root, "node_num");
    json_t *port_id = json_object_get(root, "port_id");
    if (!json_is_integer(node_id) || !json_is_integer(node_num) || !json_is_integer(port_id)) {
        json_decref(root);
        return -1;
    }
    sense_config.node_id = (uint32_t)json_integer_value(node_id);
    sense_config.node_num = (uint32_t)json_integer_value(node_num);
    sense_config.port_id = (uint32_t)json_integer_value(port_id);

    json_t *ip_map = json_object_get(root, "ip_map");
    json_t *mac_map = json_object_get(root, "mac_map");
    if (!json_is_object(ip_map) || !json_is_object(mac_map)) {
        json_decref(root);
        return -1;
    }

    for (uint32_t i = 1; i <= sense_config.node_num; i++) {
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
        strncpy(sense_config.ip_map[i], ip_str, sizeof(sense_config.ip_map[i]) - 1);
        sense_config.ip_map[i][sizeof(sense_config.ip_map[i]) - 1] = '\0';
        if (parse_mac(mac_str, &sense_config.mac_map[i]) != 0) {
            json_decref(root);
            return -1;
        }
    }

    json_t *collector = json_object_get(root, "collector");
    if (json_is_object(collector)) {
        json_t *ip = json_object_get(collector, "ip");
        json_t *mac = json_object_get(collector, "mac");
        json_t *port = json_object_get(collector, "port");
        if (json_is_string(ip) && json_is_string(mac)) {
            const char *ip_str = json_string_value(ip);
            const char *mac_str = json_string_value(mac);
            strncpy(sense_config.collector_ip, ip_str,
                    sizeof(sense_config.collector_ip) - 1);
            sense_config.collector_ip[sizeof(sense_config.collector_ip) - 1] = '\0';
            if (parse_mac(mac_str, &sense_config.collector_mac) == 0) {
                if (json_is_integer(port))
                    sense_config.collector_port = (uint16_t)json_integer_value(port);
                sense_config.collector_enabled = true;
            } else {
                fprintf(stderr, "Invalid collector MAC format: %s\n", mac_str);
            }
        }
    }

    json_decref(root);
    return 0;
}
