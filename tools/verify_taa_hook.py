#!/usr/bin/env python3
"""Read-only verification of Modern Tweaks 2.5.1 DrawIndexed hooks.

Requires the original user's own ZIP or dinput8.dll. Does not modify any DLL.
"""
import argparse
import hashlib
from pathlib import Path
import struct
from zipfile import ZipFile

EXPECTED_SHA256 = "529b16daec59e2ce5c735e5c8e7974662b647591da2847c53a16f165a479e900"
TARGET_RVA = 0x83C80
DIRECT_RVA = 0x799D6
INDIRECT_RVA = 0x796ED
DIRECT_CODE = bytes.fromhex("E8 A5 A2 00 00")
INDIRECT_CODE = bytes.fromhex("E8 8E A5 00 00")

def rva_to_offset(data: bytes, rva: int) -> int:
    if data[:2] != b"MZ":
        raise ValueError("not a PE executable")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe:pe + 4] != b"PE\0\0":
        raise ValueError("invalid PE signature")
    count = struct.unpack_from("<H", data, pe + 6)[0]
    header_size = struct.unpack_from("<H", data, pe + 20)[0]
    if struct.unpack_from("<H", data, pe + 24)[0] != 0x20B:
        raise ValueError("expected PE32+ (64-bit DLL)")
    for i in range(count):
        start = pe + 24 + header_size + 40 * i
        vsize, vaddr, rawsize, rawaddr = struct.unpack_from("<IIII", data, start + 8)
        if vaddr <= rva < vaddr + max(vsize, rawsize):
            result = rawaddr + rva - vaddr
            if result >= len(data):
                raise ValueError("RVA maps beyond end of DLL")
            return result
    raise ValueError(f"could not map RVA 0x{rva:x}")

def check_site(data: bytes, name: str, call_rva: int, expected: bytes):
    off = rva_to_offset(data, call_rva)
    actual = data[off:off + 5]
    if actual != expected:
        raise ValueError(f"{name} call bytes changed: {actual.hex(' ')}")
    delta = struct.unpack_from("<i", data, off + 1)[0]
    dest = call_rva + 5 + delta
    if dest != TARGET_RVA:
        raise ValueError(f"{name} calls 0x{dest:x}; expected 0x{TARGET_RVA:x}")
    print(f"{name}: CALL 0x{call_rva:X} -> 0x{dest:X} ({actual.hex(' ')})")

def verify(data: bytes):
    digest = hashlib.sha256(data).hexdigest()
    if digest != EXPECTED_SHA256:
        raise ValueError(f"wrong Modern Tweaks DLL hash: {digest}")
    print(f"Modern Tweaks 2.5.1 SHA256 {digest}")
    check_site(data, "Fallback/indirect DrawIndexed", INDIRECT_RVA, INDIRECT_CODE)
    check_site(data, "Active/direct DrawIndexed", DIRECT_RVA, DIRECT_CODE)
    print("The v5/v6 probe hooked ONLY the fallback RVA 0x796ED.")
    print("The v7 probe hooks the direct RVA 0x799D6 instead.")
    print("These static checks do NOT prove that an FSR frame was processed.")

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("input", type=Path, help="original dinput8.dll or original Modern Tweaks ZIP")
    path = p.parse_args().input
    if path.suffix.lower() == ".zip":
        with ZipFile(path) as z:
            names = [x for x in z.namelist() if x.replace("\\", "/").split("/")[-1].lower() == "dinput8.dll"]
            if len(names) != 1:
                raise ValueError("ZIP must contain exactly one dinput8.dll")
            verify(z.read(names[0]))
    else:
        verify(path.read_bytes())

if __name__ == "__main__":
    main()
