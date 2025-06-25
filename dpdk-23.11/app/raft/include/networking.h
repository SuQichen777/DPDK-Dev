// include/networking.h
#ifndef NETWORKING_H
#define NETWORKING_H

#include <rte_ether.h>
#include <rte_udp.h>
#include "packet.h"

void net_init(uint32_t self_id);
void send_raft_packet(struct raft_packet *pkt, uint16_t dst_id);
void process_packets(void);

#endif