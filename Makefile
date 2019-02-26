#
# This is a project Makefile. It is assumed the directory this Makefile resides in is a
# project subdirectory.
#

PROJECT_NAME := waterMonitor
MKSPIFFS := /Users/brian/esp/mkspiffs/mkspiffs

include $(IDF_PATH)/make/project.mk

flashfilesystem : spiffs.bin
	python $(IDF_PATH)/components/esptool_py/esptool/esptool.py --chip esp32 --port $(CONFIG_ESPTOOLPY_PORT) --baud $(CONFIG_ESPTOOLPY_BAUD) write_flash -z 0x314000 spiffs.bin

spiffs.bin : filesystem
	$(MKSPIFFS) -d 5 -c filesystem -b 4096 -p 256 -s 0x80000 spiffs.bin
