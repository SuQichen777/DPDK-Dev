#!/usr/bin/env python3
"""Simple UDP server to print Sense stats reports coming from the DPU."""

import argparse
import socket
import struct
import sys
from typing import List, Tuple


MSG_STATS_REPORT = 20
HEADER_STRUCT = struct.Struct('<BBHIQ')  # msg_type, peer_count, reserved, src_id, seq
PEER_STRUCT = struct.Struct('<IfI')      # peer_id, avg_rtt_us, loss_count


def parse_report(payload: bytes):
    if len(payload) < HEADER_STRUCT.size:
        return None

    msg_type, peer_count, _reserved, src_id, seq = HEADER_STRUCT.unpack_from(payload, 0)
    if msg_type != MSG_STATS_REPORT:
        return None

    peers: List[Tuple[int, float, int]] = []
    offset = HEADER_STRUCT.size
    for _ in range(peer_count):
        if offset + PEER_STRUCT.size > len(payload):
            break
        peer_id, avg_rtt_us, loss_count = PEER_STRUCT.unpack_from(payload, offset)
        peers.append((peer_id, avg_rtt_us, loss_count))
        offset += PEER_STRUCT.size

    return {
        "src_id": src_id,
        "seq": seq,
        "peers": peers,
    }


def main():
    parser = argparse.ArgumentParser(description="Listen for Sense stats reports over UDP")
    parser.add_argument('--host', default='0.0.0.0', help='IP/interface to bind (default: %(default)s)')
    parser.add_argument('--port', type=int, default=9998, help='UDP port to bind (default: %(default)s)')
    args = parser.parse_args()

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.bind((args.host, args.port))

    print(f"Listening for Sense stats on {args.host}:{args.port} ...", flush=True)

    try:
        while True:
            data, addr = sock.recvfrom(4096)
            report = parse_report(data)
            if not report:
                print(f"[{addr[0]}] Ignored packet (len={len(data)})", flush=True)
                continue

            print(f"[seq={report['seq']}] from node {report['src_id']} ({addr[0]})")
            for peer_id, avg_rtt, loss in report['peers']:
                rtt_display = f"{avg_rtt:.3f} us" if avg_rtt >= 0 else "n/a"
                print(f"  peer {peer_id:2d}: avg {rtt_display}, loss {loss}")
            print(flush=True)
    except KeyboardInterrupt:
        print("Interrupted, exiting.", file=sys.stderr)
    finally:
        sock.close()


if __name__ == '__main__':
    main()
