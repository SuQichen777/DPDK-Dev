// dpdk_pong.c
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

static struct rte_ether_addr dest_mac = {
    .addr_bytes = {0x00, 0x0C, 0x29, 0x58, 0xE9, 0xD3}
};

static void signal_handler(int signum) { // handle signals like ctrl-c
    if (signum == SIGINT || signum == SIGTERM)
        force_quit = 1;
}

static void send_string(uint16_t port, const char *msg) {
    struct rte_mbuf *m = rte_pktmbuf_alloc(mbuf_pool);
    if (!m) return;

    size_t plen = strlen(msg) + 1;
    char *ptr = rte_pktmbuf_append(m, sizeof(struct rte_ether_hdr) + plen);
    if (!ptr) {
        rte_pktmbuf_free(m);
        return;
    }

    struct rte_ether_hdr *eth = (struct rte_ether_hdr *)ptr;
    char *payload = ptr + sizeof(struct rte_ether_hdr);

    rte_eth_macaddr_get(port, &eth->src_addr);
    rte_ether_addr_copy(&dest_mac, &eth->dst_addr);
    eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4);
    memcpy(payload, msg, plen);

    printf("Sending: %s\n", msg);
    rte_eth_tx_burst(port, 0, &m, 1);
}

static void process_packets(uint16_t port) {
    struct rte_mbuf *bufs[BURST_SIZE];
    uint16_t nb_rx = rte_eth_rx_burst(port, 0, bufs, BURST_SIZE);

    for (int i = 0; i < nb_rx; i++) {
        struct rte_ether_hdr *eth = rte_pktmbuf_mtod(bufs[i], struct rte_ether_hdr *);
        char *payload = (char *)(eth + 1);

        printf("Received: %s\n", payload);
        if (strcmp(payload, PING) == 0) {
            send_string(port, PONG);
        }

        rte_pktmbuf_free(bufs[i]);
    }
}

int main(int argc, char **argv) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    int ret = rte_eal_init(argc, argv);
    if (ret < 0) rte_exit(EXIT_FAILURE, "EAL init failed\n");

    if (rte_eth_dev_count_avail() < 1)
        rte_exit(EXIT_FAILURE, "Need at least 1 DPDK port\n");

    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS,
        MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (!mbuf_pool)
        rte_exit(EXIT_FAILURE, "mbuf pool creation failed\n");

    struct rte_eth_conf conf = {0};
    uint16_t port = 0;
    rte_eth_dev_configure(port, 1, 1, &conf);
    rte_eth_rx_queue_setup(port, 0, 128, rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    rte_eth_tx_queue_setup(port, 0, 128, rte_eth_dev_socket_id(port), NULL);
    rte_eth_dev_start(port);
    rte_eth_promiscuous_enable(port);

    printf("Running dpdk_pong...\n");

    while (!force_quit) {
        process_packets(port);
        rte_delay_ms(100);
    }

    rte_eal_cleanup();
    return 0;
}
