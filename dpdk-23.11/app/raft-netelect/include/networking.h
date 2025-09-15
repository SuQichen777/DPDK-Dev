// include/networking.h
#ifndef NETWORKING_H
#define NETWORKING_H

#include <rte_ether.h>
#include <rte_udp.h>
#include "packet.h"

void net_init(void);
void send_raft_packet(struct raft_packet *pkt, uint16_t dst_id);
void send_ps_packet(struct ps_broadcast_packet *pkt, uint16_t dst_id);
void send_ping_packet(uint32_t dst_id);
void send_pong_packet(uint32_t dst_id, uint64_t echoed_ts, uint64_t tsc_hz);
void send_rtt_ping_packet(struct rtt_ping_packet *pkt, uint16_t dst_id);
void send_rtt_pong_packet(struct rtt_pong_packet *pkt, uint16_t dst_id);
void process_packets(void);

#endif