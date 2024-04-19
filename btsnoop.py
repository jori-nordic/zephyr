#!/usr/bin/env python

import sys
import time
import struct
from enum import Enum

class HCIPktType(Enum):
        HCI_CMD = 0x01,
        HCI_ACL_DATA = 0x02,
        HCI_SYNC_DATA = 0x03,
        HCI_EVT = 0x04,
        HCI_ISO_DATA = 0x05

def encode_file_hdr(file):
        identification_pattern = b'btsnoop\x00'
        version_number = 1
        datalink_type = 1002

        btsnoop_hdr = struct.pack(
                '>8sII',
                identification_pattern,
                version_number,
                datalink_type
        )

        file.write(btsnoop_hdr)

def send_data(file, pkt_type, opcode, data):
        original_len = len(data)
        included_len = len(data)

        pkt_flags = 0

        cumulative_drops = 0

        timestamp = int(time.time() * 1e6)

        packet = struct.pack('<HH', opcode, len(data)) + data
        file.write(struct.pack('>IIIIq', original_len, included_len, cumulative_drops, timestamp) + packet)

def main():
        filename = 'log.btsnoop'
        with open(filename, 'wb') as btsnoop_file:
                encode_file_hdr(btsnoop_file)


