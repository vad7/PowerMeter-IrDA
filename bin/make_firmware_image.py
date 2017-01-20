#!/usr/bin/env python
#
# ESP8266 make firmware image
# 
# Arguments: dir of *.bin
#
# (c) vad7

import argparse
import os

argp = argparse.ArgumentParser()
argp.add_argument('flashsize', action='store', help='Flash size, kb')
argp.add_argument('dir', action='store', help='Directory of *.bin')
args = argp.parse_args()
fout_name = args.dir + "firmware.bin"
fout = open(fout_name, "wb")
fin = open(args.dir + "0x00000.bin", "rb")
data = fin.read()
fin.close()
data += b"\xFF" * (0x7000 - len(data))
fin = open(args.dir + "0x07000.bin", "rb")
data2 = fin.read()
fin.close()
data = data + data2
fout.write(data)
fout.flush()
size = os.fstat(fout.fileno()).st_size
fout.close()
print "Make: " + fout_name
if int(args.flashsize) == 512:
	webfs = (size + 0xFFF) & 0xFF000
	maxota = (0x7B000 / 2) & 0xFF000
else:
	webfs = 0x80000
	maxota = 0x7B000
print "Firmware size: " + str(size) + ", WebFS addr: " + str(webfs) + ", Max OTA size: " + str(maxota)
print "Space available for OTA: " + str(maxota - size)
