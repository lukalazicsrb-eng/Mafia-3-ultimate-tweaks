#!/usr/bin/env python3
"""Verify original Modern Tweaks 2.5.1 F8 AA call, without editing the DLL."""
import argparse
import hashlib
import struct
from pathlib import Path
from zipfile import ZipFile

SHA256 = "529b16daec59e2ce5c735e5c8e7974662b647591da2847c53a16f165a479e900"
CALL_RVA = 0x5EEE3
COMBO_RVA = 0x10E3B0

def offset(data, rva):
    if data[:2] != b"MZ":
        raise ValueError("Not PE")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe+4] != b"PE\x00\x00":
        raise ValueError("Bad PE header")
    n = struct.unpack_from("<H", data, pe+6)[0]
    opt_size = struct.unpack_from("<H", data, pe+20)[0]
    if struct.unpack_from("<H", data, pe+24)[0] != 0x20B:
        raise ValueError("Expected PE32+ x64")
    for i in range(n):
        at = pe+24+opt_size+i*40
        vsize, vaddr, rawsize, rawaddr = struct.unpack_from("<IIII",data,at+8)
        if vaddr <= rva < vaddr+max(vsize,rawsize):
            return rawaddr+rva-vaddr
    raise ValueError("RVA not mapped")

def verify(data):
    digest=hashlib.sha256(data).hexdigest()
    if digest != SHA256:
        raise ValueError("Unrecognized DLL: "+digest)
    count=offset(data,0x5EEC5)
    if data[count:count+6] != bytes.fromhex("41b903000000"):
        raise ValueError("AA item count changed")
    p=offset(data,CALL_RVA)
    if data[p:p+5] != bytes.fromhex("e8c8f40a00"):
        raise ValueError("AA Combo call changed")
    if CALL_RVA+5+struct.unpack_from("<i",data,p+1)[0] != COMBO_RVA:
        raise ValueError("Combo destination mismatch")
    print("Verified original Modern Tweaks v2.5.1 DLL: "+digest)
    print("F8 AA Combo CALL RVA 0x5eee3 -> RVA 0x10e3b0")
    print("Only static integrity verified; no game or FSR runtime test performed.")

if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source",type=Path)
    path=parser.parse_args().source
    if path.suffix.lower()==".zip":
        with ZipFile(path) as z: verify(z.read("dinput8.dll"))
    else:
        verify(path.read_bytes())
