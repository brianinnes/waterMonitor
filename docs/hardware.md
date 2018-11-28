# Hardware components and schematic

This project requires the following components:

- ESP32 DevKitC
- Flow meter ??????
- 2 X DFROBOT Gravity TDS Meter V1.0

The following connections should be made:

- GPIO 34 : data pin for TDS sensor **AFTER** the filter
- GPIO 33 : data pin for TDS sensor **BEFORE** the filter
- GPIO 27 : Flow meter data pin
- GPIO 26 : +'ve pin for TDS sensor **AFTER** the filter
- GPIO 25 : +'ve pin for TDS sensor **BEFORE** the filter