"""Print the IPv4 packets on a network interface that come from the sensor, go to a multicast group, or are IGMP.

Shows whether the sensor's messages reach the computer at all, independent of any socket joining the group.
Needs root (raw socket).

    sudo python3 sniff.py wlp82s0 60 192.168.2.211
"""
import socket
import struct
import sys
import time

iface, seconds, sensor = sys.argv[1], float(sys.argv[2]), sys.argv[3]
sock = socket.socket(socket.AF_PACKET, socket.SOCK_RAW, socket.ntohs(0x0003))
sock.bind((iface, 0))
sock.settimeout(1)
start, seen = time.time(), 0
while time.time() - start < seconds:
    try:
        packet = sock.recv(65535)
    except socket.timeout:
        continue
    if packet[12:14] != b"\x08\x00":
        continue
    ip = packet[14:]
    proto, src, dst = ip[9], socket.inet_ntoa(ip[12:16]), socket.inet_ntoa(ip[16:20])
    if src != sensor and not dst.startswith("239.") and proto != 2:
        continue
    ihl = (ip[0] & 15) * 4
    info = f"proto {proto}"
    if proto == 17:
        sport, dport = struct.unpack("!HH", ip[ihl:ihl + 4])
        info = f"udp {sport}->{dport} {ip[ihl + 8:ihl + 8 + 90]!r}"
    elif proto == 2:
        info = f"igmp type 0x{ip[ihl]:02x} group {socket.inet_ntoa(ip[ihl + 4:ihl + 8])}"
    print(f"{time.time() - start:5.1f}s {src} -> {dst} {info}", flush=True)
    seen += 1
print(f"done, {seen} matching packets")
