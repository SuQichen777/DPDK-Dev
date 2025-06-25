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

typedef struct {
    uint32_t self_id;
    uint32_t current_term;
    uint32_t voted_for;
    uint32_t vote_granted;
    raft_state_t current_state;
    uint64_t last_heard_ms;
} raft_node_t;


void raft_init(uint32_t self_id);
void raft_tick(uint64_t now_ms);
void raft_handle_packet(const struct raft_packet *pkt, uint16_t port);
raft_state_t raft_get_state(void);
void raft_send_heartbeat(void);
uint32_t raft_get_node_id(void);
uint32_t raft_get_term(void);

#ifdef __cplusplus
}
#endif

#endif // ELECTION_H
