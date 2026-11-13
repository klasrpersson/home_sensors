#!/usr/bin/env python3
"""Send a new firmware to a device over Wi-Fi, see components/ota-server."""

import argparse
import http.client
import json
import os
import selectors
import socket
import ssl
import sys
import time

from multicast_receiver import open_group

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
KEYS = os.path.join(REPO, "keys")
# Measurements and diagnostics, a device sends at least one of them
GROUPS = ["239.255.10.1", "239.255.10.2"]


def find_device(name, port, interface, timeout):
    """Return the IP address of the device with this name, from the multicast messages it sends."""
    selector = selectors.DefaultSelector()
    for group in GROUPS:
        selector.register(open_group(group, port, interface), selectors.EVENT_READ)

    deadline = time.monotonic() + timeout
    while (remaining := deadline - time.monotonic()) > 0:
        for key, _ in selector.select(remaining):
            payload, (address, _) = key.fileobj.recvfrom(2048)
            try:
                if json.loads(payload).get("src") == name:
                    return address
            except (ValueError, AttributeError):
                pass
    return None


def push(address, port, image, token, cert):
    """Upload the image, return the HTTP status and the device's answer."""
    # The devices share one self-signed certificate, so it is trusted directly instead of by host name
    context = ssl.create_default_context(cafile=cert)
    context.check_hostname = False

    size = os.path.getsize(image)
    connection = http.client.HTTPSConnection(address, port, context=context, timeout=60)
    connection.putrequest("POST", "/ota")
    connection.putheader("Authorization", f"Bearer {token}")
    connection.putheader("Content-Type", "application/octet-stream")
    connection.putheader("Content-Length", str(size))
    connection.endheaders()

    progress = sys.stdout.isatty()
    sent = 0
    with open(image, "rb") as f:
        while chunk := f.read(4096):
            try:
                connection.send(chunk)
            except OSError:
                # The device closes the connection when it refuses a large image, its answer may still arrive
                break
            sent += len(chunk)
            if progress:
                print(f"\rSent {sent * 100 // size}%", end="", flush=True)
    if progress:
        print()

    response = connection.getresponse()
    return response.status, response.read().decode(errors="replace")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("device", help="device name, as in its messages' src")
    parser.add_argument("image", help="signed app image, e.g. firmware/vindriktning/build/vindriktning.bin")
    parser.add_argument("--ip", help="IP address of the device, instead of finding it by name")
    parser.add_argument("--port", type=int, default=443, help="HTTPS port of the device (default: 443)")
    parser.add_argument("--multicast-port", type=int, default=45000, help="UDP port of the device's messages (default: 45000)")
    parser.add_argument("--interface", default="0.0.0.0", help="IP address of the local interface to find the device on (default: any)")
    parser.add_argument("--keys", default=KEYS, help="folder with ota_token and ota_server_cert.pem (default: keys/)")
    args = parser.parse_args()

    with open(os.path.join(args.keys, "ota_token")) as f:
        token = f.read().strip()
    cert = os.path.join(args.keys, "ota_server_cert.pem")

    address = args.ip
    if address is None:
        print(f"Looking for {args.device}...", flush=True)
        address = find_device(args.device, args.multicast_port, args.interface, timeout=70)
        if address is None:
            sys.exit(f"{args.device} sent no messages for 70 s, use --ip to give its address")
    print(f"Sending {args.image} to {args.device} at {address}", flush=True)

    try:
        status, answer = push(address, args.port, args.image, token, cert)
    except (OSError, ssl.SSLError, http.client.HTTPException) as e:
        sys.exit(f"Update failed: {e}")
    if status != 200:
        sys.exit(f"Update refused ({status}): {answer}")
    print(answer, flush=True)

    print("Waiting for the device to come back...", flush=True)
    # It restarts a second after answering, skip what it sends before that
    time.sleep(5)
    if find_device(args.device, args.multicast_port, args.interface, timeout=70) is None:
        sys.exit(f"{args.device} sent no messages for 70 s after the update")
    print(f"{args.device} is back. It goes back to the old firmware if the new one does not confirm.")


if __name__ == "__main__":
    main()
