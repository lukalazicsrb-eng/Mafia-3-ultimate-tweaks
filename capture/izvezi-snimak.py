"""Export metadata for candidate Mafia III temporal passes using RenderDoc 1.47."""

import os
from pathlib import Path
import struct
import tempfile
import zlib

import renderdoc as rd


PIXEL = rd.ShaderStage.Pixel
# Shader IDs used by Luma's Mafia III port as corroborating evidence.
MAFIA_SHADERS = {
    0xB00E89BC: "EncodeMotionVectors",
    0xE4A815CF: "LinearizeDepth",
    0x4671BB12: "PreTAA",
    0x2200CBD7: "TAA",
    0xE781A41B: "TAA (transparent)",
    0xA8D4D208: "TAA (subsurface)",
    0xF4D0E9C2: "Tonemap",
}
FILE_HEADER = struct.Struct("<8sII16s")
SECTION_HEADER = struct.Struct("<4sIQQQII")
FRAME_SECTION = b"renderdoc/internal/framecapture\0"


class CaptureOpenError(RuntimeError):
    pass


def complete_frame_end(source, size):
    raw_header = source.read(FILE_HEADER.size)
    if len(raw_header) != FILE_HEADER.size:
        return None
    magic, version, header_size, _ = FILE_HEADER.unpack(raw_header)
    if magic != b"RDOC\0\0\0\0" or version not in (0x100, 0x101, 0x102):
        return None
    if not FILE_HEADER.size <= header_size <= size - SECTION_HEADER.size:
        return None

    source.seek(header_size)
    marker, kind, stored, unpacked, _, _, name_size = SECTION_HEADER.unpack(
        source.read(SECTION_HEADER.size)
    )
    if marker != b"\0\0\0\0" or kind != 1 or name_size != len(FRAME_SECTION):
        return None
    if source.read(name_size) != FRAME_SECTION or not stored or not unpacked:
        return None

    end = source.tell() + stored
    return end if end < size else None


def copy_frame_prefix(path, destination):
    with path.open("rb") as source:
        end = complete_frame_end(source, os.fstat(source.fileno()).st_size)
        if end is None:
            raise ValueError("Ne mogu da izdvojim ceo prvi frejm iz ovog snimka.")
        source.seek(0)
        with destination.open("xb") as output:
            remaining = end
            while remaining:
                block = source.read(min(remaining, 4 * 1024 * 1024))
                if not block:
                    raise ValueError("Snimak se promenio tokom citanja.")
                output.write(block)
                remaining -= len(block)


def draws(actions):
    for action in actions:
        if action.flags & rd.ActionFlags.Drawcall and action.numIndices in (3, 6):
            yield action
        yield from draws(action.children)


def pixel_resources(pipe, shader):
    if shader is None:
        return {}
    bindings = shader.readOnlyResources
    return {
        bindings[d.access.index].fixedBindNumber + d.access.arrayElement: d.descriptor
        for d in pipe.GetReadOnlyResources(PIXEL)
        if d.access.index < len(bindings)
    }


def texture_format(descriptor):
    return descriptor.format.Name() if descriptor is not None else ""


def describe_tex(descriptor, textures):
    if descriptor is None or descriptor.resource == rd.ResourceId():
        return "unbound"
    tex = textures.get(str(descriptor.resource))
    size = f"{tex.width}x{tex.height} samples {tex.msSamp}" if tex is not None else ""
    return f"{descriptor.resource} {texture_format(descriptor)} {size}".rstrip()


def describe_frame(controller, report):
    actions = list(draws(controller.GetRootActions()))
    textures = {str(t.resourceId): t for t in controller.GetTextures()}
    count = 0
    for action in actions:
        controller.SetFrameEvent(action.eventId, False)
        pipe = controller.GetPipelineState()
        shader = pipe.GetShaderReflection(PIXEL)
        crc = zlib.crc32(shader.rawBytes) if shader and shader.rawBytes else 0
        name = MAFIA_SHADERS.get(crc)
        srvs = pixel_resources(pipe, shader)
        outputs = pipe.GetOutputTargets()
        color = outputs[0] if outputs else None
        # The shipped mod looks for three PS inputs and an HDR output.
        hdr_output = "R11G11B10" in texture_format(color).upper()
        motion_bound = 2 in srvs and srvs[2].resource != rd.ResourceId()
        if not name and not (hdr_output and 0 in srvs and 1 in srvs and motion_bound):
            continue

        count += 1
        viewport = pipe.GetViewport(0)
        report.write(f"\nEvent {action.eventId}, PS {crc:08X}, {name or 'kandidat'}\n")
        report.write(f"  Viewport {viewport.width:g}x{viewport.height:g}\n")
        for slot in range(5):
            report.write(f"  PS t{slot} {describe_tex(srvs.get(slot), textures)}\n")
        report.write(f"  RTV0 {describe_tex(color, textures)}\n")
        report.write(f"  Depth {describe_tex(pipe.GetDepthTarget(), textures)}\n")
        if count == 64:
            report.write("\nPrikazana su prva 64 kandidata.\n")
            break
    report.write(f"\nKandidati {count} / pregledani draw prolazi {len(actions)}\n")


def open_capture(path, report):
    cap = rd.OpenCaptureFile()
    try:
        started = cap.OpenFile(str(path), "", None)
        if started.code != rd.ResultCode.Succeeded:
            raise CaptureOpenError(started.Message())
        if cap.DriverName() != "D3D11":
            raise RuntimeError(f"Ocekivan je D3D11, snimak je {cap.DriverName()}")
        started, controller = cap.OpenCapture(rd.ReplayOptions(), None)
        if started.code != rd.ResultCode.Succeeded:
            raise RuntimeError(started.Message())
        try:
            describe_frame(controller, report)
        finally:
            controller.Shutdown()
    finally:
        cap.Shutdown()


def analyze_capture(path, directory, report):
    try:
        open_capture(path, report)
    except CaptureOpenError as error:
        if "Unrecognised section type" not in str(error):
            raise
        report.write(f"Originalni snimak nije otvoren. {error}\n")
        with tempfile.TemporaryDirectory(prefix="rdc-", dir=directory) as temporary:
            copy = Path(temporary) / "frejm.rdc"
            copy_frame_prefix(path, copy)
            open_capture(copy, report)
        report.write("Prvi frejm je uspesno analiziran iz privremene kopije.\n")


def main():
    directory = Path(os.environ["MAFIA_CAPTURE_DIRECTORY"])
    selected = os.environ.get("MAFIA_CAPTURE_FILE")
    captures = [Path(selected)] if selected else sorted(directory.glob("*.rdc"))[:3]
    successful = 0
    with (directory / "RenderDoc analiza.txt").open("w", encoding="utf-8") as report:
        report.write("RenderDoc 1.47 / D3D11\n")
        for capture in captures:
            report.write(f"\nSnimak {capture.name}\n")
            try:
                analyze_capture(capture, directory, report)
                successful += 1
            except Exception as error:
                report.write(f"Analiza nije zavrsena. {error}\n")
        report.write(f"\nUspesno analizirani snimci {successful} od {len(captures)}\n")


if __name__ == "__main__":
    main()
    raise SystemExit
