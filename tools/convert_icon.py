from PIL import Image  # noqa: I001

def convert_icon(image_path: str, output_path: str, var_name: str) -> None:
    """
    Convert a PNG icon to a 1-bit C-style header array for PROGMEM.
    Resizes to 160x160 if necessary.
    """
    try:
        # Using context manager ensures the image file is closed properly
        with Image.open(image_path) as img:
            img = img.convert("1")  # Convert to 1-bit monochrome
            width, height = img.size
            
            if width != 160 or height != 160:
                print(f"Warning: Dimensions are {width}x{height}, resizing to 160x160")
                img = img.resize((160, 160))
                width, height = 160, 160
                
            # Efficiently load pixel map into memory (Fixes F841)
            pixels = img.load()
            
            bytes_list = []
            current_byte = 0
            bit_count = 0
            
            # Pack bits: Row by row, MSB first
            for y in range(height):
                for x in range(width):
                    # PIL '1' mode: 0 is black, >0 is white.
                    # GFX expects 1 for Color (Black), 0 for Bg (White)
                    pixel_val = pixels[x, y]
                    bit = 1 if pixel_val == 0 else 0
                    
                    current_byte = (current_byte << 1) | bit
                    bit_count += 1
                    
                    if bit_count == 8:
                        bytes_list.append(current_byte)
                        current_byte = 0
                        bit_count = 0
                        
            line_len = width // 8
            
            # Write out the C/C++ header file
            with open(output_path, "w", encoding="utf-8") as f:
                f.write(f"#ifndef {var_name.upper()}_H\n")
                f.write(f"#define {var_name.upper()}_H\n\n")
                f.write("#include <pgmspace.h>\n\n")
                f.write(f"const unsigned char {var_name}[] PROGMEM = {{\n")
                
                for i, b in enumerate(bytes_list):
                    f.write(f"0x{b:02x}, ")
                    if (i + 1) % line_len == 0:
                        f.write("\n")
                        
                f.write("};\n\n")
                f.write(f"#endif // {var_name.upper()}_H\n")
                print(f"Success: {output_path} generated.")

    except FileNotFoundError:
        print(f"Error: Target image '{image_path}' not found.")

if __name__ == "__main__":
    convert_icon("icon_reader.png", "lib/Apps/AppReader/icon_reader.h", "icon_reader_160x160")
    convert_icon("icon_update.png", "lib/KomaBon_Apps/icon_update.h", "icon_update_160x160")