#pragma once
#include <stdint.h>

void net_init(void);
void process_rx(void);
void send_ping_packet(uint32_t peer_id);
void send_pong_packet(uint32_t dst_id, uint64_t echoed_ts, uint64_t tsc_hz);