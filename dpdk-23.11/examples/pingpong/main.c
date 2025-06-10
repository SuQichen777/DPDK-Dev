// The first DPU, Sending "ping" to the second DPU, and receiving "pong" from the second DPU
#include <stdlib.h>
#include <signal.h>  // deal with ctrl-c
#include <string.h>
#include <stdio.h>
#include <rte_common.h> // DPDK common definitions
#include <rte_eal.h> // DPDK Environment Abstraction Layer
#include <rte_ethdev.h> // DPDK Ethernet device API
#include <rte_mbuf.h> // DPDK memory buffer API
#include <rte_lcore.h> // DPDK logical core API
#include <rte_ether.h> // DPDK Ethernet header

// Global definitions
#define NUM_MBUFS 8192
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

static volatile int force_quit = 0; // when ctrl-c is pressed, this will be set to 1
static struct rte_mempool * mbuf_pool; // memory pool for mbufs

static const char * PING = "ping";
static const char * PONG = "pong";

// Hardcoded destination MAC address for the pong machine (can be overridden by --dest-mac)
static struct rte_ether_addr dest_mac = {
    // 30:3E:A7:1E:D9:E5
    .addr_bytes = {0x30, 0x3E, 0xA7, 0x1E, 0xD9, 0xE5}
};

// handle signals like ctrl-c
static void signal_handler(int signum) {
    if (signum == SIGINT || signum == SIGTERM)
        force_quit = 1;
}

static int parse_mac(const char * mac_str, struct rte_ether_addr * mac_addr) {
    // mac_str should be in the format "xx:xx:xx:xx:xx:xx"
    unsigned int mac[6];
    if (sscanf(mac_str, "%x:%x:%x:%x:%x:%x",
               &mac[0], &mac[1], &mac[2],
               &mac[3], &mac[4], &mac[5]) != 6) {
        return -1;
    }
    for (int i = 0; i < 6; i++) {
        mac_addr->addr_bytes[i] = (uint8_t)mac[i];
    }
    // turn "00:0C:29:D3:CC:89" into {0x00, 0x0C, 0x29, 0xD3, 0xCC, 0x89}
    return 0;
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

    rte_eth_macaddr_get(port, &eth->src_addr); // get our MAC address
    rte_ether_addr_copy(&dest_mac, &eth->dst_addr); 
    eth->ether_type = rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4); // dummy ether type
    memcpy(payload, msg, plen); // copy "ping" or "pong" into payload

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
        if (strcmp(payload, PONG) == 0) {
            send_string(port, PING);
        }

        rte_pktmbuf_free(bufs[i]);
    }
}

// DPDK initialization and main loop
int main(int argc, char **argv) {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    int ret = rte_eal_init(argc, argv);
    if (ret < 0) rte_exit(EXIT_FAILURE, "EAL init failed\n");

    argc -= ret; //argc is the number of arguments left after EAL initialization
    argv += ret; //argv[0] is now the first non-EAL argument
    uint16_t port = 2;
    // parse optional command line argument: --dest-mac=xx:xx:xx:xx:xx:xx
    for (int i = 1; i < argc; i++) {
        if (strncmp(argv[i], "--dest-mac=", 11) == 0) {
            const char *mac_str = argv[i] + 11;
            if (parse_mac(mac_str, &dest_mac) < 0) {
                rte_exit(EXIT_FAILURE, "Invalid MAC format in --dest-mac\n");
            } else {
                printf("Using destination MAC: %s\n", mac_str);
            }
        }else if (strncmp(argv[i], "--port=", 7) == 0) {
            port = (uint16_t)atoi(argv[i] + 7);
            printf("Using port: %u\n", port);
        }
    }

    if (rte_eth_dev_count_avail() < 1)
        rte_exit(EXIT_FAILURE, "Need at least 1 DPDK port\n");

    // create mbuf pool
    mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS,
        MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
    if (!mbuf_pool)
        rte_exit(EXIT_FAILURE, "mbuf pool creation failed\n");

    struct rte_eth_conf conf = {0}; // basic port config
    
    rte_eth_dev_configure(port, 1, 1, &conf); // 1 RX queue, 1 TX queue
    rte_eth_rx_queue_setup(port, 0, 128, rte_eth_dev_socket_id(port), NULL, mbuf_pool);
    rte_eth_tx_queue_setup(port, 0, 128, rte_eth_dev_socket_id(port), NULL);
    rte_eth_dev_start(port); // start the port
    rte_eth_promiscuous_enable(port); // enable promiscuous mode

    printf("Running dpdk_ping...\n");

    send_string(port, PING);

    while (!force_quit) {
        process_packets(port);
        rte_delay_ms(100);
    }

    rte_eal_cleanup();
    return 0;
}
