IKEA Vindriktning
=================

Reads PM1, PM2.5 and PM10 measurements from the particle sensor inside an IKEA Vindriktning air quality monitor over UART, using an ESP32-C3.

Requires [ESP-IDF](https://github.com/espressif/esp-idf) v6.1 or later. See the [ESP-IDF docs](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/get-started/index.html) for getting started instructions.

Build and flash with:

    idf.py build
    idf.py -p PORT flash monitor

The UART port, baud rate and RX pin can be changed under "IKEA Vindriktning Configuration" in `idf.py menuconfig`.

*Code in this repository is in the Public Domain (or CC0 licensed, at your option.)
Unless required by applicable law or agreed to in writing, this
software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
CONDITIONS OF ANY KIND, either express or implied.*
