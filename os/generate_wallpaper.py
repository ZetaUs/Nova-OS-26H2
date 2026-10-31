import struct
import sys
import zlib
from pathlib import Path


OUTPUT_WIDTH = 1920
OUTPUT_HEIGHT = 1080


def decode_png(path):
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Wallpaper source is not a PNG image.")

    offset = 8
    compressed = bytearray()
    width = height = bit_depth = color_type = None
    while offset < len(data):
        length = struct.unpack_from(">I", data, offset)[0]
        chunk_type = data[offset + 4:offset + 8]
        chunk = data[offset + 8:offset + 8 + length]
        offset += length + 12

        if chunk_type == b"IHDR":
            width, height, bit_depth, color_type, compression, filtering, interlace = (
                struct.unpack(">IIBBBBB", chunk)
            )
            if compression != 0 or filtering != 0 or interlace != 0:
                raise ValueError("Unsupported PNG encoding in wallpaper.")
        elif chunk_type == b"IDAT":
            compressed.extend(chunk)
        elif chunk_type == b"IEND":
            break

    if bit_depth != 8 or color_type not in (2, 6):
        raise ValueError("Wallpaper PNG must be 8-bit RGB or RGBA.")

    bytes_per_pixel = 4 if color_type == 6 else 3
    stride = width * bytes_per_pixel
    decoded = zlib.decompress(compressed)
    pixels = bytearray(height * stride)
    source_offset = 0
    previous_row = bytearray(stride)

    for row_index in range(height):
        filter_type = decoded[source_offset]
        source_offset += 1
        row = bytearray(decoded[source_offset:source_offset + stride])
        source_offset += stride

        for index in range(stride):
            left = row[index - bytes_per_pixel] if index >= bytes_per_pixel else 0
            above = previous_row[index]
            upper_left = previous_row[index - bytes_per_pixel] if index >= bytes_per_pixel else 0

            if filter_type == 1:
                predictor = left
            elif filter_type == 2:
                predictor = above
            elif filter_type == 3:
                predictor = (left + above) // 2
            elif filter_type == 4:
                estimate = left + above - upper_left
                distances = (
                    abs(estimate - left),
                    abs(estimate - above),
                    abs(estimate - upper_left),
                )
                predictor = (left, above, upper_left)[distances.index(min(distances))]
            elif filter_type == 0:
                predictor = 0
            else:
                raise ValueError(f"Unsupported PNG filter {filter_type}.")

            row[index] = (row[index] + predictor) & 0xFF

        start = row_index * stride
        pixels[start:start + stride] = row
        previous_row = row

    if source_offset != len(decoded):
        raise ValueError("Wallpaper PNG image data has an unexpected size.")
    return width, height, pixels, bytes_per_pixel


def bilinear_channel(pixels, stride, bytes_per_pixel, channel, x0, x1, y0, y1, fx, fy):
    def sample(x, y):
        if channel == 3 and bytes_per_pixel == 3:
            return 255
        return pixels[y * stride + x * bytes_per_pixel + channel]

    top = sample(x0, y0) * (65536 - fx) + sample(x1, y0) * fx
    bottom = sample(x0, y1) * (65536 - fx) + sample(x1, y1) * fx
    top = (top + 32768) >> 16
    bottom = (bottom + 32768) >> 16
    return (top * (65536 - fy) + bottom * fy + 32768) >> 16


def generate_rgb565(width, height, pixels, bytes_per_pixel):
    source_stride = width * bytes_per_pixel
    output = bytearray(OUTPUT_WIDTH * OUTPUT_HEIGHT * 2)

    for output_y in range(OUTPUT_HEIGHT):
        source_y_fixed = ((2 * output_y + 1) * height * 32768 // OUTPUT_HEIGHT) - 32768
        source_y = source_y_fixed // 65536
        fy = source_y_fixed - source_y * 65536
        y0 = max(0, min(height - 1, source_y))
        y1 = max(0, min(height - 1, source_y + 1))

        for output_x in range(OUTPUT_WIDTH):
            source_x_fixed = ((2 * output_x + 1) * width * 32768 // OUTPUT_WIDTH) - 32768
            source_x = source_x_fixed // 65536
            fx = source_x_fixed - source_x * 65536
            x0 = max(0, min(width - 1, source_x))
            x1 = max(0, min(width - 1, source_x + 1))

            alpha = bilinear_channel(pixels, source_stride, bytes_per_pixel, 3, x0, x1, y0, y1, fx, fy)
            red = bilinear_channel(pixels, source_stride, bytes_per_pixel, 0, x0, x1, y0, y1, fx, fy)
            green = bilinear_channel(pixels, source_stride, bytes_per_pixel, 1, x0, x1, y0, y1, fx, fy)
            blue = bilinear_channel(pixels, source_stride, bytes_per_pixel, 2, x0, x1, y0, y1, fx, fy)
            red = (red * alpha + 10 * (255 - alpha) + 127) // 255
            green = (green * alpha + 18 * (255 - alpha) + 127) // 255
            blue = (blue * alpha + 32 * (255 - alpha) + 127) // 255

            color = ((red >> 3) << 11) | ((green >> 2) << 5) | (blue >> 3)
            offset = (output_y * OUTPUT_WIDTH + output_x) * 2
            output[offset] = color & 0xFF
            output[offset + 1] = color >> 8

    return output


def main():
    if len(sys.argv) != 3:
        raise SystemExit("Usage: generate_wallpaper.py wallpaper.png output.rgb565")

    source_path, output_path = map(Path, sys.argv[1:])
    width, height, pixels, bytes_per_pixel = decode_png(source_path)
    wallpaper = generate_rgb565(width, height, pixels, bytes_per_pixel)
    output_path.write_bytes(wallpaper)
    print(
        f"Generated {OUTPUT_WIDTH}x{OUTPUT_HEIGHT} RGB565 wallpaper "
        f"({len(wallpaper)} bytes) from {width}x{height} PNG."
    )


if __name__ == "__main__":
    main()
