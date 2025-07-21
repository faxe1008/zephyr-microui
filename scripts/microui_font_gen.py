from PIL import Image, ImageDraw, ImageFont
import sys
import os
import math

def generate_font_data(ttf_path, font_size, output_path):
    """Generate C font data from a TTF file with variable width support"""

    # Validate inputs
    if not os.path.isfile(ttf_path):
        raise FileNotFoundError(f"Font file not found: {ttf_path}")

    if font_size < 4 or font_size > 128:
        raise ValueError(f"Font size {font_size} out of range (4-128)")

    # Load the font
    try:
        font = ImageFont.truetype(ttf_path, font_size)
        print(f"Loaded font: {os.path.basename(ttf_path)} @ {font_size}px")
    except Exception as e:
        raise RuntimeError(f"Failed to load font: {e}")

    # Measure font dimensions
    font_height, max_width, avg_width = measure_font_dimensions(font)

    # Determine bitmap width (round up to next power of 2, min 8, max 32)
    bitmap_width = max(8, min(32, 2 ** math.ceil(math.log2(max_width))))
    bytes_per_row = bitmap_width // 8

    print(f"Font height: {font_height}")
    print(f"Character widths: avg={avg_width:.1f}, max={max_width}")
    print(f"Bitmap width: {bitmap_width} bits ({bytes_per_row} bytes per row)")

    # Generate glyphs for ASCII range 32-127
    glyphs = generate_variable_width_glyphs(font, bitmap_width, font_height)
    print(f"Generated {len(glyphs)} character glyphs")

    # Write output file
    write_c_header(output_path, bitmap_width, font_height, bytes_per_row, glyphs, ttf_path, font_size, avg_width)
    print(f"Font data written to: {output_path}")

def measure_font_dimensions(font):
    """Measure font dimensions for all ASCII characters"""

    # Create test image
    test_img = Image.new('1', (400, 200), color=0)
    test_draw = ImageDraw.Draw(test_img)

    max_height = 0
    max_width = 0
    total_width = 0
    char_count = 0

    for code in range(32, 128):
        char = chr(code)

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

    # Add padding for better appearance
    max_height += 1
    max_width = max(max_width, 4)  # Minimum width

    return max_height, max_width, avg_width

def generate_variable_width_glyphs(font, bitmap_width, font_height):
    """Generate variable width bitmap data for ASCII characters 32-127"""

    glyphs = []
    bytes_per_row = bitmap_width // 8

    for code in range(32, 128):
        char = chr(code)

        # Create character image with extra space for measurement
        measure_img = Image.new('1', (bitmap_width * 2, font_height * 2), color=0)
        measure_draw = ImageDraw.Draw(measure_img)

        char_width = 0
        bitmap = [0] * (font_height * bytes_per_row)

        try:
            # First, measure the character
            bbox = measure_draw.textbbox((0, 0), char, font=font)
            char_width = max(1, bbox[2] - bbox[0])  # Minimum width of 1

            # Limit character width to bitmap width
            char_width = min(char_width, bitmap_width)

            # For space character, use a reasonable width
            if code == 32:  # Space
                char_width = max(4, char_width // 2)

            # Create bitmap image
            if char_width > 0 and code != 32:  # Don't render space
                img = Image.new('1', (bitmap_width, font_height), color=0)
                draw = ImageDraw.Draw(img)

                # Draw character at origin
                draw.text((0, 0), char, font=font, fill=1)

                # Convert to bitmap bytes
                for y in range(font_height):
                    for byte_idx in range(bytes_per_row):
                        byte_val = 0
                        for bit in range(8):
                            x = byte_idx * 8 + bit
                            if x < bitmap_width:
                                try:
                                    if img.getpixel((x, y)):
                                        byte_val |= (0x80 >> bit)
                                except:
                                    pass  # Pixel out of bounds
                        bitmap[y * bytes_per_row + byte_idx] = byte_val

        except Exception as e:
            print(f"Warning: Could not render character {code} ('{char}'): {e}")
            # Use default width for problematic characters
            char_width = 4 if code == 32 else 6

        glyphs.append((code, char_width, bitmap))

    return glyphs

def write_c_header(output_path, bitmap_width, height, bytes_per_row, glyphs, source_font, size, avg_width):
    """Write C header file with variable width font data"""

    header_guard = os.path.basename(output_path).upper().replace('.', '_').replace('-', '_')

    with open(output_path, 'w') as f:
        # File header
        f.write("/*\n")
        f.write(" * Variable Width Bitmap Font Data\n")
        f.write(f" * Generated from: {os.path.basename(source_font)}\n")
        f.write(f" * Font size: {size} pixels\n")
        f.write(f" * Bitmap size: {bitmap_width}x{height} pixels\n")
        f.write(f" * Average character width: {avg_width:.1f} pixels\n")
        f.write(f" * Character range: ASCII 32-127 ({len(glyphs)} characters)\n")
        f.write(" * Format: Variable width, 1 bit per pixel\n")
        f.write(" */\n\n")

        f.write(f"#ifndef {header_guard}\n")
        f.write(f"#define {header_guard}\n\n")

        f.write("#include <stdint.h>\n")
        f.write("#include <microui/font.h>\n\n")

        # Font data
        f.write("static const uint8_t font_bitmaps[] = {\n")
        bitmap_offset = 0
        bitmap_offsets = []

        for _, _, bitmap in glyphs:
            bitmap_offsets.append(bitmap_offset)
            hex_values = [f"0x{b:02X}" for b in bitmap]
            f.write("    " + ", ".join(hex_values) + ",\n")
            bitmap_offset += len(bitmap)

        f.write("};\n\n")

        f.write("static const struct FontGlyph font_glyphs[] = {\n")
        for i, (unicode, width, bitmap) in enumerate(glyphs):
            char_name = format_char_name(unicode)
            f.write(f"    {{{width:2d}, {height:2d}, &font_bitmaps[{bitmap_offsets[i]}]}}")
            if i < len(glyphs) - 1:
                f.write(",")
            f.write(f" // {char_name} (width: {width})\n")
        f.write("};\n\n")

        # Font struct
        f.write("static const struct Font font = {\n")
        f.write(f"    .height = {height},\n")
        f.write(f"    .bitmap_width = {bitmap_width},\n")
        f.write(f"    .bytes_per_row = {bytes_per_row},\n")
        f.write("    .first_char = 32,\n")
        f.write("    .last_char = 127,\n")
        f.write(f"    .default_width = {int(avg_width + 0.5)},\n")
        f.write("    .char_spacing = 1,\n")
        f.write("    .glyphs = font_glyphs\n")
        f.write("};\n\n")

        f.write(f"#endif // {header_guard}\n")

def format_char_name(unicode):
    """Format character name for comments"""
    if unicode == 32:
        return "Space"
    elif unicode == 34:
        return "'\"'"
    elif unicode == 39:
        return "\"'\""
    elif unicode == 92:
        return "'\\\\'"
    elif 33 <= unicode <= 126:
        return f"'{chr(unicode)}'"
    else:
        return f"\\x{unicode:02X}"

def main():
    if len(sys.argv) != 4:
        print("Variable Width Bitmap Font Generator")
        print("Converts TTF/OTF fonts to C bitmap data with variable character widths")
        print()
        print("Usage:")
        print("  python font_generator.py <font_file> <size> <output.h>")
        print()
        print("Arguments:")
        print("  font_file  Path to TTF or OTF font file")
        print("  size       Font size in pixels (recommended: 8-32)")
        print("  output.h   Output C header file")
        print()
        print("Examples:")
        print("  python font_generator.py arial.ttf 12 font_arial_12.h")
        print("  python font_generator.py /System/Library/Fonts/Monaco.ttf 16 font_mono_16.h")
        print()
        print("Features:")
        print("  - Variable character widths for better typography")
        print("  - Automatic bitmap width optimization")
        print("  - Configurable character spacing")
        return

    font_file = sys.argv[1]
    try:
        font_size = int(sys.argv[2])
    except ValueError:
        print(f"Error: Invalid font size '{sys.argv[2]}' - must be a number")
        return

    output_file = sys.argv[3]

    # Generate font data
    try:
        generate_font_data(font_file, font_size, output_file)
        print("\nSuccess! Font supports variable character widths.")
        print("Text will now render with proper character spacing.")

    except Exception as e:
        print(f"Error: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
