"""USBTMC client for a Rigol DS1000Z scope that reads one packet at a time.

The scope reports 64-byte bulk packets, which is invalid at USB high speed. The xHCI driver assumes 512,
so every packet ends a transfer, and the kernel usbtmc driver loses track of long replies. This client
detaches the kernel driver and keeps reading packets until the reply's header says it is complete.

Needs pyusb and root (or write access to the USB device). Run on its own to send queries:

    python3 rigol.py '*IDN?' ':MEAS:VMAX? CHAN1'
"""
import struct
import sys

import usb.core
import usb.util

EP_OUT = 0x03
EP_IN = 0x82


class Scope:
    def __init__(self):
        self.dev = usb.core.find(idVendor=0x1AB1, idProduct=0x0517)
        if self.dev is None:
            raise RuntimeError("scope not found")
        if self.dev.is_kernel_driver_active(0):
            self.dev.detach_kernel_driver(0)
        usb.util.claim_interface(self.dev, 0)
        self.tag = 1
        # Throw away a reply left over from an earlier, interrupted query
        self.drain()

    def close(self):
        usb.util.release_interface(self.dev, 0)
        self.dev.attach_kernel_driver(0)

    def drain(self):
        count = 0
        while True:
            try:
                count += len(self.dev.read(EP_IN, 512, timeout=300))
            except usb.core.USBTimeoutError:
                return count

    def _header(self, msg_id, size, attributes):
        tag = self.tag
        self.tag = self.tag % 255 + 1
        return tag, struct.pack("<BBBBIBBBB", msg_id, tag, ~tag & 0xFF, 0, size, attributes, 0, 0, 0)

    def write(self, cmd):
        data = (cmd + "\n").encode()
        _, header = self._header(1, len(data), 1)
        self.dev.write(EP_OUT, header + data + b"\0" * (-len(data) % 4), timeout=5000)

    def _read_packet(self, timeout):
        return bytes(self.dev.read(EP_IN, 64 * 1024, timeout=timeout))

    def read(self, timeout=20000):
        result = b""
        while True:
            tag, request = self._header(2, 1 << 20, 0)
            self.dev.write(EP_OUT, request, timeout=5000)
            data = b""
            while len(data) < 12:
                data += self._read_packet(timeout)
            msg_id, reply_tag, _, _, size, attributes = struct.unpack("<BBBBIB", data[:9])
            if msg_id != 2 or reply_tag != tag:
                raise RuntimeError(f"unexpected reply header {data[:12].hex()}")
            payload = data[12:]
            while len(payload) < size:
                payload += self._read_packet(timeout)
            result += payload[:size]
            if attributes & 1:
                return result

    def query(self, cmd):
        self.write(cmd)
        return self.read().decode(errors="replace").strip()

    def query_block(self, cmd):
        """Query a reply in IEEE 488.2 definite-length block format, such as :WAV:DATA?."""
        self.write(cmd)
        data = self.read()
        ndigits = int(data[1:2])
        length = int(data[2:2 + ndigits])
        return data[2 + ndigits:2 + ndigits + length]

    def read_waveform(self, channel):
        """Read the screen waveform of a stopped capture, 1200 points, with its preamble."""
        self.write(f":WAV:SOUR {channel}")
        self.write(":WAV:MODE NORM")
        self.write(":WAV:FORM BYTE")
        # A RAW read leaves the start and stop points beyond the 1200 NORM points
        self.write(":WAV:STAR 1")
        self.write(":WAV:STOP 1200")
        preamble = self.query(":WAV:PRE?").split(",")
        return {"preamble": preamble, "data": list(self.query_block(":WAV:DATA?"))}

    def measure(self, channel, items=("VMAX", "VMIN", "VTOP", "VBASE", "VAVG")):
        return {item: self.query(f":MEAS:{item}? {channel}") for item in items}


if __name__ == "__main__":
    scope = Scope()
    try:
        for cmd in sys.argv[1:]:
            print(f"{cmd} -> {scope.query(cmd)}")
    finally:
        scope.close()
