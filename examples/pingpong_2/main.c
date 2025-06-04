#include <stdlib.h>
#include <signal.h>  // deal with ctrl-c
#include <string.h>
#include <rte_common.h> // DPDK common definitions
#include <rte_eal.h> // DPDK Environment Abstraction Layer
#include <rte_ethdev.h> // DPDK Ethernet device API
#include <rte_mbuf.h> // DPDK memory buffer API
#include <rte_lcore.h> // DPDK logical core API
// Global definitions
#define NUM_MBUFS 8192
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

static volatile int force_quit = 0; // when ctrl-c is pressed, this will be set to 1
static struct rte_mempool * mbuf_pool; // memory pool for mbufs

static const char * PING = "ping";
static const char * PONG = "pong";

static void signal_handler(int signum) { // handle signals like ctrl-c
    if (signum == SIGINT || signum == SIGTERM)
        force_quit = 1;
}

static void send_string(uint16_t port, uint16_t peer_port, const char *msg) {
    /*
    port: current port
    peer_port: the port to send msg
    msg: Ping or Pong
    */
    struct rte_mbuf *m = rte_pktmbuf_alloc(mbuf_pool);
    // rte_mbuf - a struct in DPDK to store a packet
    if (!m) return; 
    /*
    if allocating a space from memory buffer
    does not work, then do noting
    */ 

    size_t plen = strlen(msg) + 1;
    char *ptr = rte_pktmbuf_append(m, sizeof(struct rte_ether_hdr) + plen);
    if (!ptr) {
        rte_pktmbuf_free(m);
        return;
    }
    // ethernet header to make sure the receiver can deal with a Ethernet packet
    struct rte_ether_hdr * eth = (struct rte_ether_hdr *)ptr;
    char *payload = ptr + sizeof(struct rte_ether_hdr);

    rte_eth_macaddr_get(port, &eth->src_addr);
    rte_eth_macaddr_get(peer_port, &eth->dst_addr); // hardcoding
    eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
    memcpy(payload, msg, plen);
    printf("Sending from port %u to port %u: %s\n", port, peer_port, msg);
    // printf("  src MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
    //        eth->src_addr.addr_bytes[0], eth->src_addr.addr_bytes[1], eth->src_addr.addr_bytes[2],
    //        eth->src_addr.addr_bytes[3], eth->src_addr.addr_bytes[4], eth->src_addr.addr_bytes[5]);
    // printf("  dst MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
    //        eth->dst_addr.addr_bytes[0], eth->dst_addr.addr_bytes[1], eth->dst_addr.addr_bytes[2],
    //        eth->dst_addr.addr_bytes[3], eth->dst_addr.addr_bytes[4], eth->dst_addr.addr_bytes[5]);

    uint16_t sent = rte_eth_tx_burst(port, 0, &m, 1);
    // printf("Port %u sent %u packet(s)\n", port, sent);

}

static void process_packets(uint16_t rx_port, uint16_t tx_port) {
    struct rte_mbuf *bufs[BURST_SIZE];
    uint16_t nb_rx = rte_eth_rx_burst(rx_port, 0, bufs, BURST_SIZE);

    for (int i = 0; i < nb_rx; i++) {
        struct rte_ether_hdr *eth = rte_pktmbuf_mtod(bufs[i], struct rte_ether_hdr *);
        char *payload = (char *)(eth + 1);

        printf("Port %u received: %s\n", rx_port, payload);

        if (strcmp(payload, PING) == 0) {
            send_string(rx_port, tx_port, PONG);
        } else if (strcmp(payload, PONG) == 0) {
            send_string(tx_port, rx_port, PING);
        }

        rte_pktmbuf_free(bufs[i]);
    }
}

int main(int argc, char **argv) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    int ret = rte_eal_init(argc, argv);
    if (ret < 0) rte_exit(EXIT_FAILURE, "EAL init failed\n");

    if (rte_eth_dev_count_avail() < 2)
        rte_exit(EXIT_FAILURE, "Need at least 2 DPDK ports\n");

    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS,
        MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (!mbuf_pool)
        rte_exit(EXIT_FAILURE, "mbuf pool creation failed\n");

    for (int port = 0; port < 2; port++) {
        struct rte_eth_conf conf = {0};
        rte_eth_dev_configure(port, 1, 1, &conf);
        rte_eth_rx_queue_setup(port, 0, 128, rte_eth_dev_socket_id(port), NULL, mbuf_pool);
        rte_eth_tx_queue_setup(port, 0, 128, rte_eth_dev_socket_id(port), NULL);
        rte_eth_dev_start(port);
        rte_eth_promiscuous_enable(port);
    }

    printf("Running bidirectional ping-pong between port 0 and 1...\n");

    send_string(0, 1, PING);

    while (!force_quit) {
        process_packets(0, 1);
        process_packets(1, 0);
        rte_delay_ms(100);
    }

    rte_eal_cleanup();
    return 0;
}
