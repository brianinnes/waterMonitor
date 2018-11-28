# Test environment

Change to the **vagrant** directory of the project and start up the backend environment - this will create a root certificate that will be used in the test environment.  You can use this certificate or provide one for the MQTT broker you will connect to.  The certificate should be copied or moved to the main folder of the project and should be named **m2mqtt_ca.pem** or change the **component.mk** file in the main folder to contain the filename of the root certificate.

If you are using the vagrant test backend and your laptop name is not resolvable in DNS then you need to set the host property for the mqtt client config in **waterMonitor_main.c** file in the main folder.  The configuration is set in function **mqtt_app_start**.
