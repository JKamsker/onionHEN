#!/usr/bin/env python3
"""Package an upstream BlueV PS5 ELF as an optional onionHEN plugin.

Requires llvm-objcopy (the PS5 Payload SDK provides it). This modifies ELF
section metadata only; it does not modify BlueV's executable segments.
"""

import argparse
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile

PLUGIN_ID = "BLUE00001"
PLUGIN_NAME = "BlueV Bluetooth Audio"
DESCRIPTOR_VERSION = "1.00"  # Integration descriptor version, not BlueV's release version.
DESCRIPTOR_SECTION = ".onion_plugin"
MAX_ELF_SIZE = 64 * 1024 * 1024  # onionHEN's default plugin size limit.


class PackagingError(Exception):
    """An incompatible ELF or missing packaging prerequisite."""


def elf_sections(image: bytes) -> dict[str, bytes]:
    """Read ELF64 LE section names and their bytes (as onionHEN does)."""
    if (len(image) < 64 or image[:7] != b"\x7fELF\x02\x01\x01"):
        raise PackagingError("input must be a little-endian ELF64")
    if struct.unpack_from("<HH", image, 16) not in ((2, 62), (3, 62)):
        raise PackagingError("input must be an x86-64 executable or shared object")
    phoff, shoff = struct.unpack_from("<QQ", image, 32)
    phentsize, phnum, shentsize, shnum, shstrndx = struct.unpack_from("<HHHHH", image, 54)
    if (phentsize != 56 or phoff + phentsize * phnum > len(image):
        raise PackagingError("ELF program headers are invalid")
    if (shentsize != 64 or not shnum or shstrndx >= shnum or shoff + 64 * shnum > len(image):
        raise PackagingError("ELF must have a valid section table")
    string_header = shoff + 64 * shstrndx
    string_off, string_size = struct.unpack_from("<QQ", image, string_header + 24)
    if string_off + string_size > len(image):
        raise PackagingError("ELF section names are out of bounds")
    names = image[string_off:string_off + string_size]
    sections = {}
    for i in range(shnum):
        header = shoff + i * 64
        name_off = struct.unpack_from("<I", image, header)[0]
        content_off, content_size = struct.unpack_from("<QQ", image, header + 24)
        if name_off >= len(names):
            raise PackagingError("ELF section name is out of bounds")
        name_end = names.find(b"\0", name_off)
        if name_end == -1:
            raise PackagingError("ELF section name is unterminated")
        name = names[name_off:name_end].decode("utf-8", errors="replace")
        if content_off + content_size > len(image):
            # SHT_NOBITS sections (e.g. .bss) have no bytes in the file.
            section_type = struct.unpack_from("<I", image, header + 4)[0]
            if section_type != 8:
                raise PackagingError(f"ELF section {name} is out of bounds")
        if name == DESCRIPTOR_SECTION:
            sections[name] = image[content_off:content_off + content_size]
    return sections


def descriptor_bytes(version: str) -> bytes:
    if not re.fullmatch(r"[0-9]+\.[0-9]{2}", version) or len(version) >= 16:
        raise PackagingError("descriptor version must be N.NN (e.g. 1.00)")

    def fixed_ascii(text: str, size: int) -> bytes:
        raw = text.encode("ascii")
        if len(raw) >= size:
            raise PackagingError(f"descriptor string exceeds {size - 1} bytes")
        return raw.ljust(size, b"\0")

    # struct_size, ABI v1, capabilities (none), flags (SIGTERM stop supported, no auto-restart).
    return struct.pack("<IIII32s16s64s", 128, 1, 0, (1 << 2),
                       fixed_ascii(PLUGIN_ID, 32), fixed_ascii(version, 16),
                       fixed_ascii(PLUGIN_NAME, 64))


def find_objcopy(explicit: str | None) -> str:
    sdk = os.environ.get("PS5_PAYLOAD_SDK")
    toolchain = os.environ.get("TOOLCHAIN")
    candidates = [explicit] if explicit else [
        str(Path(toolchain) / "llvm-objcopy") if toolchain else None,
        str(Path(sdk) / "bin/llvm-objcopy") if sdk else None,
        shutil.which("llvm-objcopy"),
        shutil.which("objcopy"),
    ]
    for candidate in candidates:
        if candidate and (shutil.which(candidate) or Path(candidate).is_file()):
            return candidate
    raise PackagingError("llvm-objcopy not found (set --objcopy or PS5_PAYLOAD_SDK)")


def package(source: Path, output: Path, version: str, objcopy: str) -> None:
    source = source.resolve()
    output = output.resolve()
    if source == output:
        raise PackagingError("output must differ from the input ELF")
    if not source.is_file():
        raise PackagingError(f"BlueV ELF not found: {source}")
    image = source.read_bytes()
    if len(image) >= MAX_ELF_SIZE:
        raise PackagingError("BlueV ELF exceeds onionHEN's 64 MiB plugin size limit")
    if DESCRIPTOR_SECTION in elf_sections(image):
        raise PackagingError("ELF already has an .onion_plugin descriptor")
    descriptor = descriptor_bytes(version)

    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".bluev-plugin-", dir=output.parent) as directory:
        temporary = Path(directory)
        descriptor_file = temporary / "descriptor.bin"
        staged_output = temporary / "plugin.elf"
        descriptor_file.write_bytes(descriptor)
        command = [objcopy, "--add-section", f"{DESCRIPTOR_SECTION}={descriptor_file}",
                   "--set-section-flags", f"{DESCRIPTOR_SECTION}=readonly",
                   str(source), str(staged_output)]
        try:
            subprocess.run(command, check=True, capture_output=True, text=True)
        except (OSError, subprocess.CalledProcessError) as exc:
            detail = exc.stderr.strip() if isinstance(exc, subprocess.CalledProcessError) else str(exc)
            raise PackagingError(f"objcopy failed: {detail}") from exc
        packaged = staged_output.read_bytes()
        if len(packaged) > MAX_ELF_SIZE or elf_sections(packaged).get(DESCRIPTOR_SECTION) != descriptor:
            raise PackagingError("generated ELF failed the plugin descriptor check")
        os.replace(staged_output, output)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("bluev_elf", type=Path, help="upstream build/release/bluev.elf")
    parser.add_argument("output", type=Path, nargs="?", default=Path(f"{PLUGIN_ID}.elf"))
    parser.add_argument("--version", default=DESCRIPTOR_VERSION,
                        help="onionHEN plugin descriptor version (N.NN; default: 1.00)")
    parser.add_argument("--objcopy", help="path to llvm-objcopy (or compatible objcopy)")
    args = parser.parse_args()
    try:
        package(args.bluev_elf, args.output, args.version, find_objcopy(args.objcopy))
    except (PackagingError, OSError) as exc:
        parser.exit(1, f"error: {exc}\n")
    print(f"Created {args.output} — copy to /data/OnionHEN/plugins/{PLUGIN_ID}.elf")
    return 0


if __name__ == "__main__":
    sys.exit(main())
