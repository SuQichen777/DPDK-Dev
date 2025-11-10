#include "latency.h"
#include "packet.h"
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <rte_flow.h>
#include <rte_version.h>

#if RTE_VER_YEAR > 23 || (RTE_VER_YEAR == 24 && RTE_VER_MONTH >= 11)
#define HAS_LATENCY_FIELD 1
#else
#define HAS_LATENCY_FIELD 0
#endif

static struct rte_flow *peer_flow[MAX_NODES + 1];
static int latency_ready;

static inline uint32_t ip_to_u32(const char *addr)
{
    return (uint32_t)inet_addr(addr);
}

int latency_init(uint16_t port_id, uint32_t self_id)
{
#if !HAS_LATENCY_FIELD
    printf("[LATENCY] Flow latency counters unsupported on this DPDK version\n");
    latency_ready = 0;
    return 0;
#else
    memset(peer_flow, 0, sizeof(peer_flow));

    struct rte_flow_attr attr = {
        .ingress = 1,
        .transfer = 1,
    };

    struct rte_flow_item pattern[4];
    memset(pattern, 0, sizeof(pattern));
    pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;

    const uint32_t self_ip = ip_to_u32(global_config.ip_map[self_id]);
    struct rte_flow_item_ipv4 ip_spec = { .hdr.src_addr = self_ip };
    struct rte_flow_item_ipv4 ip_mask = {
        .hdr.src_addr = 0xffffffff,
        .hdr.dst_addr = 0xffffffff,
    };
    pattern[1].type = RTE_FLOW_ITEM_TYPE_IPV4;
    pattern[1].spec = &ip_spec;
    pattern[1].mask = &ip_mask;

    struct rte_flow_item_udp udp_spec = { .hdr.dst_port = rte_cpu_to_be_16(RAFT_PORT) };
    struct rte_flow_item_udp udp_mask = { .hdr.dst_port = 0xffff };
    pattern[2].type = RTE_FLOW_ITEM_TYPE_UDP;
    pattern[2].spec = &udp_spec;
    pattern[2].mask = &udp_mask;

    pattern[3].type = RTE_FLOW_ITEM_TYPE_END;

    struct rte_flow_action_count count_conf = { .shared = 0, .latency = 1 };
    struct rte_flow_action actions[2] = {
        { .type = RTE_FLOW_ACTION_TYPE_COUNT, .conf = &count_conf },
        { .type = RTE_FLOW_ACTION_TYPE_END },
    };

    struct rte_flow_error err;
    uint32_t peers_created = 0;
    for (uint32_t peer = 1; peer <= global_config.node_num; ++peer)
    {
        if (peer == self_id)
            continue;
        const char *dst_ip = global_config.ip_map[peer];
        if (dst_ip[0] == '\0')
            continue;
        ip_spec.hdr.dst_addr = ip_to_u32(dst_ip);

        peer_flow[peer] = rte_flow_create(port_id, &attr, pattern, actions, &err);
        if (!peer_flow[peer])
        {
            printf("[LATENCY] Failed to create flow for peer %u: %s (type=%d)\n",
                   peer, err.message ? err.message : "unknown", err.type);
            continue;
        }
        peers_created++;
    }

    if (peers_created == 0)
    {
        printf("[LATENCY] No latency flows created\n");
        latency_ready = 0;
        return 0;
    }

    latency_ready = 1;
    printf("[LATENCY] Enabled for %u peers\n", peers_created);
    return 0;
#endif
}

int latency_poll(uint16_t port_id, struct latency_poll_result *result)
{
    if (!result)
        return -1;

    result->count = 0;

#if !HAS_LATENCY_FIELD
    (void)port_id;
    return 0;
#else
    if (!latency_ready)
        return 0;

    struct rte_flow_error err;
    for (uint32_t peer = 1; peer <= global_config.node_num; ++peer)
    {
        struct rte_flow *flow = peer_flow[peer];
        if (!flow)
            continue;

        struct rte_flow_query_count query = {0};
        if (rte_flow_query(port_id, flow, RTE_FLOW_ACTION_TYPE_COUNT, &query, &err) == 0 &&
            query.hits > 0 && query.latency > 0)
        {
            double rtt_ms = (double)query.latency / (1e6 * (double)query.hits);
            if (result->count < MAX_NODES)
            {
                result->samples[result->count].peer_id = peer;
                result->samples[result->count].rtt_ms = rtt_ms;
                result->count++;
            }
            query.reset = 1;
            rte_flow_query(port_id, flow, RTE_FLOW_ACTION_TYPE_COUNT, &query, NULL);
        }
    }
    return 0;
#endif
}
