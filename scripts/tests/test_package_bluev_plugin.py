#!/usr/bin/env python3
"""Host-only regression tests for the optional BlueV ELF wrapper."""

import importlib.util
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().parents[1] / "package_bluev_plugin.py"
spec = importlib.util.spec_from_file_location("package_bluev_plugin", SCRIPT)
plugin = importlib.util.module_from_spec(spec)
spec.loader.exec_module(plugin)


class DescriptorTests(unittest.TestCase):
    def test_descriptor_layout(self):
        raw = plugin.descriptor_bytes("1.00")
        self.assertEqual(len(raw), 128)
        self.assertEqual(raw[:16], bytes.fromhex("80000000010000000000000006000000"))
        self.assertEqual(raw[16:48].split(b"\0", 1)[0], b"BLUE00001")
        self.assertEqual(raw[48:64].split(b"\0", 1)[0], b"1.00")
        self.assertEqual(raw[64:128].split(b"\0", 1)[0], b"BlueV Bluetooth Audio")

    def test_invalid_versions(self):
        for version in ("0.4.0", "1.0", "-1.00", "1.000", "1.00a"):
            with self.subTest(version=version), self.assertRaises(plugin.PackagingError):
                plugin.descriptor_bytes(version)

    def test_reject_non_elf(self):
        with self.assertRaises(plugin.PackagingError):
            plugin.elf_sections(b"not an ELF")


@unittest.skipUnless(shutil.which("llvm-objcopy") and (shutil.which("clang") or shutil.which("gcc")),
                     "requires compiler and llvm-objcopy")
class IntegrationTests(unittest.TestCase):
    def test_packaged_elf_has_descriptor_and_runs(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            source = root / "bluev.elf"
            output = root / "BLUE00001.elf"
            compiler = shutil.which("clang") or shutil.which("gcc")
            c_file = root / "hello.c"
            c_file.write_text("int main(void) { return 0; }\n")
            subprocess.run([compiler, str(c_file), "-o", str(source)], check=True)
            original = source.read_bytes()
            self.assertEqual(subprocess.run([sys.executable, str(SCRIPT), str(source), str(output)],
                                            capture_output=True).returncode, 0)
            self.assertEqual(source.read_bytes(), original)
            self.assertEqual(plugin.elf_sections(output.read_bytes())[".onion_plugin"],
                             plugin.descriptor_bytes("1.00"))
            self.assertEqual(subprocess.run([str(output)], capture_output=True).returncode, 0)
            again = subprocess.run([sys.executable, str(SCRIPT), str(output), str(root / "again.elf")],
                                   capture_output=True, text=True)
            self.assertNotEqual(again.returncode, 0)
            self.assertIn("already has", again.stderr)
            self.assertEqual(subprocess.run([sys.executable, str(SCRIPT), str(source), str(source)],
                                            capture_output=True).returncode, 1)


if __name__ == "__main__":
    unittest.main()
