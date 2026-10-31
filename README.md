Home sensors
============

Firmware for sensors around the home. The sensors publish their measurements on the local network, where any device can receive them. The receiver firmware receives the air quality measurements on an ESP32-C6 and logs them.

Requires [ESP-IDF](https://github.com/espressif/esp-idf) v6.1 or later. See the ESP-IDF getting started guide for the [ESP32-C3](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/get-started/index.html) or [ESP32-C6](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c6/get-started/index.html).

Repository layout
-----------------

    firmware/vindriktning/   ESP32-C3 firmware that reads an IKEA Vindriktning and publishes the measurements
    firmware/receiver/       ESP32-C6 firmware that receives the air quality measurements and logs them
    components/              ESP-IDF components shared by the firmwares
    tools/                   Tools for testing from a computer

Each firmware is a separate ESP-IDF project, built from its own directory. All components in `components/` are available to every firmware, and only the ones a firmware uses are built. Components that only one firmware uses live in that firmware's own `components/` directory.

Settings that differ from the ESP-IDF defaults are in each firmware's `sdkconfig.defaults`. The first build creates `sdkconfig` from it, and menuconfig changes are saved there. `sdkconfig` is ignored by git, so to change a setting for everyone, run `idf.py save-defconfig`, which writes the settings that differ from the defaults to `sdkconfig.defaults`, or edit that file by hand.

All firmwares connect to the Wi-Fi network set in `wifi_credentials.h` at the repository root. Before building, copy `wifi_credentials.example.h` to `wifi_credentials.h` and set your network in it. git ignores `wifi_credentials.h`, so the password stays on your machine.

Messages
--------

Measurements are sent over Wi-Fi as UDP multicast, so any device on the network can receive them without a broker. Each message type has its own multicast group, and receivers join the groups of the types they want. Messages are JSON:

    {"v":1,"type":"air_quality","src":"vindriktning","seq":42,"data":{"pm1":3,"pm10":6,"pm25":5}}

`src` is the device name and `seq` counts up per message type, so receivers can tell devices apart and notice lost messages.

To see the messages on a computer on the same network, run from the repository root:

    python3 tools/multicast_receiver.py

It joins the air quality group, 239.255.10.1, on port 45000. To see other message types, add `--group` with each group to join, and `--port` if they use another port.

IKEA Vindriktning
-----------------

Reads PM1, PM2.5 and PM10 measurements from the particle sensor inside an IKEA Vindriktning air quality monitor over UART, and publishes them as `air_quality` messages. Build and flash from `firmware/vindriktning`:

    cd firmware/vindriktning
    idf.py menuconfig
    idf.py build
    idf.py -p PORT flash monitor

The UART port, baud rate and RX pin can be changed under "IKEA Vindriktning Configuration" in `idf.py menuconfig`. The device name, multicast group, port and TTL are in its "Multicast" submenu.

Receiver
--------

Receives the air quality messages and logs them. Build and flash from `firmware/receiver`:

    cd firmware/receiver
    idf.py menuconfig
    idf.py build
    idf.py -p PORT flash monitor

Each message is logged with the device name and IP address of its sender:

    I (8123) Receiver: air_quality from vindriktning (192.168.1.23) #42: {"pm1":3,"pm10":6,"pm25":5}

The multicast group and port can be changed under "Receiver Configuration" → "Multicast" in `idf.py menuconfig`, and must match the air quality group and port set in the sensors. Gaps in `#` mean messages were lost on the way.

*Code in this repository is in the Public Domain (or CC0 licensed, at your option.)
Unless required by applicable law or agreed to in writing, this
software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.*
