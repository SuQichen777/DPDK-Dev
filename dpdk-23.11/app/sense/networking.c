#include "networking.h"
#include "config.h"
#include "packet.h"
#include <rte_ethdev.h>
#include <rte_ether.h>
#include <rte_mbuf.h>
#include <rte_ip.h>
#include <rte_udp.h>
#include <rte_errno.h>
#include <rte_cycles.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#define MBUF_POOL_SIZE 4096
#define BURST_SIZE 32

static struct rte_mempool *mbuf_pool;
static const struct rte_eth_conf port_conf_default = {
    .rxmode = {.mtu = 1500},
    .txmode = {.offloads = 0}
};

static void send_raw_packet(void *payload, size_t payload_len, uint16_t dst_id)
{
    struct rte_mbuf *mbuf = rte_pktmbuf_alloc(mbuf_pool);
    if (!mbuf) return;

    char *pkt_data = rte_pktmbuf_append(mbuf, sizeof(struct rte_ether_hdr) +
                                              sizeof(struct rte_ipv4_hdr) +
                                              sizeof(struct rte_udp_hdr) + payload_len);
    if (!pkt_data) {
        rte_pktmbuf_free(mbuf);
        return;
    }

    struct rte_ether_hdr *eth_hdr = (struct rte_ether_hdr *)pkt_data;
    rte_ether_addr_copy(&sense_config.mac_map[sense_config.node_id], &eth_hdr->src_addr);
    rte_ether_addr_copy(&sense_config.mac_map[dst_id], &eth_hdr->dst_addr);
    eth_hdr->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);

    struct rte_ipv4_hdr *ip_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
    ip_hdr->version_ihl = RTE_IPV4_VHL_DEF;
    ip_hdr->type_of_service = 0;
    ip_hdr->total_length = rte_cpu_to_be_16(sizeof(struct rte_ipv4_hdr) +
                                            sizeof(struct rte_udp_hdr) + payload_len);
    ip_hdr->packet_id = rte_cpu_to_be_16(0);
    ip_hdr->fragment_offset = rte_cpu_to_be_16(0);
    ip_hdr->time_to_live = 64;
    ip_hdr->next_proto_id = IPPROTO_UDP;

    const char *src_ip = sense_config.ip_map[sense_config.node_id];
    const char *dst_ip = sense_config.ip_map[dst_id];
    if (!src_ip || !dst_ip) {
        rte_exit(EXIT_FAILURE, "Missing IP mapping for node %u or %u\n",
                 sense_config.node_id, dst_id);
    }
    ip_hdr->src_addr = inet_addr(src_ip);
    ip_hdr->dst_addr = inet_addr(dst_ip);

    struct rte_udp_hdr *udp_hdr = (struct rte_udp_hdr *)(ip_hdr + 1);
    udp_hdr->src_port = rte_cpu_to_be_16(SENSE_PORT);
    udp_hdr->dst_port = rte_cpu_to_be_16(SENSE_PORT);
    udp_hdr->dgram_len = rte_cpu_to_be_16(sizeof(struct rte_udp_hdr) + payload_len);

    void *pl = (void *)(udp_hdr + 1);
    memcpy(pl, payload, payload_len);

    ip_hdr->hdr_checksum = rte_ipv4_cksum(ip_hdr);
    udp_hdr->dgram_cksum = rte_ipv4_udptcp_cksum(ip_hdr, udp_hdr);

    rte_eth_tx_burst(sense_config.port_id, 0, &mbuf, 1);
}

void send_ping_packet(uint32_t peer_id)
{
    struct sense_ping_packet pkt;
    pkt.msg_type = MSG_PING_RTT;
    pkt.src_id = sense_config.node_id;
    pkt.send_ts = rte_get_tsc_cycles();
    pkt.tsc_hz = rte_get_tsc_hz();
    send_raw_packet(&pkt, sizeof(pkt), peer_id);
}

void send_pong_packet(uint32_t dst_id, uint64_t echoed_ts, uint64_t tsc_hz)
{
    struct sense_pong_packet pkt;
    pkt.msg_type = MSG_PONG_RTT;
    pkt.dst_id = dst_id;
    pkt.src_id = sense_config.node_id;
    pkt.echoed_ts = echoed_ts;
    pkt.tsc_hz = tsc_hz;
    send_raw_packet(&pkt, sizeof(pkt), dst_id);
}

void net_init(void)
{
    mbuf_pool = rte_pktmbuf_pool_create("SENSE_MBUF_POOL", MBUF_POOL_SIZE,
                                        0, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (mbuf_pool == NULL)
        rte_exit(EXIT_FAILURE, "Cannot create mbuf pool: %s\n", rte_strerror(rte_errno));

    int ret = rte_eth_dev_configure(sense_config.port_id, 1, 1, &port_conf_default);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "dev_configure err=%d\n", ret);

    ret = rte_eth_rx_queue_setup(sense_config.port_id, 0, 128,
                                 rte_eth_dev_socket_id(sense_config.port_id), NULL, mbuf_pool);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "rx_queue_setup err=%d\n", ret);

    ret = rte_eth_tx_queue_setup(sense_config.port_id, 0, 512,
                                 rte_eth_dev_socket_id(sense_config.port_id), NULL);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "tx_queue_setup err=%d\n", ret);

    ret = rte_eth_dev_start(sense_config.port_id);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "dev_start err=%d\n", ret);

    struct rte_ether_addr actual_mac;
    ret = rte_eth_macaddr_get(sense_config.port_id, &actual_mac);
    if (ret != 0) {
        printf("Failed to get MAC address for port %u: %s\n",
               sense_config.port_id, rte_strerror(rte_errno));
    } else {
        printf("Actual MAC for port %u: %02x:%02x:%02x:%02x:%02x:%02x\n",
               sense_config.port_id,
               actual_mac.addr_bytes[0], actual_mac.addr_bytes[1], actual_mac.addr_bytes[2],
               actual_mac.addr_bytes[3], actual_mac.addr_bytes[4], actual_mac.addr_bytes[5]);
    }
}

void process_rx(void)
{
    struct rte_mbuf *rx_bufs[BURST_SIZE];
    uint16_t n = rte_eth_rx_burst(sense_config.port_id, 0, rx_bufs, BURST_SIZE);
    for (uint16_t i = 0; i < n; i++) {
        struct rte_mbuf *m = rx_bufs[i];
        char *data = rte_pktmbuf_mtod(m, char *);

        struct rte_ether_hdr *eth = (struct rte_ether_hdr *)data;
        if (eth->ether_type != rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4)) {
            rte_pktmbuf_free(m);
            continue;
        }
        struct rte_ipv4_hdr *ip = (struct rte_ipv4_hdr *)(eth + 1);
        if (ip->next_proto_id != IPPROTO_UDP) {
            rte_pktmbuf_free(m);
            continue;
        }
        struct rte_udp_hdr *udp = (struct rte_udp_hdr *)(ip + 1);
        if (udp->dst_port != rte_cpu_to_be_16(SENSE_PORT) &&
            udp->src_port != rte_cpu_to_be_16(SENSE_PORT)) {
            rte_pktmbuf_free(m);
            continue;
        }

        uint8_t *payload = (uint8_t *)(udp + 1);
        uint8_t mtype = payload[0];

        if (mtype == MSG_PING_RTT) {
            struct sense_ping_packet *pp = (struct sense_ping_packet *)payload;
            uint32_t src_id = pp->src_id;
            if (src_id == 0 || src_id > sense_config.node_num) {
                rte_pktmbuf_free(m);
                continue;
            }
            send_pong_packet(src_id, pp->send_ts, pp->tsc_hz);
            rte_pktmbuf_free(m);
            continue;
        } else if (mtype == MSG_PONG_RTT) {
            struct sense_pong_packet *rp = (struct sense_pong_packet *)payload;
            uint32_t src_id = rp->src_id;
            uint32_t dst_id = rp->dst_id;
            if (dst_id != sense_config.node_id || src_id == 0 || src_id > sense_config.node_num) {
                rte_pktmbuf_free(m);
                continue;
            }
            uint64_t now = rte_get_tsc_cycles();
            uint64_t rtt_cycles = now - rp->echoed_ts;
            double rtt_us = (double)rtt_cycles * 1e6 / (double)rp->tsc_hz;
            printf("[SENSE] Node %u ⟵ RTT pong from %u | rtt=%.3f us\n",
                   sense_config.node_id, src_id, rtt_us);
            rte_pktmbuf_free(m);
            continue;
        }

        rte_pktmbuf_free(m);
    }
}