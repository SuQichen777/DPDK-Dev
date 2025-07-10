#include "config.h"     
#include "timeout.h" 
#include "latency.h"

#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <rte_flow.h>

#if RTE_VERSION_NUM >= RTE_VERSION_NUM(23,11,0,0)
#define HAS_LATENCY_FIELD 1
#else
#define HAS_LATENCY_FIELD 0
#endif

static struct rte_flow *peer_flow[NUM_NODES + 1];
static inline uint32_t ip_to_u32(const char *s)
{
    return (uint32_t)inet_addr(s);
}

int latency_init_flows(uint16_t port_id, uint32_t self_id)
{
    struct rte_flow_error err;
    const uint32_t self_ip = ip_to_u32(global_config.ip_map[self_id]);

    struct rte_flow_attr attr = { .ingress = 1, .transfer = 1 };

    struct rte_flow_item pattern[4];
    memset(pattern, 0, sizeof(pattern));
    pattern[0].type = RTE_FLOW_ITEM_TYPE_ETH;

    struct rte_flow_item_ipv4 ip_spec = { .hdr.src_addr = self_ip };
    struct rte_flow_item_ipv4 ip_mask = { .hdr.src_addr = 0xffffffff,
                                          .hdr.dst_addr = 0xffffffff };
    pattern[1].type = RTE_FLOW_ITEM_TYPE_IPV4;
    pattern[1].spec = &ip_spec;
    pattern[1].mask = &ip_mask;

    struct rte_flow_item_udp  udp_spec = { .hdr.dst_port =
                                           rte_cpu_to_be_16(RAFT_PORT) };
    struct rte_flow_item_udp  udp_mask = { .hdr.dst_port = 0xffff };
    pattern[2].type = RTE_FLOW_ITEM_TYPE_UDP;
    pattern[2].spec = &udp_spec;
    pattern[2].mask = &udp_mask;

    pattern[3].type = RTE_FLOW_ITEM_TYPE_END;

#if !HAS_LATENCY_FIELD
    fprintf(stderr,
            "WARNING: DPDK < 23.11 —— flow latency counter not supported.\n"
            "latency.c is not used\n");
    return 0;
#else
    struct rte_flow_action_count cnt = { .shared = 0, .latency = 1 };
    struct rte_flow_action actions[2] = {
        { .type = RTE_FLOW_ACTION_TYPE_COUNT, .conf = &cnt },
        { .type = RTE_FLOW_ACTION_TYPE_END }
    };

    for (uint32_t peer = 1; peer <= NUM_NODES; ++peer) {
        if (peer == self_id) continue;
        ip_spec.hdr.dst_addr = ip_to_u32(global_config.ip_map[peer]);

        peer_flow[peer] = rte_flow_create(port_id, &attr,
                                          pattern, actions, &err);
        if (!peer_flow[peer]) {
            fprintf(stderr,
                    "latency_init: peer %u flow is not created: %s (%d)\n",
                    peer, err.message, err.type);
            return -1;
        }
    }
    return 0;
#endif
}

/* called periodically in main*/
void latency_poll_once(uint16_t port_id)
{
#if HAS_LATENCY_FIELD
    struct rte_flow_error err;
    for (uint32_t peer = 1; peer <= NUM_NODES; ++peer) {
        struct rte_flow *f = peer_flow[peer];
        if (!f) continue;

        struct rte_flow_query_count qc = {0};
        if (rte_flow_query(port_id, f, RTE_FLOW_ACTION_TYPE_COUNT,
                           &qc, &err) == 0 &&
            qc.hits > 0 && qc.latency > 0) {
            double rtt_ms = (double)qc.latency / (1e6 * qc.hits); /* ns→ms */
            sense_update(peer, rtt_ms);

            qc.reset = 1;
            rte_flow_query(port_id, f, RTE_FLOW_ACTION_TYPE_COUNT,
                           &qc, NULL);
        }
    }
#else
    (void)port_id; /* older DPDK */
#endif
}
