import subprocess
import time
import os
from datetime import datetime

LEADER_LOGICAL_NAME = "node4"
NODE_HOSTS = {
    "node0": "c1gpu010.clemson.cloudlab.us",
    "node1": "c1gpu022.clemson.cloudlab.us",
    "node2": "c1gpu017.clemson.cloudlab.us",
    "node3": "clgpu011.clemson.cloudlab.us",
    "node4": "c1gpu021.clemson.cloudlab.us",
    "node5": "c1gpu014.clemson.cloudlab.us",
    "node6": "clgpu020.clemson.cloudlab.us",
}
USERNAME = "Jiaxi"
RAFT_PROCESS_NAME = "dpdk-raft"
CSV_OUTPUT_FILE = "failover_timestamps.csv"


def get_wall_time_us():
    return int(time.time() * 1_000_000)


def ensure_ssh_agent_socket():
    if "SSH_AUTH_SOCK" not in os.environ or not os.environ["SSH_AUTH_SOCK"]:
        agent_sock = os.popen("echo $SSH_AUTH_SOCK").read().strip()
        if agent_sock:
            os.environ["SSH_AUTH_SOCK"] = agent_sock
            print(f"[INFO] SSH_AUTH_SOCK set to: {agent_sock}")
        else:
            print("[WARN] SSH agent socket not found. SSH may fail.")


def ssh_kill_raft_node(host):
    print(f"[INFO] Connecting to {host} and killing raft-node")

    full_cmd = f"/usr/bin/env ssh {USERNAME}@{host} 'sudo pkill -9 {RAFT_PROCESS_NAME}'"
    result = subprocess.run(
        full_cmd,
        shell=True,
        executable="/bin/zsh",
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if result.returncode != 0:
        print(f"[ERROR] Failed to kill process on {host}:\n{result.stderr.decode()}")
    else:
        print(f"[OK] raft-node killed on {host}")


def main():
    ensure_ssh_agent_socket()

    host = NODE_HOSTS[LEADER_LOGICAL_NAME]
    ssh_kill_raft_node(host)

    t_fail_us = get_wall_time_us()
    now_str = datetime.now().isoformat()
    print(f"[T_FAIL] Leader killed at {t_fail_us} µs ({now_str})")

    with open(CSV_OUTPUT_FILE, "a") as f:
        f.write(f"{LEADER_LOGICAL_NAME},{host},{t_fail_us},{now_str}\n")

    print(f"[LOG] T_fail written to {CSV_OUTPUT_FILE}")


if __name__ == "__main__":
    main()
