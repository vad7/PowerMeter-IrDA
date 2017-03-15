rem esptool.py --port COM9 -b 460800 read_flash 0 524288 firmware.bin
esptool.py --port COM5 -b 460800 read_flash %1 %2 %3
