# Hardware components and schematic

This project requires the following components:

- ESP32 DevKitC
- Flow meter ??????
- 2 X DFROBOT Gravity TDS Meter V1.0
- DS18b20 temperature sensor in waterproof package
- 4.7K resistor to put between 3.3 and data pins on DS18b20 sensor

The following connections should be made:

- GPIO 34 : data pin for TDS sensor **AFTER** the filter
- GPIO 33 : data pin for TDS sensor **BEFORE** the filter
- GPIO 27 : Flow meter data pin
- GPIO 26 : +'ve pin for TDS sensor **AFTER** the filter
- GPIO 25 : +'ve pin for TDS sensor **BEFORE** the filter
- GPIO 14 : data pin for the temperature sensor (+'ve and -'ve connectors should go to 3.3V and Gnd pins of ESP board)