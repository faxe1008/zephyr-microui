#!/usr/bin/env python3
"""
MicroUI Image Generator

This script converts images to C files compatible with the MicroUI library.
It supports various pixel formats used in Zephyr RTOS display subsystem.

Copyright (c) 2025 Fabian Blatz <fabianblatz@gmail.com>
SPDX-License-Identifier: Apache-2.0
"""

from PIL import Image
import argparse
import os
import struct
import sys


# Pixel format definitions matching Zephyr's display.h
PIXEL_FORMATS = {
    "RGB_888": {
        "zephyr_enum": "PIXEL_FORMAT_RGB_888",
        "bits_per_pixel": 24,
        "description": "24-bit RGB",
    },
    "MONO01": {
        "zephyr_enum": "PIXEL_FORMAT_MONO01",
        "bits_per_pixel": 1,
        "description": "Monochrome (0=Black 1=White)",
    },
    "MONO10": {
        "zephyr_enum": "PIXEL_FORMAT_MONO10",
        "bits_per_pixel": 1,
        "description": "Monochrome (1=Black 0=White)",
    },
    "ARGB_8888": {
        "zephyr_enum": "PIXEL_FORMAT_ARGB_8888",
        "bits_per_pixel": 32,
        "description": "32-bit ARGB",
    },
    "RGB_565": {
        "zephyr_enum": "PIXEL_FORMAT_RGB_565",
        "bits_per_pixel": 16,
        "description": "16-bit RGB (5-6-5)",
    },
    "BGR_565": {
        "zephyr_enum": "PIXEL_FORMAT_RGB_565X",
        "bits_per_pixel": 16,
        "description": "16-bit BGR (5-6-5) byte swapped",
    },
    "L_8": {
        "zephyr_enum": "PIXEL_FORMAT_L_8",
        "bits_per_pixel": 8,
        "description": "8-bit Grayscale/Luminance",
    },
    "AL_88": {
        "zephyr_enum": "PIXEL_FORMAT_AL_88",
        "bits_per_pixel": 16,
        "description": "8-bit Grayscale/Luminance with alpha",
    },
}


def sanitize_c_identifier(name):
    """Convert a string to a valid C identifier by replacing invalid characters with underscores."""
    if not name:
        return "image"

    # Replace invalid characters with underscores
    sanitized = ""
    for i, char in enumerate(name):
        if char.isalnum() or char == "_":
            sanitized += char
        else:
            sanitized += "_"

    # C identifiers cannot start with a digit
    if sanitized[0].isdigit():
        sanitized = "_" + sanitized

    # Ensure it's not empty after sanitization
    if not sanitized or sanitized == "_":
        return "image"

    return sanitized


def load_and_resize_image(image_path, target_width=None, target_height=None):
    """Load an image and optionally resize it to target dimensions."""
    if not os.path.isfile(image_path):
        raise FileNotFoundError(f"Image file not found: {image_path}")

    try:
        img = Image.open(image_path)
        print(f"Loaded image: {os.path.basename(image_path)}")
        print(f"Original size: {img.width}x{img.height}, mode: {img.mode}")
    except Exception as e:
        raise RuntimeError(f"Failed to load image: {e}")

    # Convert to RGBA for consistent processing
    if img.mode != "RGBA":
        img = img.convert("RGBA")

    # Resize if dimensions are specified
    if target_width or target_height:
        new_width = target_width if target_width else img.width
        new_height = target_height if target_height else img.height

        if (new_width, new_height) != (img.width, img.height):
            print(f"Resizing to: {new_width}x{new_height}")
            img = img.resize((new_width, new_height), Image.Resampling.LANCZOS)

    return img


def convert_to_rgb_888(img):
    """Convert image to RGB_888 format (24-bit RGB)."""
    data = []
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = img.getpixel((x, y))
            data.append(r)
            data.append(g)
            data.append(b)
    return bytes(data)


def convert_to_argb_8888(img):
    """Convert image to ARGB_8888 format (32-bit ARGB, little-endian).

    ARGB_8888 represents a 32-bit value as 0xAARRGGBB.
    On little-endian systems, this is stored in memory as B, G, R, A.
    """
    data = []
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = img.getpixel((x, y))
            data.append(b)
            data.append(g)
            data.append(r)
            data.append(a)
    return bytes(data)


def convert_to_rgb_565(img):
    """Convert image to RGB_565 format (16-bit RGB, big-endian)."""
    data = []
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = img.getpixel((x, y))

            # Convert 8-bit RGB to 5-6-5 format
            r5 = (r >> 3) & 0x1F
            g6 = (g >> 2) & 0x3F
            b5 = (b >> 3) & 0x1F

            # Pack into 16-bit value: RRRRRGGGGGGBBBBB
            rgb565 = (r5 << 11) | (g6 << 5) | b5

            # Store as big-endian (MSB first)
            data.append((rgb565 >> 8) & 0xFF)
            data.append(rgb565 & 0xFF)

    return bytes(data)


def convert_to_bgr_565(img):
    """Convert image to BGR_565 format (16-bit BGR, little-endian)."""
    data = []
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = img.getpixel((x, y))

            # Convert 8-bit RGB to 5-6-5 format
            r5 = (r >> 3) & 0x1F
            g6 = (g >> 2) & 0x3F
            b5 = (b >> 3) & 0x1F

            # Pack into 16-bit value: BBBBBGGGGGGRRRRR
            bgr565 = (b5 << 11) | (g6 << 5) | r5

            # Store as little-endian (LSB first)
            data.append(bgr565 & 0xFF)
            data.append((bgr565 >> 8) & 0xFF)

    return bytes(data)


def convert_to_mono01(img):
    """Convert image to MONO01 format (1-bit, 0=Black 1=White, packed into bytes)."""
    data = []

    for y in range(img.height):
        byte_val = 0
        bit_pos = 0

        for x in range(img.width):
            r, g, b, a = img.getpixel((x, y))

            # Convert to grayscale using standard luminance formula
            luminance = int(0.299 * r + 0.587 * g + 0.114 * b)

            # Threshold at 128 (0=Black, 1=White)
            if luminance >= 128:
                byte_val |= 1 << (7 - bit_pos)

            bit_pos += 1

            # Store byte when we have 8 bits or at end of row
            if bit_pos == 8:
                data.append(byte_val)
                byte_val = 0
                bit_pos = 0

        # Store remaining bits if row width is not multiple of 8
        if bit_pos > 0:
            data.append(byte_val)

    return bytes(data)


def convert_to_mono10(img):
    """Convert image to MONO10 format (1-bit, 1=Black 0=White, packed into bytes)."""
    data = []

    for y in range(img.height):
        byte_val = 0
        bit_pos = 0

        for x in range(img.width):
            r, g, b, a = img.getpixel((x, y))

            # Convert to grayscale using standard luminance formula
            luminance = int(0.299 * r + 0.587 * g + 0.114 * b)

            # Threshold at 128 (1=Black, 0=White)
            if luminance < 128:
                byte_val |= 1 << (7 - bit_pos)

            bit_pos += 1

            # Store byte when we have 8 bits or at end of row
            if bit_pos == 8:
                data.append(byte_val)
                byte_val = 0
                bit_pos = 0

        # Store remaining bits if row width is not multiple of 8
        if bit_pos > 0:
            data.append(byte_val)

    return bytes(data)


def convert_to_l_8(img):
    """Convert image to L_8 format (8-bit grayscale/luminance)."""
    data = []
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = img.getpixel((x, y))

            # Convert to grayscale using standard luminance formula
            luminance = int(0.299 * r + 0.587 * g + 0.114 * b)
            data.append(luminance)

    return bytes(data)


def convert_to_al_88(img):
    """Convert image to AL_88 format (8-bit grayscale with alpha)."""
    data = []
    for y in range(img.height):
        for x in range(img.width):
            r, g, b, a = img.getpixel((x, y))

            # Convert to grayscale using standard luminance formula
            luminance = int(0.299 * r + 0.587 * g + 0.114 * b)

            data.append(a)
            data.append(luminance)

    return bytes(data)


def convert_image_to_format(img, pixel_format):
    """Convert image to the specified pixel format."""
    converters = {
        "RGB_888": convert_to_rgb_888,
        "ARGB_8888": convert_to_argb_8888,
        "RGB_565": convert_to_rgb_565,
        "BGR_565": convert_to_bgr_565,
        "MONO01": convert_to_mono01,
        "MONO10": convert_to_mono10,
        "L_8": convert_to_l_8,
        "AL_88": convert_to_al_88,
    }

    if pixel_format not in converters:
        raise ValueError(f"Unsupported pixel format: {pixel_format}")

    return converters[pixel_format](img)


def calculate_stride(width, bits_per_pixel):
    """Calculate stride (bytes per row) for the given width and pixel format."""
    # For bit formats, round up to nearest byte
    if bits_per_pixel == 1:
        return (width + 7) // 8
    else:
        return width * (bits_per_pixel // 8)


def write_c_file(output_path, img, pixel_format, image_data, image_name=None):
    """Write the image data to a C file."""

    if image_name is None:
        base_name = os.path.splitext(os.path.basename(output_path))[0]
        image_name = sanitize_c_identifier(base_name)
    else:
        image_name = sanitize_c_identifier(image_name)

    format_info = PIXEL_FORMATS[pixel_format]
    stride = calculate_stride(img.width, format_info["bits_per_pixel"])

    # Generate C file
    with open(output_path, "w") as f:
        # Write header
        f.write("/*\n")
        f.write(" * Auto-generated image data\n")
        f.write(f" * Image: {image_name}\n")
        f.write(f" * Size: {img.width}x{img.height}\n")
        f.write(f" * Format: {pixel_format} ({format_info['description']})\n")
        f.write(f" * Data size: {len(image_data)} bytes\n")
        f.write(" *\n")
        f.write(" * Generated by microui_gen_image.py\n")
        f.write(" */\n\n")

        f.write("#include <microui/image.h>\n")
        f.write("#include <zephyr/drivers/display.h>\n\n")

        # Write image data array
        f.write(f"static const uint8_t {image_name}_data[] = {{\n")

        # Write data in rows of 12 bytes for readability
        bytes_per_line = 12
        for i in range(0, len(image_data), bytes_per_line):
            line_data = image_data[i : i + bytes_per_line]
            hex_values = ", ".join(f"0x{b:02x}" for b in line_data)
            f.write(f"\t{hex_values},\n")

        f.write("};\n\n")

        # Write image descriptor
        f.write(f"const struct mu_ImageDescriptor {image_name} = {{\n")
        f.write(f"\t.width = {img.width},\n")
        f.write(f"\t.height = {img.height},\n")
        f.write(f"\t.stride = {stride},\n")
        f.write(f"\t.data_size = sizeof({image_name}_data),\n")
        f.write(f"\t.data = {image_name}_data,\n")
        f.write(f"\t.pixel_format = {format_info['zephyr_enum']},\n")
        f.write(f"\t.compression = MU_IMAGE_COMPRESSION_NONE,\n")
        f.write("};\n")


def main():
    parser = argparse.ArgumentParser(
        description="Convert images to C files for MicroUI library",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Supported pixel formats:
  RGB_888     24-bit RGB
  ARGB_8888   32-bit ARGB
  RGB_565     16-bit RGB (5-6-5)
  BGR_565     16-bit BGR (5-6-5)
  MONO01      1-bit monochrome (0=Black 1=White)
  MONO10      1-bit monochrome (1=Black 0=White)
  L_8         8-bit grayscale/luminance
  AL_88       8-bit grayscale with alpha

Examples:
  # Convert image to RGB_565 format
  %(prog)s -i logo.png -o logo.c -f RGB_565
  
  # Convert and resize to 128x64 pixels
  %(prog)s -i icon.png -o icon.c -f MONO01 -w 128 -h 64
  
  # Specify custom image name
  %(prog)s -i splash.png -o splash.c -f ARGB_8888 -n my_splash_image
        """,
    )

    parser.add_argument("-i", "--input", required=True, help="Input image file path")
    parser.add_argument("-o", "--output", required=True, help="Output C file path")
    parser.add_argument(
        "-f",
        "--format",
        required=True,
        choices=list(PIXEL_FORMATS.keys()),
        help="Target pixel format",
    )
    parser.add_argument(
        "-w", "--width", type=int, help="Target width (optional, resizes image)"
    )
    parser.add_argument(
        "-H", "--height", type=int, help="Target height (optional, resizes image)"
    )
    parser.add_argument(
        "-n",
        "--name",
        help="Image name for C identifier (default: derived from output filename)",
    )

    args = parser.parse_args()

    try:
        # Load and optionally resize image
        img = load_and_resize_image(args.input, args.width, args.height)

        # Convert to target pixel format
        print(f"Converting to {args.format} format...")
        image_data = convert_image_to_format(img, args.format)

        # Write C file
        write_c_file(args.output, img, args.format, image_data, args.name)

        print(f"Successfully generated: {args.output}")
        print(f"Image size: {img.width}x{img.height}")
        print(f"Data size: {len(image_data)} bytes")

    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    sys.exit(main())
