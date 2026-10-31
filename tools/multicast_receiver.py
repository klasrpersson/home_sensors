#!/usr/bin/env python3
"""Print messages sent by the multicast-pubsub component."""

import argparse
import json
import selectors
import socket
import struct


def open_group(group, port, interface):
    """Open a socket that receives the messages sent to a multicast group."""
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    # Bind to the group address, so the socket only gets messages sent to this group. Bound to any
    # address, Linux would also deliver every other group joined on this computer, and unicast to the port.
    # Windows cannot bind to a group address, and only delivers the groups the socket joined anyway.
    try:
        sock.bind((group, port))
    except OSError:
        sock.bind(("", port))
    membership = struct.pack("4s4s", socket.inet_aton(group), socket.inet_aton(interface))
    sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, membership)
    return sock


def print_message(payload, address, last_seq):
    """Print a message, noting lost messages and restarted sequences."""
    try:
        message = json.loads(payload)
        src, msg_type, seq, data = message["src"], message["type"], message["seq"], message["data"]
        if not (isinstance(src, str) and isinstance(msg_type, str) and isinstance(seq, int)) or isinstance(seq, bool):
            raise TypeError("src and type must be strings and seq an integer")
    except (ValueError, KeyError, TypeError):
        print(f"{address}: invalid message {payload!r}", flush=True)
        return

    # Include the address, so devices that kept the same name are tracked separately
    key = (address, src, msg_type)
    note = ""
    if key in last_seq:
        if seq > last_seq[key] + 1:
            note = f" (missed {seq - last_seq[key] - 1})"
        elif seq < last_seq[key]:
            note = " (sequence restarted)"
    last_seq[key] = seq

    print(f"{address} {src} {msg_type} #{seq}{note}: {json.dumps(data)}", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--group", action="append", help="multicast group to join, can be repeated (default: 239.255.10.1)")
    parser.add_argument("--port", type=int, default=45000, help="UDP port (default: 45000)")
    parser.add_argument("--interface", default="0.0.0.0", help="IP address of the local interface to receive on (default: any)")
    args = parser.parse_args()
    groups = args.group or ["239.255.10.1"]

    selector = selectors.DefaultSelector()
    for group in groups:
        selector.register(open_group(group, args.port, args.interface), selectors.EVENT_READ)
    print(f"Listening on {', '.join(groups)} port {args.port}", flush=True)

    last_seq = {}
    while True:
        for key, _ in selector.select():
            payload, (address, _) = key.fileobj.recvfrom(2048)
            print_message(payload, address, last_seq)


if __name__ == "__main__":
    main()
