# Water Monitor

ESP32 project to monitor water quality

## Building the project

This project needs the ESP-IDF tooling installed.  Follow the [getting started instructions on the ESP-IDF site.](https://docs.espressif.com/projects/esp-idf/en/latest/get-started/index.html#setup-toolchain)

Configure the project using the makemenu option:

```make menuconfig```

Set the **Partition Table** setting to a Custom partition table CSV and set the Custom partition CSV file to **partitions.csv**

Set the WaterFlow meter configuration options as desired.  This is where you specify the default WiFi the ESP device will connect to and the MQTT broker configuration - the default MQTT settings are set for the test Vagrant environment.

In the **Serial flasher config** settings you can set the default port that the ESP appear as on your system.

Exit and save the project configuration.

Change to the **vagrant** directory of the project and start up the backend environment - this will create a root certificate that will be used in the test environment.  You can use this certificate or provide one for the MQTT broker you will connect to.  The certificate should be copied or moved to the main folder of the project and should be named **m2mqtt_ca.pem** or change the **component.mk** file in the main folder to contain the filename of the root certificate.

If you are using the vagrant test backend and your laptop name is not resolvable in DNS then you need to set the host property for the mqtt client config in **waterMonitor_main.c** file in the main folder.  The configuration is set in function **mqtt_app_start**.

To build the binary for the ESP32 device run command:

```make app```

