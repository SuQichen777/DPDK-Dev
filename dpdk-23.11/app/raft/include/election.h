// include/election.h
#ifndef ELECTION_H
#define ELECTION_H

#include <stdint.h>
#include <stdbool.h>
#include "packet.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    STATE_FOLLOWER,
    STATE_CANDIDATE,
    STATE_LEADER
} raft_state_t;

void raft_init(uint32_t self_id);
void raft_tick(uint64_t now_ms);
void raft_handle_packet(const struct raft_packet *pkt, uint16_t port);
raft_state_t raft_get_state(void);
void raft_send_heartbeat(void);

#ifdef __cplusplus
}
#endif

#endif // ELECTION_H
