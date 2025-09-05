# Raft App Instruction

This is a specific instruction on how [raft](https://github.com/SuQichen777/DPDK-Dev/tree/master/dpdk-23.11/app/raft) app using dpdk-23.11 works.

## Tree Structure of Raft App
```
app/
└── raft/                      
    ├── include/               # Header files (public interfaces)
    │   ├── config.h           
    │   ├── election.h         
    │   ├── metadata.h         
    │   ├── networking.h       
    │   ├── packet.h           # Packet format definitions
    │   └── timeout.h          
    ├── config.c               # Configuration loader implementation
    ├── config.json            # Runtime configuration
    ├── election.c             # Leader election implementation
    ├── main.c                 # Main entry point (initialization + event loop)
    ├── meson.build            # Meson build configuration file
    ├── metadata.c             # Metadata module implementation
    ├── networking.c           # Networking implementation
    ├── RAFT.md                # Documentation (this project overview)
    └── timeout.c              # Timeout handling implementation
```

It should be notified that functions implemented in `metadata.c` is currently not in use. You should follow the instructions in `main.c` to construct the metadata timer if needed.

```
Actors:
  [CFG] Config      [EAL] DPDK EAL/Timer     [NET] DPDK Net (mbuf/ethdev)
  [RAFT] Local Node   [OTH] Other Followers

--------------------------------------------------------------------------------
Initialization (once)
--------------------------------------------------------------------------------
+------------------+       +--------------------+      +-----------------------+
| load_config()    |-----> | rte_eal_init()     |----> | net_init():           |
|  (config.json)   |       | rte_timer_subsys() |      | - mbuf pool           |
|  IP/MAC/timeouts |       |                    |      | - ethdev cfg/start    |
+------------------+       +--------------------+      +-----------------------+
                                  |
                                  |
                                  v
                         +--------------------+
                         | raft_init(node_id) |
                         |  state=FOLLOWER    |
                         |  start election TL |
                         +--------------------+
                                  |
                                  v
--------------------------------------------------------------------------------
Actors:
  [EAL] DPDK Timer Drive     [NET] DPDK Net (rx_burst/tx_burst)
  [RAFT] Local State Machine [OTH] Other Followers

--------------------------------------------------------------------------------
+==========================================================================+
||                          MAIN EVENT LOOP (steady)                       ||
||                                                                         ||
||  +--------------------+                                                 ||
||  | Packet RX Path     |                                                 ||
||  | [NET]              |                                                 ||
||  | rte_eth_rx_burst() |----> raft_handle_packet() -------------------+  ||
||  +--------------------+                                              |  ||
||                                                                     /   ||
||  +--------------------+                                            /    ||
||  | Timer Drive        |                                           /     ||
||  | [EAL]              |                                          /      ||
||  | rte_timer_manage() | (no timeout)                            /       ||
||  +--------------------+                                        /        ||
||            |                                                  /         ||
||     (timeout fires)                                          /          ||
||            v                                                /           ||
||  +-----------------------------+                           /            ||
||  | election_timeout_cb()       |                          /             ||
||  | [RAFT] FOLLOWER/CANDIDATE   |                         /              ||
||  +-----------------------------+                        /               ||
||            | start_election()                          /                ||
||            v                                          /                 ||
||  +-----------------------------+                     /                  ||
||  | state=CANDIDATE, term++     |                    /                   ||
||  | vote self, reset timer      |                   /                    ||
||  +-----------------------------+                  /                     ||
||            | broadcast VoteReq                   /                      ||
||            v                                    |                       ||
||  +-----------------------------+                |                       ||
||  | send_raft_packet()          | [NET] mbuf, set L2/L3/L4, checksums    ||
||  | rte_eth_tx_burst()          |--------------------------------------- ||
||  +-----------------------------+                                      | ||
||                                                                       | ||
||                               [OTH] process VoteReq, decide grant   <-- ||
||                                   reset their election timer            ||
||                                                                         ||
||  +-----------------------------+                                        ||
||  | RX VoteResp                 | [NET] rx_burst                         ||
||  | -> raft_handle_packet()     |--------------------------------------->||
||  +-----------------------------+                                        ||
||            | tally votes                                                ||
||            | (if majority)                                              ||
||            v                                                            ||
||  +-----------------------------+                                        ||
||  | become LEADER               |                                        ||
||  | stop election timer         |                                        ||
||  +-----------------------------+                                        ||
||            | immediate heartbeat                                        ||
||            v                                                            ||
||  +-----------------------------+                                        ||
||  | raft_send_heartbeat()       | [NET] mbuf/tx_burst                    ||
||  +-----------------------------+--------------------------------------->||
||                               [OTH] process Heartbeat, reset timers     ||
||                                                                         ||
||  (while LEADER: on heartbeat interval -> raft_send_heartbeat() -> TX)   ||
||   - interval check based on monotonic_us() and                          ||
||     global_config.heartbeat_interval_ms                                 ||
+==========================================================================+
--------------------------------------------------------------------------------
```
Notes:
- RX path continuously feeds raft_handle_packet() for VoteReq/VoteResp/Heartbeat.
- Timer drive uses rte_timer_manage(); on election timeout, election_timeout_cb() triggers start_election() and broadcast of VoteReq.
- The IP addresses in `config.json` does not really make sense. Making the MAC addresses   reliable can ensure all nodes communicating with each other.