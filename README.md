IKEA Vindriktning
=================

Reads PM1, PM2.5 and PM10 measurements from the particle sensor inside an IKEA Vindriktning air quality monitor over UART, using an ESP32-C3, and publishes them on the local network. The [receiver](#receiver) firmware in `receiver/` receives them on an ESP32-C6.

Requires [ESP-IDF](https://github.com/espressif/esp-idf) v6.1 or later. See the [ESP-IDF docs](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/get-started/index.html) for getting started instructions.

Copy `wifi_credentials.example.h` to `wifi_credentials.h` and set your Wi-Fi network in it. git ignores `wifi_credentials.h`, so the password stays on your machine. Then build and flash with:

    idf.py build
    idf.py -p PORT flash monitor

The UART port, baud rate and RX pin can be changed under "IKEA Vindriktning Configuration" in `idf.py menuconfig`.

Project settings that differ from the ESP-IDF defaults are in `sdkconfig.defaults`. The first build creates `sdkconfig` from it, and menuconfig changes are saved there. `sdkconfig` is ignored by git, so to change a setting for everyone, run `idf.py save-defconfig`, which writes the settings that differ from the defaults to `sdkconfig.defaults`, or edit that file by hand.

Publishing over the network
---------------------------

Measurements are sent over Wi-Fi as UDP multicast, so any device on the network can receive them without a broker. Each message type has its own multicast group, and receivers join the groups of the types they want. Messages are JSON:

    {"v":1,"type":"air_quality","src":"vindriktning","seq":42,"data":{"pm1":3,"pm10":6,"pm25":5}}

`src` is the device name and `seq` counts up per message type, so receivers can tell devices apart and notice lost messages.

Set the device name, multicast group and port under "IKEA Vindriktning Configuration" in `idf.py menuconfig`.

To see the messages on a computer on the same network, run:

    python3 tools/multicast_receiver.py

Receiver
--------

`receiver/` is firmware for an ESP32-C6 that receives the air quality messages and logs them. It is a separate ESP-IDF project, sharing the `multicast-pubsub` and `wlan-station` components with the sensor firmware, and connects to the same Wi-Fi network in `wifi_credentials.h`. Build and flash it from its directory:

    cd receiver
    idf.py menuconfig
    idf.py build
    idf.py -p PORT flash monitor

Each message is logged with the device name and IP address of its sender:

    I (8123) Receiver: air_quality from vindriktning (192.168.1.23) #42: {"pm1":3,"pm10":6,"pm25":5}

The multicast group and port can be changed under "Vindriktning Receiver Configuration" in `idf.py menuconfig`, and must match the sensor firmware. Gaps in `#` mean messages were lost on the way.

*Code in this repository is in the Public Domain (or CC0 licensed, at your option.)
Unless required by applicable law or agreed to in writing, this
software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.*
