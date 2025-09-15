// rtt_test.c - RTT测试工具，基于raft-netelect的通信机制
#include "networking.h"
#include "config.h"
#include "packet.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_timer.h>
#include <rte_lcore.h>

#define MAX_RTT_SAMPLES 1000
#define DEFAULT_TEST_DURATION 10  // seconds
#define DEFAULT_PING_INTERVAL 100 // milliseconds

static volatile bool force_quit = false;
static uint32_t target_node_id = 0;
static uint32_t test_duration = DEFAULT_TEST_DURATION;
static uint32_t ping_interval_ms = DEFAULT_PING_INTERVAL;
static uint32_t ping_count = 0;
static uint32_t pong_count = 0;

// RTT统计数据
static double rtt_samples[MAX_RTT_SAMPLES];
static uint32_t sample_count = 0;
static double min_rtt = 999999.0;
static double max_rtt = 0.0;
static double sum_rtt = 0.0;

// 信号处理函数
static void signal_handler(int signum)
{
    if (signum == SIGINT || signum == SIGTERM) {
        printf("\n收到信号 %d，正在退出...\n", signum);
        force_quit = true;
    }
}

// 打印使用说明
static void print_usage(const char *prgname)
{
    printf("使用方法: %s [EAL选项] -- [应用选项]\n", prgname);
    printf("应用选项:\n");
    printf("  -t TARGET_NODE_ID : 目标节点ID (必需)\n");
    printf("  -d DURATION       : 测试持续时间(秒) [默认: %d]\n", DEFAULT_TEST_DURATION);
    printf("  -i INTERVAL       : ping间隔(毫秒) [默认: %d]\n", DEFAULT_PING_INTERVAL);
    printf("  -h                : 显示此帮助信息\n");
}

// 解析命令行参数
static int parse_args(int argc, char **argv)
{
    int opt;
    char **argvopt;
    int option_index;
    char *prgname = argv[0];
    
    argvopt = argv;
    
    while ((opt = getopt(argc, argvopt, "t:d:i:h")) != EOF) {
        switch (opt) {
        case 't':
            target_node_id = (uint32_t)strtoul(optarg, NULL, 10);
            break;
        case 'd':
            test_duration = (uint32_t)strtoul(optarg, NULL, 10);
            break;
        case 'i':
            ping_interval_ms = (uint32_t)strtoul(optarg, NULL, 10);
            break;
        case 'h':
            print_usage(prgname);
            return -1;
        default:
            print_usage(prgname);
            return -1;
        }
    }
    
    if (target_node_id == 0) {
        printf("错误: 必须指定目标节点ID (-t)\n");
        print_usage(prgname);
        return -1;
    }
    
    return 0;
}

// 发送ping包
static void send_ping_to_target(void)
{
    struct rtt_ping_packet pkt;
    pkt.msg_type = MSG_PING_RTT;
    pkt.src_id = global_config.node_id;
    pkt.send_ts = rte_get_timer_cycles();
    pkt.tsc_hz = rte_get_timer_hz();
    
    send_rtt_ping_packet(&pkt, target_node_id);
    ping_count++;
    
    printf("发送 ping #%u 到节点 %u\n", ping_count, target_node_id);
}

// 处理接收到的pong包
static void handle_pong_packet(struct rtt_pong_packet *pong_pkt)
{
    uint64_t now = rte_get_timer_cycles();
    uint64_t rtt_cycles = now - pong_pkt->echoed_ts;
    double rtt_us = (double)rtt_cycles * 1000000.0 / (double)pong_pkt->tsc_hz;
    
    pong_count++;
    
    // 记录RTT样本
    if (sample_count < MAX_RTT_SAMPLES) {
        rtt_samples[sample_count] = rtt_us;
        sample_count++;
    }
    
    // 更新统计信息
    if (rtt_us < min_rtt) min_rtt = rtt_us;
    if (rtt_us > max_rtt) max_rtt = rtt_us;
    sum_rtt += rtt_us;
    
    printf("收到 pong #%u 从节点 %u: RTT = %.3f μs\n", 
           pong_count, pong_pkt->dst_id, rtt_us);
}

// 自定义数据包处理函数
static void process_rtt_packets(void)
{
    struct rte_mbuf *rx_bufs[32];
    uint16_t nb_rx = rte_eth_rx_burst(global_config.port_id, 0, rx_bufs, 32);

    for (int i = 0; i < nb_rx; i++) {
        struct rte_ether_hdr *eth_hdr = rte_pktmbuf_mtod(rx_bufs[i], struct rte_ether_hdr *);
        if (eth_hdr->ether_type != rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4)) {
            rte_pktmbuf_free(rx_bufs[i]);
            continue;
        }

        struct rte_ipv4_hdr *ip_hdr = (struct rte_ipv4_hdr *)(eth_hdr + 1);
        if (ip_hdr->next_proto_id != IPPROTO_UDP) {
            rte_pktmbuf_free(rx_bufs[i]);
            continue;
        }

        struct rte_udp_hdr *udp_hdr = (struct rte_udp_hdr *)(ip_hdr + 1);
        if (udp_hdr->dst_port != rte_cpu_to_be_16(RAFT_PORT)) {
            rte_pktmbuf_free(rx_bufs[i]);
            continue;
        }

        uint8_t *payload = (uint8_t *)(udp_hdr + 1);
        uint8_t mtype = payload[0];
        
        if (mtype == MSG_PING_RTT) {
            // 收到ping，发送pong回复
            struct rtt_ping_packet *ping_pkt = (struct rtt_ping_packet *)payload;
            
            struct rtt_pong_packet pong_pkt;
            pong_pkt.msg_type = MSG_PONG_RTT;
            pong_pkt.dst_id = ping_pkt->src_id;
            pong_pkt.echoed_ts = ping_pkt->send_ts;
            pong_pkt.tsc_hz = ping_pkt->tsc_hz;
            
            send_rtt_pong_packet(&pong_pkt, ping_pkt->src_id);
            printf("收到 ping 从节点 %u，发送 pong 回复\n", ping_pkt->src_id);
        }
        else if (mtype == MSG_PONG_RTT) {
            // 收到pong，处理RTT测量
            struct rtt_pong_packet *pong_pkt = (struct rtt_pong_packet *)payload;
            handle_pong_packet(pong_pkt);
        }
        
        rte_pktmbuf_free(rx_bufs[i]);
    }
}

// 打印最终统计结果
static void print_statistics(void)
{
    printf("\n=== RTT测试统计结果 ===\n");
    printf("目标节点: %u\n", target_node_id);
    printf("发送ping包: %u\n", ping_count);
    printf("收到pong包: %u\n", pong_count);
    printf("丢包率: %.2f%%\n", 
           ping_count > 0 ? (double)(ping_count - pong_count) * 100.0 / ping_count : 0.0);
    
    if (pong_count > 0) {
        double avg_rtt = sum_rtt / pong_count;
        printf("RTT统计 (μs):\n");
        printf("  最小值: %.3f\n", min_rtt);
        printf("  最大值: %.3f\n", max_rtt);
        printf("  平均值: %.3f\n", avg_rtt);
        
        // 计算标准差
        if (sample_count > 1) {
            double variance = 0.0;
            for (uint32_t i = 0; i < sample_count; i++) {
                double diff = rtt_samples[i] - avg_rtt;
                variance += diff * diff;
            }
            variance /= (sample_count - 1);
            double stddev = sqrt(variance);
            printf("  标准差: %.3f\n", stddev);
        }
    }
    printf("========================\n");
}

int main(int argc, char **argv)
{
    int ret;
    
    // 初始化EAL
    ret = rte_eal_init(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "EAL初始化失败\n");
    
    argc -= ret;
    argv += ret;
    
    // 解析应用参数
    ret = parse_args(argc, argv);
    if (ret < 0)
        rte_exit(EXIT_FAILURE, "参数解析失败\n");
    
    // 加载配置
    if (load_config("config.json") < 0)
        rte_exit(EXIT_FAILURE, "配置加载失败\n");
    
    printf("RTT测试工具启动\n");
    printf("本节点ID: %u\n", global_config.node_id);
    printf("目标节点ID: %u\n", target_node_id);
    printf("测试持续时间: %u 秒\n", test_duration);
    printf("ping间隔: %u 毫秒\n", ping_interval_ms);
    
    // 设置信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    // 初始化网络
    net_init();
    
    printf("开始RTT测试...\n");
    
    uint64_t start_time = rte_get_timer_cycles();
    uint64_t last_ping_time = 0;
    uint64_t ping_interval_cycles = (uint64_t)ping_interval_ms * rte_get_timer_hz() / 1000;
    uint64_t test_duration_cycles = (uint64_t)test_duration * rte_get_timer_hz();
    
    // 主循环
    while (!force_quit) {
        uint64_t now = rte_get_timer_cycles();
        
        // 检查是否到达测试结束时间
        if (now - start_time >= test_duration_cycles) {
            printf("测试时间结束\n");
            break;
        }
        
        // 发送ping包
        if (now - last_ping_time >= ping_interval_cycles) {
            send_ping_to_target();
            last_ping_time = now;
        }
        
        // 处理接收到的数据包
        process_rtt_packets();
        
        // 短暂休眠避免CPU占用过高
        usleep(1000); // 1ms
    }
    
    // 打印统计结果
    print_statistics();
    
    printf("RTT测试结束\n");
    return 0;
}