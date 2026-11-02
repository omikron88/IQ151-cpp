#!/usr/bin/env python3
"""Small tape fixture using the upstream Txt2Csw modulation (GPL-3.0-or-later).
2000 pilot cycles; each byte is a start cycle then eight complemented LSB-first
bits, 11 samples per half-cycle, 500 pilot cycles between CR-terminated lines.
The initial extra low sample matches CswFile.setRecord(true)'s len=1.
"""
from pathlib import Path
import struct
samples=[False]
def tone(bit):
    samples.extend([not bit]*11+[bit]*11)
for _ in range(2000): tone(False)
for line in ['10 PRINT "TAPE OK"','20 END','\x1b']:
    for c in line+'\r':
        b=(ord(c)|0x80)^0xff
        tone(True)
        for i in range(8):tone(bool(b&(1<<i)))
    for _ in range(500):tone(False)
runs=[];last=samples[0];length=0
for s in samples:
    if s!=last:runs.append(length);length=0;last=s
    length+=1
runs.append(length)
header=b'Compressed Square Wave\x1a\x02\x00'+struct.pack('<II',22050,len(runs))+b'\x01\x00\x00'+b'IQ-151 test\0\0\0\0\0'
assert len(header)==52
payload=b''.join(bytes([n]) if n<256 else b'\0'+struct.pack('<I',n) for n in runs)
Path(__file__).parents[1].joinpath('fixtures/hello.bas.csw').write_bytes(header+payload)
