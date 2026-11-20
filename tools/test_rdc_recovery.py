import importlib.util
from pathlib import Path
import struct
import sys
import tempfile
import types
import unittest
from unittest.mock import patch


SOURCE = Path(__file__).resolve().parents[1] / "capture" / "izvezi-snimak.py"
spec = importlib.util.spec_from_file_location("rdc_exporter", SOURCE)
exporter = importlib.util.module_from_spec(spec)
with patch.dict(sys.modules, {"renderdoc": types.SimpleNamespace(ShaderStage=types.SimpleNamespace(Pixel=0))}):
    spec.loader.exec_module(exporter)


class FramePrefixTests(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory()
        self.addCleanup(scratch.cleanup)
        root = Path(scratch.name)
        self.original = root / "original.rdc"
        self.output = root / "copy.rdc"

        name = b"renderdoc/internal/framecapture\0"
        payload = b"synthetic frame data"
        self.header = struct.pack("<8sII16s", b"RDOC\0\0\0\0", 0x102, 32, b"test")
        section = struct.pack("<4sIQQQII", b"\0" * 4, 1, len(payload), len(payload), 1, 0, len(name))
        self.complete = self.header + section + name + payload

    def test_copies_complete_frame_without_touching_original(self):
        original_bytes = self.complete + b"0malformed trailing section"
        self.original.write_bytes(original_bytes)
        exporter.copy_frame_prefix(self.original, self.output)
        self.assertEqual(self.output.read_bytes(), self.complete)
        self.assertEqual(self.original.read_bytes(), original_bytes)

    def test_refuses_incomplete_frame(self):
        self.original.write_bytes(self.complete[:-1])
        with self.assertRaises(ValueError):
            exporter.copy_frame_prefix(self.original, self.output)
        self.assertFalse(self.output.exists())

    def test_leaves_capture_without_trailing_data_alone(self):
        self.original.write_bytes(self.complete)
        with self.assertRaises(ValueError):
            exporter.copy_frame_prefix(self.original, self.output)
        self.assertFalse(self.output.exists())

    def test_refuses_file_without_frame_as_first_section(self):
        other_type = self.header + self.complete[32:36] + b"\x02\x00\x00\x00" + self.complete[40:]
        self.original.write_bytes(other_type + b"0trailer")
        with self.assertRaises(ValueError):
            exporter.copy_frame_prefix(self.original, self.output)
        self.assertFalse(self.output.exists())


if __name__ == "__main__":
    unittest.main()
