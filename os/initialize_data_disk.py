import sys
from pathlib import Path


SECTOR_SIZE = 512
DISK_SECTORS = 65536
SECTORS_PER_CLUSTER = 4
ROOT_ENTRY_COUNT = 512
FAT_COUNT = 2
RESERVED_SECTORS = 1


def create_boot_sector(fat_sectors: int) -> bytes:
    root_sectors = ROOT_ENTRY_COUNT * 32 // SECTOR_SIZE
    sector = bytearray(SECTOR_SIZE)
    sector[0:3] = b"\xEB\x3C\x90"
    sector[3:11] = b"NOVA26H2"
    sector[11:13] = SECTOR_SIZE.to_bytes(2, "little")
    sector[13] = SECTORS_PER_CLUSTER
    sector[14:16] = RESERVED_SECTORS.to_bytes(2, "little")
    sector[16] = FAT_COUNT
    sector[17:19] = ROOT_ENTRY_COUNT.to_bytes(2, "little")
    sector[19:21] = (0).to_bytes(2, "little")
    sector[21] = 0xF8
    sector[22:24] = fat_sectors.to_bytes(2, "little")
    sector[24:26] = (63).to_bytes(2, "little")
    sector[26:28] = (16).to_bytes(2, "little")
    sector[28:32] = (0).to_bytes(4, "little")
    sector[32:36] = DISK_SECTORS.to_bytes(4, "little")
    sector[36] = 0x80
    sector[38] = 0x29
    sector[39:43] = (0x26_10_24).to_bytes(4, "little")
    sector[43:54] = b"NOVA DATA  "
    sector[54:62] = b"FAT16   "
    sector[510:512] = b"\x55\xAA"
    return bytes(sector)


def calculate_fat_sectors() -> int:
    root_sectors = ROOT_ENTRY_COUNT * 32 // SECTOR_SIZE
    fat_sectors = 1
    while True:
        data_sectors = (
            DISK_SECTORS
            - RESERVED_SECTORS
            - FAT_COUNT * fat_sectors
            - root_sectors
        )
        cluster_count = data_sectors // SECTORS_PER_CLUSTER
        required = ((cluster_count + 2) * 2 + SECTOR_SIZE - 1) // SECTOR_SIZE
        if required == fat_sectors:
            return fat_sectors
        fat_sectors = required


def initialize_disk(path: Path) -> None:
    fat_sectors = calculate_fat_sectors()
    boot_sector = create_boot_sector(fat_sectors)
    if path.exists():
        with path.open("rb") as disk:
            existing = disk.read(SECTOR_SIZE)
        if (
            path.stat().st_size != DISK_SECTORS * SECTOR_SIZE
            or existing[510:512] != b"\x55\xAA"
            or existing[54:62] != b"FAT16   "
            or int.from_bytes(existing[32:36], "little") != DISK_SECTORS
        ):
            raise ValueError(
                "{} exists but is not the expected Nova FAT16 data disk; "
                "it was left unchanged.".format(path)
            )
        print("Keeping existing persistent Nova data disk: {}".format(path))
        return

    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("wb") as disk:
        disk.truncate(DISK_SECTORS * SECTOR_SIZE)
        disk.seek(0)
        disk.write(boot_sector)
        fat_offset = RESERVED_SECTORS * SECTOR_SIZE
        fat_header = b"\xF8\xFF\xFF\xFF"
        disk.seek(fat_offset)
        disk.write(fat_header)
        disk.seek(fat_offset + fat_sectors * SECTOR_SIZE)
        disk.write(fat_header)
    print(
        "Created blank {} MiB persistent Nova FAT16 data disk: {}".format(
            DISK_SECTORS * SECTOR_SIZE // (1024 * 1024), path
        )
    )


if __name__ == "__main__":
    if len(sys.argv) != 2:
        raise SystemExit("Usage: initialize_data_disk.py nova-data.img")
    initialize_disk(Path(sys.argv[1]))
