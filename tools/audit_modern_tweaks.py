#!/usr/bin/env python3
"""Report static PE metadata and selected markers from a local Modern Tweaks DLL."""

import argparse
import hashlib
import os
from pathlib import Path, PurePosixPath
import re
from shutil import copyfileobj
import subprocess
from tempfile import TemporaryDirectory
from zipfile import BadZipFile, ZipFile


GRAPHICS_LIBRARIES = {
    "d3d11.dll", "d3d12.dll", "d3dcompiler_47.dll", "dxgi.dll", "vulkan-1.dll"
}
ASCII_MARKERS = (
    "DirectInput8Create",
    "imgui_impl_win32",
    "imgui_impl_dx11",
    r"\ModernTweaks.dat",
    "[Visuals]",
    "TemporalAA=",
    "DLAA=",
    "NVSDK_NGX_D3D11_Init_Ext",
    "NVSDK_NGX_D3D11_CreateFeature",
    "NVSDK_NGX_D3D11_EvaluateFeature",
    "Menu: present hook",
    "[dlaa] the game's TAA pass found",
)
UTF16_MARKERS = (r"\_nvngx.dll", r"\nvngx_dlss.dll")


def inspect_dll(path):
    data = path.read_bytes()
    if not data.startswith(b"MZ"):
        raise ValueError("the DLL does not have an MZ header")

    metadata = subprocess.run(
        ["objdump", "-p", str(path.resolve())],
        capture_output=True,
        env={**os.environ, "LC_ALL": "C"},
        text=True,
        encoding="utf-8",
        errors="replace",
        check=True,
        timeout=30,
    ).stdout

    print(f"DLL size: {len(data):,} bytes")
    print(f"DLL SHA-256: {hashlib.sha256(data).hexdigest()}")
    for line in metadata.splitlines():
        if ":     file format " in line:
            print(line.split(":     ", 1)[1])
        elif "(PE32+)" in line or line.startswith("ImageBase"):
            print(line.strip())

    print("Graphics import libraries:")
    for name in re.findall(r"^\s*DLL Name:\s*(\S+)", metadata, re.MULTILINE):
        if name.lower() in GRAPHICS_LIBRARIES:
            print(f"  {name}")

    print("Graphics import functions:")
    for name in re.findall(
        r"^\s*[0-9a-f]+\s+(?:<none>\s+)?[0-9a-f]+\s+"
        r"((?:D3D|DXGI|CreateDXGIFactory)\w+)",
        metadata,
        re.MULTILINE,
    ):
        print(f"  {name}")

    print("Named PE exports:")
    _, _, exports = metadata.partition("[Ordinal/Name Pointer] Table")
    exports = exports.lstrip("\n").partition("\n\n")[0]
    for name in re.findall(
        r"^\s*\[\s*\d+\]\s+(?:\+base\[\s*\d+\]\s+[0-9a-f]+\s+)?(\S+)",
        exports,
        re.MULTILINE,
    ):
        print(f"  {name}")

    for encoding, markers in (("ascii", ASCII_MARKERS), ("utf-16le", UTF16_MARKERS)):
        print(f"First {encoding} marker offsets:")
        for marker in markers:
            offset = data.find(marker.encode(encoding))
            location = f"0x{offset:08x}" if offset >= 0 else "absent"
            print(f"  {location:>10}  {marker}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="path to dinput8.dll or its original ZIP")
    args = parser.parse_args()

    try:
        if args.input.suffix.lower() == ".zip":
            with ZipFile(args.input) as archive, TemporaryDirectory() as directory:
                dlls = [
                    entry for entry in archive.infolist()
                    if not entry.is_dir()
                    and PurePosixPath(entry.filename.replace("\\", "/")).name.lower()
                    == "dinput8.dll"
                ]
                if len(dlls) != 1:
                    raise ValueError("the ZIP must contain exactly one dinput8.dll")

                dll_path = Path(directory) / "dinput8.dll"
                with archive.open(dlls[0]) as source, dll_path.open("wb") as destination:
                    copyfileobj(source, destination)
                inspect_dll(dll_path)
        else:
            inspect_dll(args.input)
    except subprocess.CalledProcessError as error:
        parser.exit(1, f"objdump could not read the DLL: {error.stderr.strip()}\n")
    except (OSError, ValueError, RuntimeError, BadZipFile, subprocess.TimeoutExpired) as error:
        parser.exit(1, f"Audit failed: {error}\n")


if __name__ == "__main__":
    main()
