import os
import sys
import tempfile
from datetime import datetime
from pathlib import Path


SECTOR_SIZE = 2048
BOOT_CATALOG_SECTOR = 19
BOOT_IMAGE_SECTOR = 20


def both_endian(value, size):
    return value.to_bytes(size, "little") + value.to_bytes(size, "big")


def directory_record(name, extent, size, flags=0):
    encoded_name = name if isinstance(name, bytes) else name.encode("ascii")
    record_size = 33 + len(encoded_name) + (1 if len(encoded_name) % 2 == 0 else 0)
    record = bytearray(record_size)
    record[0] = record_size
    record[2:10] = both_endian(extent, 4)
    record[10:18] = both_endian(size, 4)
    now = datetime.now()
    record[18:25] = bytes(
        (now.year - 1900, now.month, now.day, now.hour, now.minute, now.second, 0)
    )
    record[25] = flags
    record[28:32] = both_endian(1, 2)
    record[32] = len(encoded_name)
    record[33 : 33 + len(encoded_name)] = encoded_name
    return record


def volume_descriptor(volume_sectors, root_sector, path_table_sector, image_size):
    descriptor = bytearray(SECTOR_SIZE)
    descriptor[0:7] = b"\x01CD001\x01"
    descriptor[8:40] = b"".ljust(32, b" ")
    descriptor[40:72] = b"NOVA_OS_26H2".ljust(32, b" ")
    descriptor[80:88] = both_endian(volume_sectors, 4)
    descriptor[120:124] = both_endian(1, 2)
    descriptor[124:128] = both_endian(1, 2)
    descriptor[128:132] = both_endian(SECTOR_SIZE, 2)
    path_table_size = 9
    descriptor[132:140] = both_endian(path_table_size, 4)
    descriptor[140:144] = path_table_sector.to_bytes(4, "little")
    descriptor[148:152] = path_table_sector.to_bytes(4, "big")
    descriptor[156:190] = directory_record(b"\x00", root_sector, SECTOR_SIZE, flags=2)
    descriptor[190:318] = b"".ljust(128, b" ")
    descriptor[318:446] = b"NOVA OS".ljust(128, b" ")
    descriptor[446:574] = b"Nova OS build".ljust(128, b" ")
    descriptor[574:702] = b"".ljust(128, b" ")
    descriptor[702:739] = b"".ljust(37, b" ")
    descriptor[739:776] = b"".ljust(37, b" ")
    descriptor[776:813] = b"".ljust(37, b" ")
    timestamp = datetime.now().strftime("%Y%m%d%H%M%S00").encode("ascii") + b"\x00"
    descriptor[813:830] = timestamp
    descriptor[830:847] = timestamp
    descriptor[847:864] = b"0" * 16 + b"\x00"
    descriptor[864:881] = b"0" * 16 + b"\x00"
    descriptor[881] = 1
    if image_size <= 0:
        raise ValueError("Boot image must not be empty.")
    return descriptor


def boot_record(catalog_sector):
    descriptor = bytearray(SECTOR_SIZE)
    descriptor[0:7] = b"\x00CD001\x01"
    descriptor[7:39] = b"EL TORITO SPECIFICATION".ljust(32, b"\x00")
    descriptor[39:71] = b"".ljust(32, b"\x00")
    descriptor[71:75] = catalog_sector.to_bytes(4, "little")
    return descriptor


def boot_catalog():
    catalog = bytearray(SECTOR_SIZE)
    catalog[0] = 1
    catalog[1] = 0
    catalog[4:28] = b"NOVA OS 26H2".ljust(24, b"\x00")
    catalog[30:32] = b"\x55\xaa"
    checksum = (
        -sum(
            int.from_bytes(catalog[i : i + 2], "little")
            for i in range(0, 32, 2)
            if i != 28
        )
    ) & 0xFFFF
    catalog[28:30] = checksum.to_bytes(2, "little")

    entry = 32
    catalog[entry] = 0x88
    catalog[entry + 1] = 0x00
    catalog[entry + 6 : entry + 8] = (265).to_bytes(2, "little")
    catalog[entry + 8 : entry + 12] = BOOT_IMAGE_SECTOR.to_bytes(4, "little")
    return catalog


def path_table(root_sector):
    table = bytearray(9)
    table[0] = 1
    table[2:6] = root_sector.to_bytes(4, "little")
    table[6:8] = (1).to_bytes(2, "little")
    table[8] = 0
    return table.ljust(SECTOR_SIZE, b"\x00")


def root_directory(image_size, root_sector):
    records = (
        directory_record(b"\x00", root_sector, SECTOR_SIZE, flags=2)
        + directory_record(b"\x01", root_sector, SECTOR_SIZE, flags=2)
        + directory_record("NOVA-OS.IMG;1", BOOT_IMAGE_SECTOR, image_size)
    )
    return records.ljust(SECTOR_SIZE, b"\x00")


def build_iso(image_path, output_path):
    image_path = Path(image_path)
    output_path = Path(output_path)
    image_size = image_path.stat().st_size
    if image_size % SECTOR_SIZE:
        raise ValueError("Boot image size must be a multiple of 2048 bytes.")
    with image_path.open("rb") as image:
        if image_size < 512:
            raise ValueError("Boot image is too small.")
        image.seek(510)
        if image.read(2) != b"\x55\xaa":
            raise ValueError("Boot image is missing the BIOS 0xAA55 signature.")

    image_sectors = image_size // SECTOR_SIZE
    root_sector = BOOT_IMAGE_SECTOR + image_sectors
    path_table_sector = root_sector + 1
    volume_sectors = path_table_sector + 1
    descriptors = (
        volume_descriptor(volume_sectors, root_sector, path_table_sector, image_size),
        boot_record(BOOT_CATALOG_SECTOR),
        bytes((0xFF,)) + b"CD001\x01" + bytes(SECTOR_SIZE - 7),
        boot_catalog(),
    )

    output_path.parent.mkdir(parents=True, exist_ok=True)
    fd, temporary_name = tempfile.mkstemp(
        prefix=output_path.name + ".", suffix=".tmp", dir=output_path.parent
    )
    try:
        with os.fdopen(fd, "wb") as output:
            output.write(bytes(16 * SECTOR_SIZE))
            for descriptor in descriptors:
                output.write(descriptor)
            with image_path.open("rb") as image:
                while True:
                    chunk = image.read(1024 * 1024)
                    if not chunk:
                        break
                    output.write(chunk)
            output.write(root_directory(image_size, root_sector))
            output.write(path_table(root_sector))
            output.flush()
            os.fsync(output.fileno())
        os.replace(temporary_name, output_path)
    except Exception:
        try:
            os.unlink(temporary_name)
        except FileNotFoundError:
            pass
        raise

    expected_size = volume_sectors * SECTOR_SIZE
    if output_path.stat().st_size != expected_size:
        raise OSError(
            f"ISO size mismatch: expected {expected_size}, got {output_path.stat().st_size}."
        )
    print(f"Created bootable Nova ISO: {output_path} ({expected_size} bytes)")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("Usage: package_iso.py <boot-image.img> <output.iso>")
    try:
        build_iso(sys.argv[1], sys.argv[2])
    except (OSError, ValueError) as error:
        raise SystemExit(f"ISO creation failed: {error}")
