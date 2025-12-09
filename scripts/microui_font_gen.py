from PIL import Image, ImageDraw, ImageFont
import argparse
import os
import math


def sanitize_c_identifier(name):
    """Convert a string to a valid C identifier by replacing invalid characters with underscores."""
    if not name:
        return "font"

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
        return "font"

    return sanitized


def parse_character_ranges(range_string):
    if not range_string:
        return list(range(32, 128))

    characters = set()
    parts = range_string.split(",")

    for part in parts:
        part = part.strip()
        if "-" in part:
            try:
                start, end = part.split("-", 1)
                start = int(start.strip())
                end = int(end.strip())
                if start > end:
                    raise ValueError(f"Invalid range: {part} (start > end)")
                if start < 0 or end < 0:
                    raise ValueError(f"Character codes must be non-negative: {part}")
                characters.update(range(start, end + 1))
            except ValueError as e:
                if "invalid literal" in str(e):
                    raise ValueError(f"Invalid range format: {part}")
                raise
        else:
            try:
                code = int(part.strip())
                if code < 0:
                    raise ValueError(f"Character code must be non-negative: {code}")
                characters.add(code)
            except ValueError as e:
                if "invalid literal" in str(e):
                    raise ValueError(f"Invalid character code: {part}")
                raise

    return sorted(list(characters))


def generate_font_data(
    ttf_path, font_size, output_path, character_codes=None, font_name=None
):
    if not os.path.isfile(ttf_path):
        raise FileNotFoundError(f"Font file not found: {ttf_path}")

    if font_size < 4 or font_size > 128:
        raise ValueError(f"Font size {font_size} out of range (4-128)")

    if character_codes is None:
        character_codes = list(range(32, 128))

    try:
        font = ImageFont.truetype(ttf_path, font_size)
        print(f"Loaded font: {os.path.basename(ttf_path)} @ {font_size}px")
    except Exception as e:
        raise RuntimeError(f"Failed to load font: {e}")

    font_height, max_width, avg_width = measure_font_dimensions(font, character_codes)

    bitmap_width = max(8, min(32, 2 ** math.ceil(math.log2(max_width))))
    bytes_per_row = bitmap_width // 8

    print(f"Font height: {font_height}")
    print(f"Character widths: avg={avg_width:.1f}, max={max_width}")
    print(f"Bitmap width: {bitmap_width} bits ({bytes_per_row} bytes per row)")
    if character_codes:
        print(
            f"Character range: {min(character_codes)}-{max(character_codes)} ({len(character_codes)} requested)"
        )

    glyphs = generate_variable_width_glyphs(
        font, bitmap_width, font_height, character_codes
    )
    print(f"Generated {len(glyphs)} character glyphs")

    write_c_file(
        output_path,
        bitmap_width,
        font_height,
        bytes_per_row,
        glyphs,
        ttf_path,
        font_size,
        avg_width,
        character_codes,
        font_name,
    )
    print(f"Font data written to: {output_path}")


def measure_font_dimensions(font, character_codes):
    test_img = Image.new("1", (400, 200), color=0)
    test_draw = ImageDraw.Draw(test_img)

    max_height = 0
    max_width = 0
    total_width = 0
    char_count = 0

    for code in character_codes:
        try:
            char = chr(code)
        except ValueError:
            print(f"Warning: Invalid character code {code}")
            continue

        try:
            bbox = test_draw.textbbox((0, 0), char, font=font)
            width = bbox[2] - bbox[0]
            height = bbox[3] - bbox[1]

            max_height = max(max_height, height)
            max_width = max(max_width, width)
            total_width += width
            char_count += 1

        except Exception as e:
            print(f"Warning: Could not measure character {code} ('{char}'): {e}")
            continue

    avg_width = total_width / char_count if char_count > 0 else 8

    max_height += 1
    max_width = max(max_width, 4)

    return max_height, max_width, avg_width


def generate_variable_width_glyphs(font, bitmap_width, font_height, character_codes):
    if not character_codes:
        return []

    glyphs = []
    bytes_per_row = bitmap_width // 8

    for code in character_codes:
        glyph = generate_single_glyph(
            font, code, bitmap_width, font_height, bytes_per_row
        )
        glyphs.append(glyph)

    return glyphs


def generate_single_glyph(font, code, bitmap_width, font_height, bytes_per_row):
    try:
        char = chr(code)
    except ValueError:
        bitmap = [0] * (font_height * bytes_per_row)
        return (code, 4, bitmap)

    measure_img = Image.new("1", (bitmap_width * 2, font_height * 2), color=0)
    measure_draw = ImageDraw.Draw(measure_img)

    char_width = 0
    bitmap = [0] * (font_height * bytes_per_row)

    try:
        bbox = measure_draw.textbbox((0, 0), char, font=font)
        char_width = max(1, bbox[2] - bbox[0])
        char_width = min(char_width, bitmap_width)

        if code == 32:
            char_width = max(4, char_width // 2)

        if char_width > 0 and code != 32:
            img = Image.new("1", (bitmap_width, font_height), color=0)
            draw = ImageDraw.Draw(img)

            draw.text((0, 0), char, font=font, fill=1)

            for y in range(font_height):
                for byte_idx in range(bytes_per_row):
                    byte_val = 0
                    for bit in range(8):
                        x = byte_idx * 8 + bit
                        if x < bitmap_width:
                            try:
                                if img.getpixel((x, y)):
                                    byte_val |= 0x80 >> bit
                            except:
                                pass
                    bitmap[y * bytes_per_row + byte_idx] = byte_val

    except Exception as e:
        print(f"Warning: Could not render character {code} ('{char}'): {e}")
        char_width = 4 if code == 32 else 6

    return (code, char_width, bitmap)


def write_c_file(
    output_path,
    bitmap_width,
    height,
    bytes_per_row,
    glyphs,
    source_font,
    size,
    avg_width,
    character_codes,
    font_name=None,
):
    # Use provided font name or derive from output filename
    if font_name:
        font_name = sanitize_c_identifier(font_name)
    else:
        # Extract from output filename (without extension) and sanitize
        base_name = os.path.splitext(os.path.basename(output_path))[0]
        font_name = sanitize_c_identifier(base_name)

    first_char = min(character_codes) if character_codes else 32
    last_char = max(character_codes) if character_codes else 127

    with open(output_path, "w") as f:
        f.write("/*\n")
        f.write(" * Variable Width Bitmap Font Data\n")
        f.write(f" * Generated from: {os.path.basename(source_font)}\n")
        f.write(f" * Font size: {size} pixels\n")
        f.write(f" * Bitmap size: {bitmap_width}x{height} pixels\n")
        f.write(f" * Average character width: {avg_width:.1f} pixels\n")
        f.write(
            f" * Character range: {first_char}-{last_char} ({len(character_codes)} requested, {len(glyphs)} total)\n"
        )
        f.write(" * Format: Variable width, 1 bit per pixel\n")
        f.write(" */\n\n")

        f.write("#include <stdint.h>\n")
        f.write("#include <microui/font.h>\n\n")

        f.write(f"const uint8_t {font_name}_bitmaps[] = {{\n")
        bitmap_offset = 0
        bitmap_offsets = []

        for _, _, bitmap in glyphs:
            bitmap_offsets.append(bitmap_offset)
            hex_values = [f"0x{b:02X}" for b in bitmap]
            f.write("    " + ", ".join(hex_values) + ",\n")
            bitmap_offset += len(bitmap)

        f.write("};\n\n")

        f.write(f"const struct FontGlyph {font_name}_glyphs[] = {{\n")
        for i, (unicode, width, bitmap) in enumerate(glyphs):
            f.write(
                f"    {{{unicode}u, {width:2d}, {height:2d}, &{font_name}_bitmaps[{bitmap_offsets[i]}]}}"
            )
            if i < len(glyphs) - 1:
                f.write(",")
            f.write(f" // {format_char_name(unicode)} (width: {width})\n")
        f.write("};\n\n")

        f.write(f"const struct Font {font_name} = {{\n")
        f.write(f"    .height = {height},\n")
        f.write(f"    .bitmap_width = {bitmap_width},\n")
        f.write(f"    .bytes_per_row = {bytes_per_row},\n")
        f.write(f"    .default_width = {int(avg_width + 0.5)},\n")
        f.write("    .char_spacing = 1,\n")
        f.write(f"    .glyph_count = {len(glyphs)},\n")
        f.write(f"    .glyphs = {font_name}_glyphs\n")
        f.write("};\n")


def format_char_name(unicode):
    try:
        char = chr(unicode)
        if unicode == 32:
            return "Space"
        elif unicode == 34:
            return "'\"'"
        elif unicode == 39:
            return '"\'"'
        elif unicode == 92:
            return "'\\\\'"
        elif 33 <= unicode <= 126:
            return f"'{char}'"
        else:
            return f"\\x{unicode:02X}"
    except ValueError:
        return f"\\x{unicode:02X}"


def main():
    parser = argparse.ArgumentParser(
        description="Variable Width Bitmap Font Generator - Converts TTF/OTF fonts to C bitmap data with variable character widths",
        epilog="""
Examples:
  %(prog)s arial.ttf 12 font_arial_12.c
  %(prog)s -r "32-127,224,227-229" arial.ttf 16 font_arial_16.c
  %(prog)s --range "65-90,97-122" /System/Library/Fonts/Monaco.ttf 16 font_mono_16.c

Features:
  - Variable character widths for better typography
  - Automatic bitmap width optimization
  - Configurable character spacing
  - Support for custom character ranges
        """,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )

    parser.add_argument("font_file", help="Path to TTF or OTF font file")
    parser.add_argument(
        "size", type=int, help="Font size in pixels (4-128, recommended: 8-32)"
    )
    parser.add_argument("output", help="Output C file")
    parser.add_argument(
        "-n",
        "--name",
        dest="font_name",
        default=None,
        help="C identifier name for the font. If not provided, derived from output filename.",
    )
    parser.add_argument(
        "-r",
        "--range",
        dest="char_range",
        help='Character ranges to generate (e.g., "32-127,224,227-229"). Default: 32-127',
    )

    args = parser.parse_args()

    if args.size < 4 or args.size > 128:
        parser.error(f"Font size {args.size} out of range (4-128)")

    try:
        character_codes = parse_character_ranges(args.char_range)
        if not character_codes:
            parser.error("No valid character codes specified")
    except ValueError as e:
        parser.error(f"Invalid character range: {e}")

    try:
        generate_font_data(
            args.font_file, args.size, args.output, character_codes, args.font_name
        )
        print("\nSuccess! Font supports variable character widths.")
        print("Text will now render with proper character spacing.")

    except Exception as e:
        print(f"Error: {e}")
        return 1

    return 0


if __name__ == "__main__":
    exit(main())
