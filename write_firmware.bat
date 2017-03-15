rem esptool.py --port COM9 -b 460800 read_flash 0 524288 firmware.bin
rem esptool.py --port COM5 -b 460800 write_flash -ff 80m -fm qio -fs 32m 0x99000 0x00000.bin
esptool.py --port COM5 -b 460800 write_flash -ff 80m -fm qio -fs 32m %1 %2
