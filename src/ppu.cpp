#include "../include/ppu.h"
#include "../include/cartridge.h"
#include <iostream>

void PPU::step_one_cycle()
{
    // =========================================================================
    // NTSC NES PPU timing model:
    // - 341 dots (PPU cycles) per scanline
    // - 262 scanlines per frame
    //
    // Important:
    // 341 is NOT the visible screen width in pixels.
    // The visible picture is about 256 pixels wide, but one full scanline
    // lasts 341 PPU cycles total. The extra cycles account for non visible
    // timing/fetch/blanking periods the PPU still spends time on.
    //
    // Same idea vertically:
    // 240 scanlines are visible picture, but a full frame also includes
    // vblank scanlines and the pre render scanline, giving 262 total.
    // =========================================================================

    current_dot++; // advance one PPU timing step (one dot) within the current scanline

    if(current_dot >= 341){
        current_dot = 0;   // start next scanline at dot 0
        current_scanline++;

        if(current_scanline >= 262){
            current_scanline = 0; // start a new frame
        }
    }

    // =========================================================================
    // Because during vblank, the screen is not actively showing visible lines
    // the games use that time to safely update palettes, nametables,
    // sprites, scroll state etc. So its very important to set vblank flag
    // during this period and clear it before the last scanline (261)
    // =========================================================================
    if(current_scanline == 241 && current_dot == 1){
        regs.ppustatus |= 0b10000000; // set vblank flag

        // =====================================================================
        // Vblank is the safe "graphics update window" for the game.
        // If NMI_on_vblank is enabled in PPUCTRL, then when "vblank window"
        // starts the PPU requests an NMI from the CPU.
        // The CPU then saves its current state and jumps to the NMI vector
        // at $FFFA-$FFFB in cartridge PRG_ROM, which gives the address of
        // the game's NMI handler routine (in the cartridge).
        // Games usually use that handler to update PPU related state such as
        // palettes, nametables, sprites, and scroll values.
        // =====================================================================
        if(ppuctrlFields.vblank_NMI_allowed){
            NMI_request = true;
        }
    }

    if(current_scanline == 261 && current_dot == 1){ // clear vblank flag
        regs.ppustatus &= 0b01111111;
    }
}

Byte PPU::cpu_read_register(Word addr)
{
    // CPU sees $2000-$2007 but but they are mirrored through $2008-$3FFF
    Word mirrored_addr = 0x2000 + ((addr - 0x2000) % 8);
    std::size_t reg_index = mirrored_addr - 0x2000;
    CPURegisterReadHandler handler = cpu_register_read_handlers[reg_index];
    Byte data = (this->*handler)();
    return data;
}

void PPU::cpu_write_register(Word addr, Byte val)
{
    Word mirrored_addr = 0x2000 + ((addr - 0x2000) % 8);
    std::size_t reg_index = mirrored_addr - 0x2000;
    CPURegisterWriteHandler handler = cpu_register_write_handlers[reg_index];
    (this->*handler)(val);
}

Byte PPU::ppu_internalBus_read(Word addr)
{
    // NOTE: PPU has 16 KB of visible address space (0x0000 to 0x3FFF)

    if(addr > 0x3FFF)  { return 0x00; } // above max addresabe range by PPU

    // 	size = 0x2000 bytes = 8 KB
    if(addr <= 0x1FFF) { return cartridge_ref->ppu_read(addr); }

    // ======================================================================================================
    // 0x2FFF - 0x2000 = 0x0FFF = (4096) 4KB addressable space but only 2KB of actual physical memory in PPU.
    // Conceptually NT0, NT1, NT2, NT3 (4 NameTables of 1KB each, NT0 and NT1 physical/real only).
    // Cartridge mirroring mode defines the type of mapping we implement.
    // ----------------------------------------------------------------------------------
    // Horizontal Mirroring             | Vertical Mirroring                             |
    // ----------------------------------------------------------------------------------
    // NT0 and NT1 share 1 KB page      | NT0 and NT2 share one physical 1 KB page       |
    // NT2 and NT3 share the other 1KB  | NT1 and NT3 share the other physical 1 KB page |
    // ----------------------------------------------------------------------------------
    //
    // ONE EXTRA MODE: Four Screen VRAM
    // Some cartridges extends the physical NameTable space of the PPU by
    // providing the 2KB "missing" memory, when this happens no mirroring is
    // needed and each NameTable maps to its own 1KB of physical space
    //
    // NOTE: if(addr <= 0x3EFF) which means we actually have almost 8 KB of addressable space
    // for the nameTables, so our first "normalization" step is to mirror those upper
    // 4KB (well, tehcnically a little less than 4KB) down to the first 4KB, nintendo
    // cut production cost by cutting the nameTable memory space to 2KB instead of 4KB
    // as this system was design to be and thats why such an extra adressable memory range for it
    // ======================================================================================================
    if(addr <= 0x3EFF){
        Word normalized_addr = 0x2000 + ((addr - 0x2000) % 4096);
        std::size_t idx = std::size_t(cartridge_ref->get_mirroring_mode());
        NametableAddressMapHandler handler = nametable_address_map_handlers[idx];
        NametableMappingResult result = (this->*handler)(normalized_addr);
        if(result.source == NametableSource::PPU_RAM)
            return nametable_mem[result.offset];

        // 4 screen capable cartridges's mapper redirects PPU_R/W: 0x2000-0x2FFF
        // to the cartridge's extra nameTable RAM
        return cartridge_ref->ppu_read(result.offset);
    }

    // 0x3FFF - 0x3F00 = 256 Bytes addressable space but real/physical memory
    // size is 32 Bytes so mapping is needed "down" to those first 32 Bytes
    // Conceptually:
    // 0x3F00 - 0x3F0F -> background palettes (16 bytes, 4 palette groups of 4 bytes each)
    // 0x3F10 - 0x3F1F -> sprite palettes     (16 bytes, same)
    // Weird Behavior:
    // the first byte of each sprite palette should mirror down to the
    // corresponding first byte of the background palette
    Word palette_offset = (addr - 0x3F00) % 32;
    if(palette_offset == 16) palette_offset = 0;
    if(palette_offset == 20) palette_offset = 4;
    if(palette_offset == 24) palette_offset = 8;
    if(palette_offset == 28) palette_offset = 12;
    return palette_mem[palette_offset];
}

void PPU::ppu_internalBus_write(Word addr, Byte val)
{
    if(addr > 0x3FFF)  { return; }

    if(addr <= 0x1FFF) { cartridge_ref->ppu_write(addr, val); return; }

    if(addr <= 0x3EFF){
        Word normalized_addr = 0x2000 + ((addr - 0x2000) % 4096);
        std::size_t idx = std::size_t(cartridge_ref->get_mirroring_mode());
        NametableAddressMapHandler handler = nametable_address_map_handlers[idx];
        NametableMappingResult result = (this->*handler)(normalized_addr);
        if(result.source == NametableSource::PPU_RAM){
            nametable_mem[result.offset] = val;
            return;
        }
        cartridge_ref->ppu_write(result.offset, val);
        return;
    }

    Word palette_offset = (addr - 0x3F00) % 32;
    if(palette_offset == 16) palette_offset = 0;
    if(palette_offset == 20) palette_offset = 4;
    if(palette_offset == 24) palette_offset = 8;
    if(palette_offset == 28) palette_offset = 12;
    palette_mem[palette_offset] = val;
}

NametableMappingResult PPU::map_horizontal_nametable_addr(Word addr)
{
    NametableMappingResult result{NametableSource::PPU_RAM, 0x00};
    Word addr_offset = addr - 0x2000;
    unsigned table = int(addr_offset / 1024); // table = 0, 1, 2, 3?
    result.offset = addr_offset % 1024;    // offset inside that 1 KB table
    if(table >= 2){
        result.offset += 1024; // NT2/NT3 -> second physical 1 KB page
    }
    return result;
}

NametableMappingResult PPU::map_vertical_nametable_addr(Word addr)
{
    NametableMappingResult result{NametableSource::PPU_RAM, 0x00};
    Word addr_offset = addr - 0x2000;
    unsigned table = int(addr_offset / 1024); // table = 0, 1, 2, 3?
    result.offset = addr_offset % 1024;    // offset inside that 1 KB table
    if(table % 2 == 1){
        result.offset += 1024; // NT1/NT3 -> second physical 1 KB page
    }
    return result;
}

NametableMappingResult PPU::map_four_screen_nametable_addr(Word addr)
{
    NametableMappingResult result{NametableSource::PPU_RAM, 0x00};
    Word addr_offset = addr - 0x2000;
    result.offset = addr_offset;
    unsigned table = int(addr_offset / 1024); // table = 0, 1, 2, 3?
    if(table >= 2){ // NT2/NT3 -> Cartridge extra ram (so we map to [0-2048] idx)
        result.offset %= 2048;
        result.source = NametableSource::CARTRIDGE_FOUR_SCREEN_RAM;
    }
    return result;
}

Byte PPU::cpu_read_ppuctrl() { return 0; } // write only CPU facing register

Byte PPU::cpu_read_ppumask() { return 0; } // write only CPU facing register

Byte PPU::cpu_read_ppustatus()
{
    Byte old_status = regs.ppustatus;

    // side effects
    regs.ppustatus = (regs.ppustatus & 0b01111111); // clear vblank bit (7th)
    ppu_addr_toggle = Toggler::FIRST;
    ppu_scroll_toggle = Toggler::FIRST;

    return old_status;
}

Byte PPU::cpu_read_oamaddr() { return 0; } // write only CPU facing register

Byte PPU::cpu_read_oamdata()
{
    return oam_mem[regs.oamaddr];
}

Byte PPU::cpu_read_ppuscroll() { return 0; } // write only CPU facing register

Byte PPU::cpu_read_ppuaddr() { return 0; } // write only CPU facing register

Byte PPU::cpu_read_ppudata()
{
    // "priming" the buffer, first CPUread is old_val, and the second one is
    // the actual Byte the CPU wanted from PPU (so its delayed by one read)
    // Exception:
    // palette reads(0x3F00 - 0x3FFF) -> PPU returns the palette byte immediately
    Byte old_val = read_buffer;
    read_buffer = ppu_internalBus_read(ppu_addr);

    // real palette reads have an extra buffer nuance underneath
    // i have not implemented yet tho
    bool palette_read = false;
    if(ppu_addr >= 0x3F00 && ppu_addr <= 0x3FFF)
        palette_read = true;

    ppu_addr += ppuctrlFields.vram_increment_mode;
    // wrapping around in case we go pass max PPU addressable memory (0x3FFF)
    ppu_addr %= 0x4000;

    return (palette_read) ? read_buffer : old_val;
}

void PPU::cpu_write_ppuctrl(Byte val)
{
    regs.ppuctrl = val;

    Byte encode = regs.ppuctrl & 0b00000011;
    switch (encode)
    {
    case 0: { ppuctrlFields.base_nameTable_addr = 0x2000; break; }
    case 1: { ppuctrlFields.base_nameTable_addr = 0x2400; break; }
    case 2: { ppuctrlFields.base_nameTable_addr = 0x2800; break; }
    case 3: { ppuctrlFields.base_nameTable_addr = 0x2C00; }
    }

    encode = (regs.ppuctrl & 0b00000001);
    ppuctrlFields.scroll_x_bit_8 = encode;

    encode = ((regs.ppuctrl & 0b00000010) >> 1);
    ppuctrlFields.scroll_y_bit_8 = encode;

    encode = ((regs.ppuctrl & 0b00000100) >> 2);
    ppuctrlFields.vram_increment_mode = (encode == 0) ? 1 : 32;

    encode = ((regs.ppuctrl & 0b00001000) >> 3);
    ppuctrlFields.sprite_pattern_table_base = (encode == 0) ? 0x0000 : 0x1000;

    encode = ((regs.ppuctrl & 0b00010000) >> 4);
    ppuctrlFields.background_pattern_table_base = (encode == 0) ? 0x0000 : 0x1000;

    encode = ((regs.ppuctrl & 0b00100000) >> 5);
    ppuctrlFields.sprite_sz = (encode == 0) ? SpriteSize::SZ_8x8 : SpriteSize::SZ_8x16;

    encode = ((regs.ppuctrl & 0b01000000) >> 6);
    ppuctrlFields.master_slave = encode;

    encode = ((regs.ppuctrl & 0b10000000) >> 7);
    ppuctrlFields.vblank_NMI_allowed = encode;
}

void PPU::cpu_write_ppumask(Byte val)
{
    regs.ppumask = val;

    Byte encode = regs.ppumask & 0b00000001;
    ppumaskFields.greyscale = (encode == 1);

    encode = ((regs.ppumask & 0b00000010) >> 1);
    ppumaskFields.show_background_left_8px = (encode == 1);

    encode = ((regs.ppumask & 0b00000100) >> 2);
    ppumaskFields.show_sprites_left_8px = (encode == 1);

    encode = ((regs.ppumask & 0b00001000) >> 3);
    ppumaskFields.background_rendering_enabled = (encode == 1);

    encode = ((regs.ppumask & 0b00010000) >> 4);
    ppumaskFields.sprite_rendering_enabled = (encode == 1);

    encode = ((regs.ppumask & 0b00100000) >> 5);
    ppumaskFields.emphasize_red = (encode == 1);

    encode = ((regs.ppumask & 0b01000000) >> 6);
    ppumaskFields.emphasize_green = (encode == 1);

    encode = ((regs.ppumask & 0b10000000) >> 7);
    ppumaskFields.emphasize_blue = (encode == 1);
}

void PPU::cpu_write_ppustatus(Byte) {} // read only CPU facing register

void PPU::cpu_write_oamaddr(Byte val)
{
    regs.oamaddr = val;
}

void PPU::cpu_write_oamdata(Byte val)
{
    oam_mem[regs.oamaddr++] = val;
}

void PPU::cpu_write_ppuscroll(Byte val)
{
    switch (ppu_scroll_toggle)
    {
    case Toggler::FIRST:
        {
            ppu_scroll_toggle = Toggler::SECOND;
            ppu_scroll_x = val | (Word(ppuctrlFields.scroll_x_bit_8) << 8);
            break;
        }
    case Toggler::SECOND:
        {
            ppu_scroll_toggle = Toggler::FIRST;
            ppu_scroll_y = val | (Word(ppuctrlFields.scroll_y_bit_8) << 8);
        }
    }
}

void PPU::cpu_write_ppuaddr(Byte val)
{
    // clear lo/hi byte
    ppu_addr &= (ppu_addr_toggle == Toggler::FIRST) ? 0x00FF : 0xFF00;

    // set lo/hi byte
    ppu_addr |= (ppu_addr_toggle == Toggler::FIRST) ? Word(val) << 8 : val;

    // The PPU has an address bus that can generate addresses in 0x0000 - 0x3FFF addressable space
    // if both address Bytes "chunks" already received, mirror resulting adress to PPU range
    if(ppu_addr_toggle == Toggler::SECOND)
        ppu_addr &= 0x3FFF;

    ppu_addr_toggle = (ppu_addr_toggle == Toggler::FIRST) ? Toggler::SECOND : Toggler::FIRST;
}

void PPU::cpu_write_ppudata(Byte val)
{
    ppu_internalBus_write(ppu_addr, val);
    ppu_addr += (ppuctrlFields.vram_increment_mode); // PPU addr ptr to next position
    ppu_addr %= 0x4000; // dont go past max apu adressable range
}



DebugImage PPU::build_palette_debug_image()
{
    // ==============================================================================
    // Each palette has 4 bytes, and each byte stores a NES color index/code,
    // not a full RGBA color like modern graphics.
    // NOTE: well, recall 16 * 4 = 64 possible different color codes stored
    // ONLY IF the cartridge provides the extra 2 KB of memory, else only 32

    // Which palette to use?
    // Background: chosen by the attribute table (part of nametable memory area)
    // Sprites: chosen by sprite attributes in OAM

    // Tile graphics data in CHR/pattern memory are decoded by the PPU,
    // producing a pixel value in the range [0, 3].

    // Once a palette has already been selected, that pixel value [0, 3]
    // is used as an index into that palette's 4 entries to choose
    // which NES color code to use.

    // That final color code then goes into the PPU's video output hardware,
    // which turns it into the actual analog color signal sent to the TV.
    // ==============================================================================

    // ===================================================
    // For any pixel (x, y) in a Row Major image buffer:
    // index = y * width + x
    // ===================================================
    DebugImage image{.height = 64, .width = 128};

    // A flat vector, its data stored as Row0 (left to right), then
    // Row1 (left to right), etc ... (ROW MAJOR so we can use the formula)
    image.pixels.resize(image.width * image.height);

    constexpr int block_size = 16;      // 16x16 pixels blocks
    constexpr int blocks_per_row = 8;

    for (int i = 0; i < 32; i++) {
        Byte color_code = palette_mem[i];
        std::uint32_t rgba = nes_color_code_to_rgba32(color_code);

        int block_row = i / blocks_per_row;
        int block_col = i % blocks_per_row;

        int start_x = block_col * block_size;
        int start_y = block_row * block_size;

        for (int dy = 0; dy < block_size; dy++) {
            for (int dx = 0; dx < block_size; dx++) {
                int x = start_x + dx;
                int y = start_y + dy;
                image.pixels[y * image.width + x] = rgba;
            }
        }
    }

    return image;
}

DebugImage PPU::build_pattern_table_debug_image()
{
    // ==============================================================================================
    // The NES has 2 pattern tables, 4 KB each.
    // Together they occupy 0x0000 - 0x1FFF in PPU address space.
    // In practice, reads from this range usually map to cartridge CHR ROM / CHR RAM.
    //
    // Each pattern table contains 256 tiles.
    // Each tile is 16 bytes and represents an 8x8 pixel image.
    //
    // This is the raw graphics data used by the NES.
    // Important: this only describes the tile's pixel pattern/shape,
    // not its final color, screen position, or scroll behavior.
    //
    // Note:
    // An 8x8 tile has 64 pixels. A bare minimum black/white version would need
    // 1 bit per pixel = 64 bits = 8 bytes total.
    //
    // The NES instead uses 2 bits per pixel = 128 bits = 16 bytes total,
    // so each tile pixel can be one of 4 values instead of just 2.
    //
    // Intuitively, instead of only having an "off/on" shape, the NES can
    // describe that shape with 4 possible shade levels.
    //
    // For the debug viewer, we will use these values:
    // 0 = black
    // 1 = dark gray / shadow
    // 2 = light gray
    // 3 = white
    //
    // Important:
    // black/gray/white here is ONLY a simple debug interpretation to visualize
    // the 4 possible tile values. In real NES rendering, those 4 values are not
    // fixed black/white shades and can end up as any 4 colors chosen by the
    // currently selected palette.
    //
    // So instead of a very flat black/white outline, a shape like a ball can
    // be represented with richer shading and interior detail.
    // ==============================================================================================

    // ==============================================================================================
    //-----------------------
    //      BITPLANES       |
    // ----------------------
    // Each tile is 16 bytes and represents an 8x8 pixel image.
    //
    // Those 16 bytes are split into 2 bitmaps/bitplanes:
    // - low bitplane  = first  8 bytes
    // - high bitplane = second 8 bytes
    //
    // Each bitplane is an 8-byte bitmap, meaning 1 bit per pixel for the
    // 8x8 tile (64 pixels total). By itself, one bitplane would only allow a
    // simple on/off black/white style shape.
    //
    // Nintendo instead uses 2 bitplanes, so each tile pixel gets 2 bits total:
    // - one bit from the low plane
    // - one bit from the high plane
    //
    // Combining those 2 bits gives 4 possible pixel values:
    // 00 -> 0
    // 01 -> 1
    // 10 -> 2
    // 11 -> 3
    //
    // That is how the NES stores richer tile shapes with shading/detail instead
    // of only a flat black/white outline.
    // ==============================================================================================

    // we will return TWO 16 tiles across and 16 tiles down side by side image.
    // or same a 32x16 TILES debugging image
    DebugImage image{.width = 2 * 16 * 8, .height = 16 * 8};
    image.pixels.resize(image.width * image.height);

    // 0x0000 - 0x1FFF is the PPU range for pattern tables internally
    std::array<Byte, 16> tile{};
    int counter = 0;

    constexpr int tiles_per_row = 16;
    constexpr int tile_pixel_size = 8;

    int tile_idx = 0;
    constexpr int right_table_offset = 128; // 16 tiles * 8 pixels = 128
    bool table_is_left = true;

    for(Word addr = 0x0000; addr <= 0x1FFF; addr++){
        tile[counter++] = ppu_internalBus_read(addr);

        if(counter >= static_cast<int>(tile.size())){
            int tile_row = tile_idx / tiles_per_row;
            int tile_col = tile_idx % tiles_per_row;

            int tile_start_x = tile_col * tile_pixel_size;
            int tile_start_y = tile_row * tile_pixel_size;

            int table_x_offset = table_is_left ? 0 : right_table_offset;

            for(int row = 0; row < 8; row++){
                Byte row_byte_lo = tile[row];
                Byte row_byte_hi = tile[row + 8];

                for(int col = 0; col < 8; col++){
                    // 0 means “leftmost pixel” (first (left to right) pixel in row)
                    bool pixel_lo = row_byte_lo & (0x1 << (7 - col));
                    bool pixel_hi = row_byte_hi & (0x1 << (7 - col));

                    unsigned int color_code =
                        ((pixel_hi ? 1 : 0) << 1) |
                        (pixel_lo ? 1 : 0);

                    std::uint32_t rgba = DEBUG_GRAYSCALE_RGBA[color_code];

                    int x = table_x_offset + tile_start_x + col;
                    int y = tile_start_y + row;

                    image.pixels[y * image.width + x] = rgba;
                }
            }

            tile_idx++;
            if (tile_idx >= 256) {
                tile_idx = 0;
                table_is_left = false;
            }

            counter = 0;
        }
    }

    return image;
}

DebugImage PPU::build_nametable_debug_image()
{
    // =================================================================================
    // A nametable is basically a grid of tile indices.
    // Example: tile 0x24 goes at row 0, col 0; tile 0x8A goes at row 0, col 1, etc.
    //
    // Nametables live in 0x2000 - 0x2FFF in PPU address space.
    // Conceptually there are 4 nametables of 1 KB each.
    // (Some cartridges can provide extra nametable memory, e.g. four-screen mode.)
    //
    // For one nametable:
    // - the background tile map is 32 tiles wide by 30 tiles tall
    // - that means 32 * 30 = 960 bytes of tile indices
    //
    // The last 64 bytes of each 1 KB nametable block are not tile indices;
    // they are the attribute table.
    // (Note: 960 + 64 = 1024 bytes = 1 KB total.)
    //
    // So for nametable 0:
    // - 0x2000 - 0x23BF = tile indices (960 bytes)
    //   (each byte stores a tile index in the range [0, 255], selecting one tile
    //    from the currently chosen background pattern table)
    // - 0x23C0 - 0x23FF = attribute table (last 64 bytes)
    //
    // Since each tile is 8x8 pixels, one full nametable covers:
    // - width  = 32 * 8 = 256 pixels
    // - height = 30 * 8 = 240 pixels
    //
    // PPUCTRL bits 0-1 select the base nametable:
    // 00 -> $2000
    // 01 -> $2400
    // 10 -> $2800
    // 11 -> $2C00
    //
    // PPUCTRL bit 4 selects the background pattern table:
    // 0 -> $0000
    // 1 -> $1000
    // =================================================================================

    DebugImage image{.width = 32 * 8, .height = 30 * 8};
    image.pixels.resize(image.width * image.height);

    std::array<Byte, 1024> nameTable{}; // lets output a single nametable (the current one)
    for(int chunk = 0; chunk < 1024; chunk++){
        Word addr = ppuctrlFields.base_nameTable_addr + chunk;
        nameTable[chunk] = ppu_internalBus_read(addr);
    }

    constexpr int tiles_per_row = 32;
    constexpr int tile_pixel_size = 8;

    int flat_idx = 0;
    for(int tile_row = 0; tile_row < 30; tile_row++){
        for(int tile_col = 0; tile_col < 32; tile_col++){
            Byte tile_idx = nameTable[flat_idx];

            std::array<Byte, 16> tile{}; // a tile in pattern_table is 16 bytes in size
            for(int byte = 0; byte < 16; byte++){
                tile[byte] = ppu_internalBus_read(
                                ppuctrlFields.background_pattern_table_base
                                + 16 * tile_idx + byte);
            }

            // draw tile on image pixel buffer
            int tile_start_x = tile_col * tile_pixel_size;
            int tile_start_y = tile_row * tile_pixel_size;

            for(int row = 0; row < 8; row++){
                Byte row_byte_lo = tile[row];
                Byte row_byte_hi = tile[row + 8];

                for(int col = 0; col < 8; col++){
                    // 0 means “leftmost pixel” (first (left to right) pixel in row)
                    bool pixel_lo = row_byte_lo & (0x1 << (7 - col));
                    bool pixel_hi = row_byte_hi & (0x1 << (7 - col));

                    unsigned int color_code =
                        ((pixel_hi ? 1 : 0) << 1) |
                        (pixel_lo ? 1 : 0);

                    std::uint32_t rgba = DEBUG_GRAYSCALE_RGBA[color_code];

                    int x = tile_start_x + col;
                    int y = tile_start_y + row;

                    image.pixels[y * image.width + x] = rgba;
                }
            }

            flat_idx++;
        }
    }

    return image;
}