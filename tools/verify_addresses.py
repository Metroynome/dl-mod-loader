#!/usr/bin/env python3
"""Verify the shared resident SDK and hook in four extracted DL retail cores."""
import argparse
from pathlib import Path
import struct
CHECKS = {0x11d5d8:0x27bdff70,0x12b338:0x27bdfeb0,
          0x12b5c8:0x27bdff60,0x12b980:0x27bdff20,0x126e20:0x24030064,
          0x138df8:0xdfb00000,0x138dfc:0x03e00008,0x138e00:0x27bd0020,
          0x139a44:0xdfb00000,0x139a48:0x03e00008,0x139a4c:0x27bd0040}
if __name__ == '__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('rac4',type=Path)
    args=parser.parse_args()
    for region in ('ntscu','pal','ntscj','ntsck'):
        file=args.rac4/'boot/core1'/('rac4.boot.'+region+'.v100.retail.core.0001.bin')
        data=file.read_bytes()
        for address,expected in CHECKS.items():
            actual=struct.unpack_from('<I',data,address-0x11a180)[0]
            assert actual==expected, (region,hex(address),hex(actual),hex(expected))
        print('Verified SDK and hook:',region)
