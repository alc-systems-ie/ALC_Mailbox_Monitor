cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(alc_mailbox_monitor)

target_sources(app PRIVATE
  src/main.cpp
  src/app.cpp
  src/adxl367.cpp
  src/led.cpp
  src/modem.cpp
  src/mqtt.cpp
  src/npm1300.cpp
  src/fuel_gauge.cpp
  src/retained.cpp
  src/orientation.cpp
)
