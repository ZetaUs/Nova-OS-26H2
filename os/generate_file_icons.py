import sys
from pathlib import Path

from generate_logo import decode_png


def encode_runs(indices):
    runs = bytearray()
    start = 0
    while start < len(indices):
        value = indices[start]
        end = start + 1
        while end < len(indices) and indices[end] == value and end - start < 255:
            end += 1
        runs.extend((end - start, value))
        start = end
    return runs


def read_icon(path):
    width, height, pixels = decode_png(path)
    if width != 96 or height != 100:
        raise ValueError("{} must be 96x100 pixels.".format(path))

    colors = []
    indices = bytearray()
    for offset in range(0, len(pixels), 4):
        color = tuple(pixels[offset:offset + 4])
        if color not in colors:
            colors.append(color)
        indices.append(colors.index(color))
    return colors, indices


def write_header(path, icons):
    palette = []
    encoded = {}
    for name, (colors, indices) in icons.items():
        for color in colors:
            if color not in palette:
                palette.append(color)
        encoded[name] = indices

    if len(palette) > 256:
        raise ValueError("File icon palette exceeds 256 colors.")
    palette_indices = {color: index for index, color in enumerate(palette)}

    lines = [
        "#define FILE_MANAGER_ICON_WIDTH 96",
        "#define FILE_MANAGER_ICON_HEIGHT 100",
        "static const uint32_t file_manager_icon_palette[] = {",
    ]
    for start in range(0, len(palette), 8):
        values = []
        for red, green, blue, alpha in palette[start:start + 8]:
            values.append("0x{:02X}{:02X}{:02X}{:02X}".format(
                alpha, red, green, blue))
        lines.append("    " + ", ".join(values) + ",")
    lines.append("};")

    for name, indices in encoded.items():
        remapped = bytearray(
            palette_indices[icons[name][0][index]] for index in indices
        )
        runs = encode_runs(remapped)
        lines.append("static const uint8_t file_manager_{}_icon_rle[] = {{".format(name))
        for start in range(0, len(runs), 16):
            lines.append("    " + ", ".join(
                "0x{:02X}".format(value) for value in runs[start:start + 16]
            ) + ",")
        lines.append("};")
    lines.append("")
    path.write_text("\n".join(lines), encoding="ascii")


def main():
    if len(sys.argv) != 3:
        raise SystemExit("Usage: generate_file_icons.py icon-directory output.h")

    icon_directory, output_path = map(Path, sys.argv[1:])
    icons = {
        "file": read_icon(icon_directory / "file.png"),
        "folder": read_icon(icon_directory / "folder.png"),
    }
    write_header(output_path, icons)
    print("Embedded {} palette colors for {} file-manager icons.".format(
        len(set(color for colors, _ in icons.values() for color in colors)),
        len(icons)))


if __name__ == "__main__":
    main()
