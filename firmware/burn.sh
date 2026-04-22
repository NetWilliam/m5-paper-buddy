#!/bin/bash

python /home/netwilliam/esp/v5.5.4/esp-idf/components/spiffs/spiffsgen.py 0xBF0000 main/spiffs_image_data build/spiffs_image.bin && python -m esptool --chip esp32s3 -p /dev/ttyACM0 -b 460800 write_flash 0x410000 build/spiffs_image.bin
