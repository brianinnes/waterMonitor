# Building the project

## Setup

This project needs the ESP-IDF tooling installed.  Follow the [getting started instructions on the ESP-IDF site.](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html#setup-toolchain)

Configure the project using the makemenu option:

```make menuconfig```

Set the **Partition Table** setting to a Custom partition table CSV and set the Custom partition CSV file to **partitions.csv**

Set the WaterFlow meter configuration options as desired.  This is where you specify the default WiFi the ESP device will connect to and the MQTT broker configuration - the default MQTT settings are set for the test Vagrant environment.

In the **Serial flasher config** settings you can set the default port that the ESP appear as on your system.

Exit and save the project configuration.

## Additional components needed for ds18b20 thermometer

Add the following to the components directory:

- https://www.github.com/DavidAntliff/esp32-ds18b20
- https://github.com/DavidAntliff/esp32-owb

## Build and Flash the binary to the device

To build the binary for the ESP32 device run command:

```make app```

To flash the binary to the ESP32 device run command:

```make flash```

Note it is possible to just use the **make flash** command, as this will also verify the binary is up to date and compile it if not.

## Monitor the device

You can monitor the log output from the device using command:

``` make monitor```

To exit the monitor use the Ctlr-] key combination (*Press and hold the Control key then press the right bracket key **]***)