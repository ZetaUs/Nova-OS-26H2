import struct
import sys
from pathlib import Path


PROGRAMS = (
    ("filemgr", 0x400000),
    ("notepad", 0x440000),
    ("browser", 0x480000),
    ("settings", 0x4C0000),
    ("terminal", 0x500000),
)
HEADER_FORMAT = "<HHQQQQ"
HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
MAX_IMAGE_SIZE = 256 * 1024


def read_program(path, load_address):
    data = path.read_bytes()
    if len(data) < HEADER_SIZE or len(data) > MAX_IMAGE_SIZE:
        raise ValueError(f"{path.name} has an invalid ZEX image size.")

    magic, version, entry, code_size, data_size, bss_size = struct.unpack_from(
        HEADER_FORMAT, data
    )
    if magic != 0x575A or version != 2:
        raise ValueError(f"{path.name} has an unsupported ZEX header.")
    if HEADER_SIZE + code_size + data_size != len(data):
        raise ValueError(f"{path.name} section sizes do not match the image.")
    if code_size == 0 or bss_size > MAX_IMAGE_SIZE:
        raise ValueError(f"{path.name} has invalid code or BSS size.")
    if entry < load_address + HEADER_SIZE or entry >= load_address + HEADER_SIZE + code_size:
        raise ValueError(f"{path.name} entry point is outside its code section.")
    if HEADER_SIZE + code_size + data_size + bss_size > MAX_IMAGE_SIZE:
        raise ValueError(f"{path.name} exceeds the ZEX load limit.")
    return data


def format_array(name, data):
    lines = [f"static const uint8_t zex_{name}[] = {{"]
    for offset in range(0, len(data), 16):
        values = ", ".join(f"0x{value:02X}" for value in data[offset:offset + 16])
        lines.append(f"    {values},")
    lines.append("};")
    lines.append(f"static const uint32_t zex_{name}_size = sizeof(zex_{name});")
    return lines


def main():
    if len(sys.argv) != 3:
        raise SystemExit(
            "Usage: generate_zex_apps.py zex_directory output_header"
        )

    source_dir, output_path = map(Path, sys.argv[1:])
    lines = []
    for name, load_address in PROGRAMS:
        lines.extend(
            format_array(
                name,
                read_program(source_dir / f"{name}.zex", load_address),
            )
        )
        lines.append("")
    Path(output_path).write_text("\n".join(lines), encoding="ascii")
    print(f"Embedded {len(PROGRAMS)} validated ZEX applications.")


if __name__ == "__main__":
    main()
