import argparse
import os

try:
    from PIL import Image
    PILLOW_AVAILABLE = True
except ImportError:
    PILLOW_AVAILABLE = False

def format_byte_array_to_c_string(data, var_name, bytes_per_line=16):
    c_array_lines = []
    c_array_lines.append(f"static const uint8_t {var_name}[] = {{")
    line_bytes = []
    for i, byte_val in enumerate(data):
        line_bytes.append(f"0x{byte_val:02X}")
        if (i + 1) % bytes_per_line == 0 or i == len(data) - 1:
            c_array_lines.append("    " + ", ".join(line_bytes) + ",")
            line_bytes = []
    if c_array_lines and c_array_lines[-1].strip().endswith(','):
        last_line = c_array_lines[-1].rstrip(); c_array_lines[-1] = last_line[:-1] if last_line.endswith(',') else last_line
    c_array_lines.append("};")
    return "\n".join(c_array_lines)

def create_c_header_std_v9(gif_path, header_path, resource_name):
    if not os.path.exists(gif_path): print(f"Error: Input GIF file not found at '{gif_path}'"); return
    width = 0; height = 0
    if PILLOW_AVAILABLE:
        try:
            with Image.open(gif_path) as img:
                if img.format != 'GIF': print(f"Warning: Input file '{gif_path}' might not be a GIF. Pillow reports format: {img.format}")
                width = img.width; height = img.height
                print(f"Successfully read GIF dimensions: {width}x{height} from '{gif_path}' using Pillow.")
        except Exception as e: print(f"Warning: Could not get GIF dimensions: {e}. W/H will be 0.")
    else: print("Warning: Pillow library not installed (pip install Pillow). W/H will be 0.")
    try:
        with open(gif_path, 'rb') as f_gif: gif_binary_data = f_gif.read()
    except Exception as e: print(f"Error: Could not read GIF file '{gif_path}'. Error: {e}"); return

    header_guard = f"{resource_name.upper()}_H"; raw_data_var_name = f"raw_gif_data_{resource_name}"
    c_header_content = [f"#ifndef {header_guard}", f"#define {header_guard}", "",
                        f"// Generated from '{os.path.basename(gif_path)}' by gif_to_lvgl_header_std_v9.py (for Standard LVGL v9)",
                        f"// Resource Name: {resource_name}", "",
                        '// This file assumes LVGL (v9) core headers (lvgl.h) are included before it.',
                        '// #include "lvgl.h"', "",
                        "#ifdef __cplusplus", "extern \"C\" {", "#endif", "",
                        f"// Raw binary data for {os.path.basename(gif_path)}",
                        format_byte_array_to_c_string(gif_binary_data, raw_data_var_name), "",
                        f"// LVGL v9 standard image descriptor for {resource_name}",
                        f"static const lv_image_dsc_t {resource_name} = {{",
                        f"    .header = {{",
                        f"        .magic = LV_IMAGE_HEADER_MAGIC,       // LVGL v9 specific magic number",
                        f"        .w = {width},                         // Width of the image",
                        f"        .h = {height},                        // Height of the image",
                        f"        .stride = 0,                      // For RAW GIF data, stride is usually handled by decoder",
                        f"        .format = LV_COLOR_FORMAT_RAW     // LVGL v9 color format for raw data",
                        # Standard LVGL v9 header doesn't have .always_zero.
                        # .reserved_2 is part of lv_image_header_t but usually not explicitly initialized for assets, so it's omitted here.
                        f"    }},",
                        f"    .data_size = sizeof({raw_data_var_name}),    // Size of the raw_gif_data array",
                        f"    .data = {raw_data_var_name},           // Pointer to the raw GIF data",
                        "};", "",
                        "#ifdef __cplusplus", "} /*extern \"C\"*/", "#endif", "",
                        f"#endif /* {header_guard} */", ""]
    try:
        with open(header_path, 'w') as f_header: f_header.write("\n".join(c_header_content))
        print(f"Successfully converted '{gif_path}' to '{header_path}' (Standard LVGL v9 format) as C resource '{resource_name}'.")
    except Exception as e: print(f"Error: Could not write C header file: {e}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Convert GIF to C header (Standard LVGL v9 format).")
    parser.add_argument("input_gif", help="Input GIF file"); parser.add_argument("output_header", help="Output C header file")
    parser.add_argument("resource_name", help="C resource name")
    args = parser.parse_args()
    if not args.resource_name.isidentifier(): print(f"Error: Resource name '{args.resource_name}' is not valid.")
    else: create_c_header_std_v9(args.input_gif, args.output_header, args.resource_name)