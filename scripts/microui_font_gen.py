from PIL import Image, ImageDraw, ImageFont
import argparse
import os
import math

try:
    from fontTools.ttLib import TTFont
except Exception:
    TTFont = None


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

    # Support bitmap widths up to 64 bits (8 bytes per row) for large fonts
    bitmap_width = max(8, 2 ** math.ceil(math.log2(max(max_width, 1))))
    if bitmap_width > 64:
        raise ValueError(f"Character width {max_width} exceeds maximum supported bitmap width of 64 bits")
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
    kerning_pairs = generate_kerning_pairs(ttf_path, font_size, character_codes)
    print(f"Generated {len(glyphs)} character glyphs")
    print(f"Generated {len(kerning_pairs)} kerning pairs")

    write_c_file(
        output_path,
        bitmap_width,
        font_height,
        bytes_per_row,
        glyphs,
        kerning_pairs,
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

    max_bottom = 0
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
            # Use bbox[3] (bottom y-coordinate) to ensure all descenders fit
            # when glyphs are drawn at (0, 0)
            max_bottom = max(max_bottom, bbox[3])
            max_width = max(max_width, width)
            total_width += width
            char_count += 1

        except Exception as e:
            print(f"Warning: Could not measure character {code} ('{char}'): {e}")
            continue

    avg_width = total_width / char_count if char_count > 0 else 8

    # Font height is the maximum y-coordinate any glyph extends to when drawn at (0,0)
    # This ensures ascenders and descenders are fully captured
    font_height = max_bottom + 1
    max_width = max(max_width, 4)

    return font_height, max_width, avg_width


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
        advance = font.getlength(char)
        char_width = int(round(advance))
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


def get_x_adjustment(value_record):
    if value_record is None:
        return 0

    adjustment = 0
    adjustment += int(getattr(value_record, "XAdvance", 0) or 0)
    adjustment += int(getattr(value_record, "XPlacement", 0) or 0)
    return adjustment


def get_xadvance(value_record):
    """
    Return only the XAdvance component from a valueRecord-like object.
    We treat missing attributes as 0. Value is returned as an int (font units).
    """
    if value_record is None:
        return 0
    # Some fonts use XAdvance, some may have xAdvance; use getattr safely.
    return int(getattr(value_record, "XAdvance", getattr(value_record, "xAdvance", 0)) or 0)


def generate_kerning_pairs(ttf_path, font_size, character_codes):
    """
    Generate kerning pairs.
    """
    if not character_codes:
        return []

    if TTFont is None:
        print("Warning: fontTools not available, kerning table generation skipped")
        return []

    kerning_map = {}

    with TTFont(ttf_path) as tt:
        # units per em => scale factor to convert font units -> pixels
        units_per_em = tt["head"].unitsPerEm
        scale = float(font_size) / float(units_per_em) if units_per_em else 1.0

        # Build cmap-related maps for only the requested codepoints
        cmap = tt.getBestCmap() or {}
        cp_to_glyph = {}
        glyph_to_cp = {}
        for codepoint in character_codes:
            glyph_name = cmap.get(codepoint)
            if glyph_name is None:
                continue
            cp_to_glyph[codepoint] = glyph_name
            # Map glyph -> first codepoint that maps to it (enough for kerning lookup)
            if glyph_name not in glyph_to_cp:
                glyph_to_cp[glyph_name] = codepoint

        if len(cp_to_glyph) < 2:
            return []

        # Choose whether to use GPOS or legacy kern table
        has_gpos = "GPOS" in tt

        # If no GPOS, process legacy 'kern' table (if present)
        if not has_gpos and "kern" in tt:
            for kern_table in tt["kern"].kernTables:
                if not hasattr(kern_table, "kernTable"):
                    continue
                for pair, value in kern_table.kernTable.items():
                    # pair may be glyph names or glyph indices
                    if isinstance(pair[0], str):
                        left_name, right_name = pair
                    else:
                        glyph_order = tt.getGlyphOrder()
                        left_name = glyph_order[pair[0]]
                        right_name = glyph_order[pair[1]]

                    left_cp = glyph_to_cp.get(left_name)
                    right_cp = glyph_to_cp.get(right_name)
                    if left_cp is None or right_cp is None:
                        continue

                    # kern table values are typically integers in font units; use directly
                    kerning_map[(left_cp, right_cp)] = kerning_map.get((left_cp, right_cp), 0) + int(value)

        # If GPOS is present, parse pair/class lookups
        if has_gpos:
            gpos = tt["GPOS"].table
            lookup_list = getattr(gpos, "LookupList", None)
            if lookup_list is not None:
                for lookup in lookup_list.Lookup:
                    # Pair Adjustment Positioning
                    if lookup.LookupType != 2:
                        continue

                    for subtable in lookup.SubTable:
                        format_type = getattr(subtable, "Format", 0)

                        # Format 1: PairSet
                        if format_type == 1:
                            # Coverage.glyphs aligns with PairSet elements
                            for left_name, pair_set in zip(subtable.Coverage.glyphs, subtable.PairSet):
                                left_cp = glyph_to_cp.get(left_name)
                                if left_cp is None:
                                    continue

                                for pair_value_record in pair_set.PairValueRecord:
                                    # Use only Value1.XAdvance as the kerning delta
                                    # (do not sum Value1 and Value2)
                                    adv = get_xadvance(pair_value_record.Value1)
                                    if adv == 0:
                                        continue
                                    right_name = pair_value_record.SecondGlyph
                                    right_cp = glyph_to_cp.get(right_name)
                                    if right_cp is None:
                                        continue

                                    key = (left_cp, right_cp)
                                    kerning_map[key] = kerning_map.get(key, 0) + adv

                        # Format 2: Class-to-class pairs
                        elif format_type == 2:
                            # class defs map glyph name -> class index
                            class_def1 = getattr(subtable, "ClassDef1", None)
                            class_def2 = getattr(subtable, "ClassDef2", None)
                            if class_def1 is None or class_def2 is None:
                                continue
                            class_def1_map = getattr(class_def1, "classDefs", {}) if class_def1 else {}
                            class_def2_map = getattr(class_def2, "classDefs", {}) if class_def2 else {}

                            class1_records = subtable.Class1Record

                            # Build mapping class2 -> list of codepoints within requested character set
                            class2_to_cps = {}
                            for cp, glyph_name in cp_to_glyph.items():
                                class2 = class_def2_map.get(glyph_name, 0)
                                class2_to_cps.setdefault(class2, []).append(cp)

                            for left_name in subtable.Coverage.glyphs:
                                left_cp = glyph_to_cp.get(left_name)
                                if left_cp is None:
                                    continue

                                class1 = class_def1_map.get(left_name, 0)
                                if class1 >= len(class1_records):
                                    continue

                                class1_record = class1_records[class1]
                                for class2, right_cps in class2_to_cps.items():
                                    if class2 >= len(class1_record.Class2Record):
                                        continue

                                    class2_record = class1_record.Class2Record[class2]
                                    # Use only Value1.XAdvance from class2_record (font units)
                                    adv = get_xadvance(class2_record.Value1)
                                    if adv == 0:
                                        continue

                                    for right_cp in right_cps:
                                        key = (left_cp, right_cp)
                                        kerning_map[key] = kerning_map.get(key, 0) + adv

    # Convert font units -> pixels and clamp to int8 range
    kerning_pairs = []
    for (left_cp, right_cp), value in kerning_map.items():
        # value currently in font units (int). Scale to px and round.
        px_adjustment = int(round(float(value) * scale))
        if px_adjustment == 0:
            continue
        # clamp to signed 8-bit range
        px_adjustment = max(-128, min(127, px_adjustment))
        kerning_pairs.append((left_cp, right_cp, px_adjustment))

    # Sort by left, then right to match binary-search expectation
    kerning_pairs.sort(key=lambda pair: (pair[0], pair[1]))
    return kerning_pairs

def write_c_file(
    output_path,
    bitmap_width,
    height,
    bytes_per_row,
    glyphs,
    kerning_pairs,
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

        f.write(f"const struct mu_FontGlyph {font_name}_glyphs[] = {{\n")
        for i, (unicode, width, bitmap) in enumerate(glyphs):
            f.write(
                f"    {{{unicode}u, {width:2d}, {height:2d}, &{font_name}_bitmaps[{bitmap_offsets[i]}]}}"
            )
            if i < len(glyphs) - 1:
                f.write(",")
            f.write(f" // {format_char_name(unicode)} (width: {width})\n")
        f.write("};\n\n")

        f.write("#ifdef CONFIG_MICROUI_FONT_KERNING\n")
        f.write(f"const struct mu_FontKerningPair {font_name}_kerning_pairs[] = {{\n")
        if kerning_pairs:
            for left_code, right_code, adjustment in kerning_pairs:
                f.write(
                    f"    {{{left_code}u, {right_code}u, {adjustment}}},"
                    f" // {format_char_name(left_code)} {format_char_name(right_code)}\n"
                )
        else:
            f.write("    {0u, 0u, 0}\n")
        f.write("};\n")
        f.write("#endif\n\n")

        f.write(f"const struct mu_FontDescriptor {font_name} = {{\n")
        f.write(f"    .height = {height},\n")
        f.write(f"    .bitmap_width = {bitmap_width},\n")
        f.write(f"    .bytes_per_row = {bytes_per_row},\n")
        f.write(f"    .default_width = {int(avg_width + 0.5)},\n")
        f.write("    .char_spacing = 1,\n")
        f.write(f"    .glyph_count = {len(glyphs)},\n")
        f.write(f"    .glyphs = {font_name}_glyphs,\n")
        f.write("#ifdef CONFIG_MICROUI_FONT_KERNING\n")
        f.write(f"    .kerning_count = {len(kerning_pairs)},\n")
        f.write(f"    .kerning_pairs = {font_name}_kerning_pairs\n")
        f.write("#endif\n")
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
