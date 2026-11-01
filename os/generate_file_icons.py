import sys
from pathlib import Path

from generate_logo import decode_png

ICON_WIDTH = 96
ICON_HEIGHT = 100
GRAY_LEVELS = 4
ALPHA_LEVELS = 4


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


def resize_and_quantize_icon(path):
    width, height, pixels = decode_png(path)
    indices = bytearray()
    for output_y in range(ICON_HEIGHT):
        source_y0 = output_y * height / ICON_HEIGHT
        source_y1 = (output_y + 1) * height / ICON_HEIGHT
        for output_x in range(ICON_WIDTH):
            source_x0 = output_x * width / ICON_WIDTH
            source_x1 = (output_x + 1) * width / ICON_WIDTH
            alpha_sum = 0.0
            gray_sum = 0.0
            weight_sum = 0.0
            for source_y in range(int(source_y0), int(source_y1 + 0.999999)):
                y_weight = max(
                    0.0,
                    min(source_y1, source_y + 1) - max(source_y0, source_y),
                )
                for source_x in range(int(source_x0), int(source_x1 + 0.999999)):
                    x_weight = max(
                        0.0,
                        min(source_x1, source_x + 1) - max(source_x0, source_x),
                    )
                    weight = x_weight * y_weight
                    offset = (source_y * width + source_x) * 4
                    red, green, blue, alpha = pixels[offset:offset + 4]
                    gray = (red * 299 + green * 587 + blue * 114) / 1000
                    alpha_sum += alpha * weight
                    gray_sum += gray * alpha * weight
                    weight_sum += weight

            if weight_sum == 0:
                indices.append(0)
                continue
            alpha = alpha_sum / weight_sum
            gray = gray_sum / alpha_sum if alpha_sum else 0
            alpha_level = min(
                ALPHA_LEVELS - 1,
                int(alpha * (ALPHA_LEVELS - 1) / 255 + 0.5),
            )
            gray_level = min(
                GRAY_LEVELS - 1,
                int(gray * (GRAY_LEVELS - 1) / 255 + 0.5),
            )
            indices.append(alpha_level * GRAY_LEVELS + gray_level)
    return indices


def write_header(path, icons):
    lines = [
        "#define FILE_MANAGER_ICON_WIDTH {}".format(ICON_WIDTH),
        "#define FILE_MANAGER_ICON_HEIGHT {}".format(ICON_HEIGHT),
        "static const uint32_t file_manager_icon_palette[] = {",
    ]
    for alpha_level in range(ALPHA_LEVELS):
        alpha = (alpha_level * 255 + (ALPHA_LEVELS - 1) // 2) // (ALPHA_LEVELS - 1)
        for gray_start in range(0, GRAY_LEVELS, 8):
            values = []
            for gray_level in range(gray_start, min(gray_start + 8, GRAY_LEVELS)):
                gray = (gray_level * 255 + (GRAY_LEVELS - 1) // 2) // (GRAY_LEVELS - 1)
                values.append("0x{:02X}{:02X}{:02X}{:02X}".format(
                    alpha, gray, gray, gray))
            lines.append("    " + ", ".join(values) + ",")
    lines.append("};")

    for name, indices in icons.items():
        runs = encode_runs(indices)
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
        "file": resize_and_quantize_icon(icon_directory / "file.png"),
        "folder": resize_and_quantize_icon(icon_directory / "folder.png"),
    }
    write_header(output_path, icons)
    print("Resized and embedded {} file-manager PNG icons at {}x{}.".format(
        len(icons), ICON_WIDTH, ICON_HEIGHT))


if __name__ == "__main__":
    main()
