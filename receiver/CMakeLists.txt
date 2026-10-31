cmake_minimum_required(VERSION 3.22)

# Components shared with the sensor firmware
set(EXTRA_COMPONENT_DIRS ../components/multicast-pubsub ../components/wlan-station)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Only build the components that main depends on
idf_build_set_property(MINIMAL_BUILD ON)

project(vindriktning-receiver)
