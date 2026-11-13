#!/usr/bin/env python3
"""Test the sensor firmware in Espressif's esp-emu emulator.

Boots firmware/vindriktning in an emulated ESP32-C3, plays the PM1006 on its UART1, and sends updates to its
OTA server. Checks the parser, the refused updates, a good update and the rollbacks. Needs:

- esp-emu, see https://github.com/espressif/esp-emulator, on PATH or given with --esp-emu
- the keys in keys/ and the Wi-Fi network in wifi_credentials.h, as for a normal build
- two builds of the sensor firmware, made with --build in an ESP-IDF environment such as the dev container,
  or by hand with the commands that are printed when they are missing

The emulator's network does not pass multicast, so the sensor's messages are not checked.
"""

import argparse
import os
import re
import secrets
import shutil
import socket
import subprocess
import sys
import tempfile
import threading
import time

import ota_push

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FIRMWARE = os.path.join(REPO, "firmware", "vindriktning")
BUILD = os.path.join(FIRMWARE, "build")
# Second firmware to update to, with a version of its own and a short confirm timeout
TEST_BUILD = os.path.join(BUILD, "emu-test")
TEST_VERSION = "emu-test"
TEST_CONFIRM_TIMEOUT = 20
TEST_DEFAULTS = f"""CONFIG_APP_PROJECT_VER_FROM_CONFIG=y
CONFIG_APP_PROJECT_VER="{TEST_VERSION}"
CONFIG_OTA_SERVER_CONFIRM_TIMEOUT={TEST_CONFIRM_TIMEOUT}
"""

# Where the app description is in an app image, after the image and first segment headers
APP_DESC_OFFSET = 24 + 8
PROJECT_NAME_OFFSET = APP_DESC_OFFSET + 48


def build():
    """Build the firmware and the test firmware, needs idf.py on PATH."""
    os.makedirs(TEST_BUILD, exist_ok=True)
    defaults = os.path.join(TEST_BUILD, "test.defaults")
    with open(defaults, "w") as f:
        f.write(TEST_DEFAULTS)
    subprocess.run(["idf.py", "build", "merge-bin"], cwd=FIRMWARE, check=True)
    subprocess.run(["idf.py", "-B", TEST_BUILD, f"-DSDKCONFIG={os.path.join(TEST_BUILD, 'sdkconfig')}",
                    f"-DSDKCONFIG_DEFAULTS=sdkconfig.defaults;{defaults}", "build"], cwd=FIRMWARE, check=True)


def check_builds():
    """Exit with the build commands if a build is missing."""
    needed = [os.path.join(BUILD, "merged-binary.bin"), os.path.join(BUILD, "vindriktning.elf"),
              os.path.join(TEST_BUILD, "vindriktning.bin"), os.path.join(TEST_BUILD, "vindriktning-unsigned.bin"),
              os.path.join(TEST_BUILD, "vindriktning.elf")]
    missing = [path for path in needed if not os.path.exists(path)]
    if missing:
        sys.exit("Missing " + ", ".join(os.path.relpath(p, REPO) for p in missing) +
                 "\nBuild them with --build in an ESP-IDF environment, such as the dev container.")


def wifi_credentials():
    """Return the SSID and password from wifi_credentials.h."""
    with open(os.path.join(REPO, "wifi_credentials.h")) as f:
        text = f.read()
    values = {}
    for name in ("WIFI_SSID", "WIFI_PASSWORD"):
        match = re.search(rf'^#define {name} "((?:[^"\\]|\\.)*)"', text, re.MULTILINE)
        if match is None:
            sys.exit(f"{name} not found in wifi_credentials.h")
        values[name] = re.sub(r"\\(.)", r"\1", match.group(1))
    return values["WIFI_SSID"], values["WIFI_PASSWORD"]


def free_port():
    with socket.socket() as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def pm1006_frame(pm25):
    """A PM1006 frame with this PM2.5, as the Vindriktning's sensor sends it."""
    frame = bytearray([0x16, 0x11, 0x0B, 0, 0, pm25 >> 8, pm25 & 0xFF, 0, 0, 0x03, 0xCB, 0, 0, 0, 0x0C, 0x01, 0, 0, 0])
    frame.append(-sum(frame) & 0xFF)
    return bytes(frame)


class Emulator:
    """The sensor firmware running in esp-emu, with its console log and the PM1006 on UART1."""

    def __init__(self, esp_emu, workdir):
        self.log_path = os.path.join(workdir, "console.log")
        flash = os.path.join(workdir, "flash.bin")
        shutil.copy(os.path.join(BUILD, "merged-binary.bin"), flash)
        self.ota_port = free_port()
        self.uart1_port = free_port()
        self.control_port = free_port()
        ssid, password = wifi_credentials()

        self.log = open(self.log_path, "w")
        self.process = subprocess.Popen(
            [esp_emu, "--chip", "esp32c3", "--firmware", flash,
             "--elf", os.path.join(BUILD, "vindriktning.elf"), os.path.join(TEST_BUILD, "vindriktning.elf"),
             "--wifi-ssid", ssid, "--wifi-password", password, "--wifi-auth", "wpa2-psk" if password else "open",
             "--net", f"user,hostfwd=tcp:127.0.0.1:{self.ota_port}-:443",
             "--uart1-tcp", f"127.0.0.1:{self.uart1_port}", "--control-tcp", f"127.0.0.1:{self.control_port}",
             "--log-file", os.path.join(workdir, "emulator.log")],
            stdout=self.log, stderr=subprocess.STDOUT, stdin=subprocess.DEVNULL)

        self.uart1 = self._connect(self.uart1_port)
        self.control = self._connect(self.control_port)
        self.control_reader = self.control.makefile("r")
        self.feeding = threading.Event()
        threading.Thread(target=self._feed, daemon=True).start()

    def _connect(self, port):
        deadline = time.monotonic() + 10
        while True:
            try:
                return socket.create_connection(("127.0.0.1", port), timeout=10)
            except OSError:
                if time.monotonic() > deadline or self.process.poll() is not None:
                    raise
                time.sleep(0.2)

    def _feed(self):
        """Send a frame every second while feeding is set, cycling through PM2.5 values."""
        values = [10, 20, 30, 40, 50]
        i = 0
        while True:
            if self.feeding.is_set():
                try:
                    self.uart1.sendall(pm1006_frame(values[i % len(values)]))
                except OSError:
                    return
                i += 1
            time.sleep(1)

    def send_uart1(self, data):
        self.uart1.sendall(data)

    def position(self):
        """Current end of the console log, for wait() to search from."""
        self.log.flush()
        return os.path.getsize(self.log_path)

    def text(self, start=0):
        """Console log from start, with plain newlines and no colors."""
        with open(self.log_path, "rb") as f:
            f.seek(start)
            text = f.read().decode(errors="replace")
        return re.sub(r"\x1b\[[0-9;]*m", "", text.replace("\r", ""))

    def wait(self, pattern, start, timeout):
        """Wait for a line matching pattern after start, return the match or None."""
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            match = re.search(pattern, self.text(start), re.MULTILINE)
            if match:
                return match
            if self.process.poll() is not None:
                return None
            time.sleep(0.2)
        return None

    def reset(self):
        self.control.sendall(b"reset\n")
        return self.control_reader.readline().strip()

    def push(self, image, token=None, cert=None):
        """Send an update, return the HTTP status and answer, or the exception."""
        keys = os.path.join(REPO, "keys")
        if token is None:
            with open(os.path.join(keys, "ota_token")) as f:
                token = f.read().strip()
        if cert is None:
            cert = os.path.join(keys, "ota_server_cert.pem")
        try:
            return ota_push.push("127.0.0.1", self.ota_port, image, token, cert, progress=False)
        except Exception as e:  # noqa: BLE001, any failure is a test result
            return None, f"{type(e).__name__}: {e}"

    def stop(self):
        self.process.terminate()
        try:
            self.process.wait(10)
        except subprocess.TimeoutExpired:
            self.process.kill()
        self.log.close()


class Results:
    def __init__(self):
        self.failed = 0

    def check(self, name, ok, detail=""):
        print(f"{'PASS' if ok else 'FAIL'}  {name}" + (f": {detail}" if detail and not ok else ""), flush=True)
        if not ok:
            self.failed += 1
        return ok


def run_tests(emu, workdir):
    results = Results()
    test_image = os.path.join(TEST_BUILD, "vindriktning.bin")
    unsigned_image = os.path.join(TEST_BUILD, "vindriktning-unsigned.bin")

    # Boot
    booted = emu.wait(r"ota_server: Waiting for updates on port 443, running version (\S+)", 0, 60)
    if not results.check("boots and starts the update server", booted is not None):
        return results
    version = booted.group(1)
    results.check("joins Wi-Fi", emu.wait(r"wlan_station: Connected, got ip", 0, 30) is not None)

    # Parser
    start = emu.position()
    emu.send_uart1(bytes([0xFF, 0xDF, 0xFF, 0xDF, 0xFF, 0xDF, 0xFF]))
    results.check("rejects a frame of the wrong length", emu.wait(r"Invalid data length\. \(7\)", start, 10) is not None)
    time.sleep(0.5)
    start = emu.position()
    bad = bytearray(pm1006_frame(7))
    bad[6] ^= 0x01
    emu.send_uart1(bytes(bad))
    results.check("rejects a frame with a wrong checksum", emu.wait(r"Invalid checksum", start, 10) is not None)
    start = emu.position()
    emu.feeding.set()
    averages = emu.wait(r"AVG PM25=10$(?:\n.*)*?\n.*AVG PM25=15$(?:\n.*)*?\n.*AVG PM25=20$", start, 20)
    results.check("averages PM2.5 over the last three frames", averages is not None,
                  "expected 10, 15, 20 for frames of 10, 20, 30")
    emu.feeding.clear()
    time.sleep(1.5)

    # Refused updates
    status, answer = emu.push(test_image, token=secrets.token_hex(32))
    results.check("refuses a wrong token", status == 401, f"{status} {answer}")

    other = os.path.join(workdir, "other-firmware.bin")
    with open(test_image, "rb") as f:
        image = bytearray(f.read())
    image[PROJECT_NAME_OFFSET:PROJECT_NAME_OFFSET + 32] = b"receiver".ljust(32, b"\0")
    with open(other, "wb") as f:
        f.write(image)
    status, answer = emu.push(other)
    results.check("refuses another firmware", status == 400 and "another firmware" in answer, f"{status} {answer}")

    status, answer = emu.push(unsigned_image)
    results.check("refuses an unsigned image", status == 400 and "signed" in answer, f"{status} {answer}")

    # The system trust store does not have the devices' self-signed certificate
    status, answer = emu.push(test_image, cert="")
    results.check("tool refuses a device without the pinned certificate", status is None and "CERTIFICATE" in answer,
                  f"{status} {answer}")

    # Update that does not confirm, as no measurements arrive
    start = emu.position()
    status, answer = emu.push(test_image)
    results.check("takes a signed update", status == 200, f"{status} {answer}")
    results.check("boots the update", emu.wait(rf"App version:\s+{TEST_VERSION}$", start, 60) is not None)
    emu.wait(r"ota_server: Waiting for updates", start, 30)
    status, answer = emu.push(test_image)
    results.check("refuses an update while the new firmware is unconfirmed", status == 409, f"{status} {answer}")
    results.check(f"goes back when not confirmed within {TEST_CONFIRM_TIMEOUT} s",
                  emu.wait(rf"not confirmed within {TEST_CONFIRM_TIMEOUT} s(?:.*\n)*?.*App version:\s+{re.escape(version)}$",
                           start, TEST_CONFIRM_TIMEOUT + 60) is not None)

    # Update that restarts before confirming
    emu.wait(r"ota_server: Waiting for updates", start, 30)
    time.sleep(1)
    start = emu.position()
    status, answer = emu.push(test_image)
    booted = emu.wait(r"ota_server: Waiting for updates on port 443, running version " + TEST_VERSION, start, 60)
    results.check("takes the update again", status == 200 and booted is not None, f"{status} {answer}")
    start = emu.position()
    results.check("control channel resets", emu.reset() == "ok")
    results.check("goes back when restarted before confirming",
                  emu.wait(rf"App version:\s+{re.escape(version)}$", start, 60) is not None)

    # Update that confirms after its first measurement, and stays after a restart
    emu.wait(r"ota_server: Waiting for updates", start, 30)
    emu.feeding.set()
    time.sleep(1)
    start = emu.position()
    status, answer = emu.push(test_image)
    results.check("confirms after the first measurement",
                  status == 200 and emu.wait(r"New firmware confirmed", start, 60) is not None, f"{status} {answer}")
    start = emu.position()
    emu.reset()
    stays = emu.wait(rf"App version:\s+{TEST_VERSION}$", start, 60)
    pending = re.search(r"must confirm", emu.text(start)) if stays else None
    results.check("keeps the confirmed update after a restart", stays is not None and pending is None)

    return results


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--build", action="store_true", help="build both firmwares first, needs idf.py on PATH")
    parser.add_argument("--esp-emu", default=shutil.which("esp-emu") or os.path.expanduser("~/.local/bin/esp-emu"),
                        help="esp-emu binary (default: from PATH or ~/.local/bin)")
    parser.add_argument("--keep", action="store_true", help="keep the logs of a passing run")
    args = parser.parse_args()

    if not os.path.exists(args.esp_emu):
        sys.exit(f"esp-emu not found at {args.esp_emu}, install it from https://github.com/espressif/esp-emulator")
    if args.build:
        build()
    check_builds()

    workdir = tempfile.mkdtemp(prefix="emu-test-")
    emu = Emulator(args.esp_emu, workdir)
    try:
        results = run_tests(emu, workdir)
    finally:
        emu.stop()

    if results.failed:
        print(f"\n{results.failed} failed, logs in {workdir}")
        sys.exit(1)
    print("\nAll passed" + (f", logs in {workdir}" if args.keep else ""))
    if not args.keep:
        shutil.rmtree(workdir)


if __name__ == "__main__":
    main()
