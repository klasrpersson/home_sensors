Home sensors
============

Firmware for sensors around the home. The sensors publish their measurements on the local network, where an ESP32-C6 receives them.

Requires [ESP-IDF](https://github.com/espressif/esp-idf) v6.1 or later. See the [ESP-IDF docs](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/get-started/index.html) for getting started instructions.

Repository layout
-----------------

    firmware/vindriktning/   ESP32-C3 firmware that reads an IKEA Vindriktning and publishes the measurements
    firmware/receiver/       ESP32-C6 firmware that receives the measurements and logs them
    components/              ESP-IDF components shared by the firmwares
    tools/                   Tools for testing and updating from a computer

Each firmware is a separate ESP-IDF project, built from its own directory. All components in `components/` are available to every firmware, and only the ones a firmware uses are built. Components that only one firmware uses live in that firmware's own `components/` directory.

Settings that differ from the ESP-IDF defaults are in each firmware's `sdkconfig.defaults`. The first build creates `sdkconfig` from it, and menuconfig changes are saved there. `sdkconfig` is ignored by git, so to change a setting for everyone, run `idf.py save-defconfig`, which writes the settings that differ from the defaults to `sdkconfig.defaults`, or edit that file by hand.

All firmwares connect to the Wi-Fi network set in `wifi_credentials.h` at the repository root. Before building, copy `wifi_credentials.example.h` to `wifi_credentials.h` and set your network in it. git ignores `wifi_credentials.h`, so the password stays on your machine.

The sensor firmware can be updated over Wi-Fi, see [Over-the-air updates](#over-the-air-updates). It needs keys in `keys/` at the repository root, which git also ignores. Create them once with `tools/generate_ota_keys.sh`.

Messages
--------

Measurements are sent over Wi-Fi as UDP multicast, so any device on the network can receive them without a broker. Each message type has its own multicast group, and receivers join the groups of the types they want. Messages are JSON:

    {"v":1,"type":"air_quality","src":"vindriktning","seq":42,"data":{"pm25":5}}

`src` is the device name and `seq` counts up per message type, so receivers can tell devices apart and notice lost messages.

Sensors also send `diagnostics` messages every 60 seconds to their own group, 239.255.10.2 by default. The `device-diagnostics` component sends them:

    {"v":1,"type":"diagnostics","src":"vindriktning","seq":7,"data":{"rssi":-66,"uptime":420}}

`rssi` is the Wi-Fi signal strength in dBm, left out while not connected, and `uptime` the seconds since the device started.

To see the messages on a computer on the same network, run from the repository root:

    python3 tools/multicast_receiver.py

IKEA Vindriktning
-----------------

Reads PM2.5 measurements from the PM1006 particle sensor inside an IKEA Vindriktning air quality monitor over UART, and publishes them as `air_quality` messages. The PM1006 only measures PM2.5; the PM1 and PM10 fields that some projects read are only defined on the PM1006K. Build and flash from `firmware/vindriktning`:

    cd firmware/vindriktning
    idf.py menuconfig
    idf.py build
    idf.py -p PORT erase-flash flash monitor

`erase-flash` is only needed the first time a board gets this firmware with over-the-air updates, as it brings a new partition table. After that, updates can be sent over Wi-Fi.

The UART port, baud rate and RX pin can be changed under "IKEA Vindriktning Configuration" in `idf.py menuconfig`. Set the device name, multicast group and port in the same menu. The diagnostics group, port and interval are under "Component config" → "Device diagnostics", where an interval of 0 turns diagnostics off.

Receiver
--------

Receives the air quality and diagnostics messages and logs them. Build and flash from `firmware/receiver`:

    cd firmware/receiver
    idf.py menuconfig
    idf.py build
    idf.py -p PORT flash monitor

Each message is logged with the device name and IP address of its sender:

    I (8123) Receiver: air_quality from vindriktning (192.168.1.23) #42: {"pm25":5}
    I (8456) Receiver: diagnostics from vindriktning (192.168.1.23) #7: {"rssi":-66,"uptime":420}

The multicast group and port can be changed under "Receiver Configuration" in `idf.py menuconfig`, and the diagnostics group and port under "Component config" → "Device diagnostics". They must match the sensors. Gaps in `#` mean messages were lost on the way.

Over-the-air updates
--------------------

The sensor firmware runs an HTTPS server, the `ota-server` component, that takes new firmware over Wi-Fi. To build and send one, from the repository root:

    (cd firmware/vindriktning && idf.py build)
    python3 tools/ota_push.py tova firmware/vindriktning/build/vindriktning.bin

`tools/ota_push.py` finds the device by the name in its messages, or by `--ip`. A device only takes an update when:

- the connection uses the certificate in `keys/ota_server_cert.pem`, which the tool trusts and nothing else,
- the request has the token in `keys/ota_token`,
- the image is the same firmware as the one running, and is signed with `keys/signing_key.pem`. The build signs it.

The device then restarts into the new firmware, which has to confirm that it works by sending its first measurement within 5 minutes. If it does not, or restarts before that, the device goes back to the previous firmware. Updates are refused until the running firmware has confirmed.

Keep a copy of `keys/` somewhere safe. Without `signing_key.pem`, devices can only be updated over USB. The token and the server key are built into the firmware like the Wi-Fi password, so anyone holding a board can read them. They can still not make it accept an image over Wi-Fi without the signing key. Hardware Secure Boot and flash encryption, which would also protect against that, are not enabled, as they cannot be turned off again.

Testing in the emulator
-----------------------

`tools/emu_test.py` runs the sensor firmware in Espressif's [esp-emu](https://github.com/espressif/esp-emulator) emulator, without a board. It plays the PM1006 on the emulated UART1 and sends updates to the emulated OTA server, and checks that:

- frames of the wrong length or with a wrong checksum are rejected, and PM2.5 is averaged over three frames,
- updates with a wrong token, for another firmware or without a signature are refused, and the tool refuses a server without the pinned certificate,
- a new firmware goes back to the previous one if it does not confirm in time or restarts first, and updates are refused until it confirms,
- a firmware that confirms after its first measurement stays after a restart.

It needs two builds: the firmware, and a test firmware with another version and a 20 second confirm timeout, which it updates to. In an ESP-IDF environment, such as the dev container, `--build` makes both. esp-emu runs outside it too, so the builds can also be made in the dev container and the tests run on the computer:

    python3 tools/emu_test.py --build    # with idf.py and esp-emu
    python3 tools/emu_test.py            # with existing builds

The emulator does not pass multicast out of its network, so the measurement and diagnostics messages are not checked. Tested with esp-emu 1.0.0.

*Code in this repository is in the Public Domain (or CC0 licensed, at your option.)
Unless required by applicable law or agreed to in writing, this
software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.*
