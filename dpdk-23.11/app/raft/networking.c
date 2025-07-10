// networking.c
#include "networking.h"
#include "election.h"
#include "config.h"
#include "latency.h"
#include <stdlib.h>
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_mbuf.h>
#include <rte_ip.h>
#include <rte_timer.h>
#include <rte_errno.h>
#include <arpa/inet.h>

#define MBUF_POOL_SIZE 4096
#define BURST_SIZE 32

static struct rte_mempool *mbuf_pool;
// static uint16_t port_id;
// static uint32_t self_id;
static const struct rte_eth_conf port_conf_default = {
    .rxmode = {.mtu = 1500},
    // .txmode = { .offloads = RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE }
    .txmode = {.offloads = 0}};

// TODO: Simplified MAC address mapping for nodes
// static struct rte_ether_addr node_mac_map[NUM_NODES + 1] = {
//     [1] = {.addr_bytes = {0x00, 0x00, 0x00, 0x00, 0x01, 0x01}},
//     [2] = {.addr_bytes = {0x00, 0x00, 0x00, 0x00, 0x02, 0x02}},
//     [3] = {.addr_bytes = {0x00, 0x00, 0x00, 0x00, 0x03, 0x03}}
// };

void net_init(void)
{
    // (void)id;
    // self_id = id;
    // pool initialization
    mbuf_pool = rte_pktmbuf_pool_create("RAFT_MBUF_POOL", MBUF_POOL_SIZE,
                                        0, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool: %s\n", rte_strerror(rte_errno));
    // ethdev initialization
    // global_config.port_id = 0;
    int ret;
    ret = rte_eth_dev_configure(global_config.port_id, 1, 1, &port_conf_default);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "dev_configure err=%d\n", ret);

    // rx and tx queue setup
    ret = rte_eth_rx_queue_setup(global_config.port_id, 0, 128,
                                 rte_eth_dev_socket_id(global_config.port_id), NULL, mbuf_pool);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "rx_queue_setup err=%d\n", ret);
    ret = rte_eth_tx_queue_setup(global_config.port_id, 0, 512,
                                 rte_eth_dev_socket_id(global_config.port_id), NULL);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "tx_queue_setup err=%d\n", ret);

    // start the device
    ret = rte_eth_dev_start(global_config.port_id);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "dev_start err=%d\n", ret);
    if (latency_init_flows(global_config.port_id,
                       global_config.node_id) < 0)
        rte_exit(EXIT_FAILURE, "Latency flow init failed\n");
    struct rte_ether_addr actual_mac;
    ret = rte_eth_macaddr_get(global_config.port_id, &actual_mac);
    if (ret != 0) {
        printf("Failed to get MAC address for port %u: %s\n", global_config.port_id, rte_strerror(rte_errno));
    } else {
        printf("Actual MAC address for port %u: %02x:%02x:%02x:%02x:%02x:%02x\n",
            global_config.port_id,
            actual_mac.addr_bytes[0], actual_mac.addr_bytes[1], actual_mac.addr_bytes[2],
            actual_mac.addr_bytes[3], actual_mac.addr_bytes[4], actual_mac.addr_bytes[5]);
    }
}

void send_raft_packet(struct raft_packet *pkt, uint16_t dst_id)
{
    struct rte_mbuf *mbuf = rte_pktmbuf_alloc(mbuf_pool);
    if (!mbuf)
        return;

    // pkt data is the ptr of the mbuf
    // rte_ether_hdr + rte_ipv4_hdr + rte_udp_hdr + raft_packet(payload)
    char *pkt_data = rte_pktmbuf_append(mbuf, sizeof(struct raft_packet) +
                                                  sizeof(struct rte_udp_hdr) +
                                                  sizeof(struct rte_ipv4_hdr) +
                                                  sizeof(struct rte_ether_hdr));

    // eth_hdr is the first part of the packet
    struct rte_ether_hdr *eth_hdr = (struct rte_ether_hdr *)pkt_data;
    rte_ether_addr_copy(&global_config.mac_map[raft_get_node_id()], &eth_hdr->src_addr); // src MAC
    rte_ether_addr_copy(&global_config.mac_map[dst_id], &eth_hdr->dst_addr);             // dst MAC
    eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

    // IPv4 header
    struct rte_ipv4_hdr *ip_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
    ip_hdr->version_ihl = 0x45;  // IPv4, IHL=5
    ip_hdr->type_of_service = 0; // TODO: if QoS is needed reset this
    ip_hdr->total_length = rte_cpu_to_be_16(sizeof(struct rte_ipv4_hdr) +
                                            sizeof(struct rte_udp_hdr) +
                                            sizeof(struct raft_packet));
    ip_hdr->packet_id = rte_cpu_to_be_16(0);
    ip_hdr->fragment_offset = rte_cpu_to_be_16(0);
    ip_hdr->time_to_live = 64;
    ip_hdr->next_proto_id = IPPROTO_UDP; // 17 UDP protocol
    const char *src_ip = global_config.ip_map[raft_get_node_id()];
    const char *dst_ip = global_config.ip_map[dst_id];
    if (!src_ip || !dst_ip)
    {
        rte_exit(EXIT_FAILURE, "Missing IP mapping for node %u or %u\n", raft_get_node_id(), dst_id);
    }
    ip_hdr->src_addr = inet_addr(src_ip);
    ip_hdr->dst_addr = inet_addr(dst_ip);

    // UDP header
    struct rte_udp_hdr *udp_hdr = (struct rte_udp_hdr *)(ip_hdr + 1);
    udp_hdr->src_port = rte_cpu_to_be_16(RAFT_PORT);
    udp_hdr->dst_port = rte_cpu_to_be_16(RAFT_PORT);
    udp_hdr->dgram_len = rte_cpu_to_be_16(sizeof(struct raft_packet) +
                                          sizeof(struct rte_udp_hdr));

    // Packet payload
    struct raft_packet *raft_data = (struct raft_packet *)(udp_hdr + 1);
    memcpy(raft_data, pkt, sizeof(struct raft_packet));

    // Calculate checksums after filling the payload
    ip_hdr->hdr_checksum = rte_ipv4_cksum(ip_hdr);
    udp_hdr->dgram_cksum = rte_ipv4_udptcp_cksum(ip_hdr, udp_hdr);

    // send the packet
    rte_eth_tx_burst(global_config.port_id, 0, &mbuf, 1);
}

void process_packets(void)
{
    struct rte_mbuf *rx_bufs[BURST_SIZE];
    uint16_t nb_rx = rte_eth_rx_burst(global_config.port_id, 0, rx_bufs, BURST_SIZE);

    for (int i = 0; i < nb_rx; i++)
    {
        struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(rx_bufs[i], struct rte_ether_hdr *);
        if (eth_hdr->ether_type != rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4))
        {
            rte_pktmbuf_free(rx_bufs[i]);
            continue;
        }

        struct rte_ipv4_hdr *ip_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
        if (ip_hdr->next_proto_id != IPPROTO_UDP)
        {
            rte_pktmbuf_free(rx_bufs[i]);
            continue;
        }

        struct rte_udp_hdr *udp_hdr = (struct rte_udp_hdr *)(ip_hdr + 1);
        if (udp_hdr->dst_port != rte_cpu_to_be_16(RAFT_PORT))
        {
            rte_pktmbuf_free(rx_bufs[i]);
            continue;
        }

        struct raft_packet *raft_pkt = (struct raft_packet *)(udp_hdr + 1);
        raft_handle_packet(raft_pkt, 0);
        rte_pktmbuf_free(rx_bufs[i]);
    }
}