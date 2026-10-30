IKEA Vindriktning
=================

Reads PM1, PM2.5 and PM10 measurements from the particle sensor inside an IKEA Vindriktning air quality monitor over UART, using an ESP32-C3.

Requires [ESP-IDF](https://github.com/espressif/esp-idf) v6.1 or later. See the [ESP-IDF docs](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/get-started/index.html) for getting started instructions.

Build and flash with:

    idf.py build
    idf.py -p PORT flash monitor

The UART port, baud rate and RX pin can be changed under "IKEA Vindriktning Configuration" in `idf.py menuconfig`.

Project settings that differ from the ESP-IDF defaults are in `sdkconfig.defaults`. The first build creates `sdkconfig` from it, and menuconfig changes are saved there. `sdkconfig` is ignored by git, so to change a setting for everyone, edit `sdkconfig.defaults` by hand. `idf.py save-defconfig` also writes that file, but it copies your Wi-Fi password into it too.

Publishing over the network
---------------------------

Measurements are sent over Wi-Fi as UDP multicast, so any device on the network can receive them without a broker. Each message type has its own multicast group, and receivers join the groups of the types they want. Messages are JSON:

    {"v":1,"type":"air_quality","src":"vindriktning","seq":42,"data":{"pm1":3,"pm10":6,"pm25":5}}

`src` is the device name and `seq` counts up per message type, so receivers can tell devices apart and notice lost messages.

Set the Wi-Fi network, device name, multicast group and port under "IKEA Vindriktning Configuration" in `idf.py menuconfig`. They are saved in `sdkconfig`, which git ignores, so the Wi-Fi password stays on your machine.

To see the messages on a computer on the same network, run:

    python3 tools/multicast_receiver.py

*Code in this repository is in the Public Domain (or CC0 licensed, at your option.)
Unless required by applicable law or agreed to in writing, this
software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.*
