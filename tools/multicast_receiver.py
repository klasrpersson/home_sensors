#!/usr/bin/env python3
"""Print messages sent by the multicast-pubsub component."""

import argparse
import json
import socket
import struct


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--group", action="append", help="multicast group to join, can be repeated (default: 239.255.10.1)")
    parser.add_argument("--port", type=int, default=45000, help="UDP port (default: 45000)")
    parser.add_argument("--interface", default="0.0.0.0", help="IP address of the local interface to receive on (default: any)")
    args = parser.parse_args()
    groups = args.group or ["239.255.10.1"]

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    sock.bind(("", args.port))
    for group in groups:
        membership = struct.pack("4s4s", socket.inet_aton(group), socket.inet_aton(args.interface))
        sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, membership)
    print(f"Listening on {', '.join(groups)} port {args.port}", flush=True)

    last_seq = {}
    while True:
        payload, (address, _) = sock.recvfrom(2048)
        try:
            message = json.loads(payload)
            key = (message["src"], message["type"])
            seq = message["seq"]
            data = message["data"]
        except (ValueError, KeyError, TypeError):
            print(f"{address}: invalid message {payload!r}", flush=True)
            continue

        missed = ""
        if key in last_seq and seq > last_seq[key] + 1:
            missed = f" (missed {seq - last_seq[key] - 1})"
        last_seq[key] = seq

        print(f"{address} {message['src']} {message['type']} #{seq}{missed}: {json.dumps(data)}", flush=True)


if __name__ == "__main__":
    main()
