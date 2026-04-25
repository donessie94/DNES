#include "../include/ppu.h"
#include "../include/cartridge.h"
#include <iostream>
#include "ppu.h"

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

    current_dot++;  // starts at -1 so first real dot becomes 0

    const bool rendering_enabled =
        ppumaskFields.background_rendering_enabled ||
        ppumaskFields.sprite_rendering_enabled;

    const bool visible_scanline = (current_scanline >= 0 && current_scanline < 240);
    const bool prerender_scanline = (current_scanline == 261);
    const bool rendering_scanline = visible_scanline || prerender_scanline;

    // ============================================================
    // Visible output stream
    // IMPORTANT:
    // Sample before shifting for this dot.
    // ============================================================
    // Note: dot number and pixel x coordinate are not the same thing (timing vs pixels)
    // Mapping is: screen_pixel_x = current_dot - 1
    if(visible_scanline && current_dot >= 1 && current_dot <= 256){
        const int pixel_x = current_dot - 1;
        const int pixel_y = current_scanline;
        const int flat_idx = pixel_y * 256 + pixel_x;

        BackgroundPixelSample bg_sample = sample_current_background_pixel_rgba();

        SpritePixelSample sprite0_data = sample_sprite_pixel(0, pixel_x, pixel_y);

        // Start with sprite 0 as the chosen sprite if it is visible.
        // That way we do not sample it twice, but it still gets a chance
        // to be the rendered sprite like any other OAM entry.
        SpritePixelSample chosen_sprite{};
        if(sprite0_data.is_opaque){
            chosen_sprite = sprite0_data;
        }

        for(int idx : overlapping_sprites_idx){

            if(idx == 0) continue; // No need to process sprite0 twice

            SpritePixelSample candidate = sample_sprite_pixel(idx, pixel_x, pixel_y);

            if(candidate.is_opaque){
                // Lower OAM index wins, so only replace the chosen sprite
                // if we do not already have one.
                if(!chosen_sprite.is_opaque){
                    chosen_sprite = candidate;
                }
                break;
            }
        }

        // Left edge masking from PPUMASK applies to the final pixel state,
        // not to each candidate sprite during the search loop.
        apply_left_edge_render_mask(pixel_x, bg_sample, sprite0_data, chosen_sprite);

        update_sprite_zero_hit_flag(pixel_x, bg_sample, sprite0_data, chosen_sprite);

        screen_pixels[flat_idx] =
            composite_background_and_sprite(bg_sample, chosen_sprite);
    }

    // ============================================================
    // Rendering related scroll/address events
    // ============================================================
    // current_vram_addr is the single source of truth for the actively
    // rendered/fetched background position.
    //
    // temp_vram_addr is the single source of truth for what CPU writes
    // to PPUCTRL/PPUSCROLL/PPUADDR have built up.
    if(rendering_enabled && rendering_scanline){
        if(current_dot == 256){
            increment_vertical_background_vram_addr();
        }

        if(current_dot == 257){
            copy_horizontal_scroll_bits_from_temp_to_current_vram_addr();
        }

        if(prerender_scanline && current_dot >= 280 && current_dot <= 304){
            copy_vertical_scroll_bits_from_temp_to_current_vram_addr();
        }
    }

    // ============================================================
    // Background fetch pipeline
    //
    // Active on:
    // - visible scanlines 0..239
    // - pre render scanline 261
    //
    // Dots:
    // - 1..256   : visible tile pipeline
    // - 321..336 : late prefetch for next scanline
    //
    // NOTE:
    // 337..340 EXIST AND MUST YET BE IMPLEMENTED
    // ============================================================
    if(rendering_enabled && rendering_scanline){
        if((current_dot >= 1 && current_dot <= 256)
            || (current_dot >= 321 && current_dot <= 336)){

            const int phase = (current_dot - 1) % 8;
            const BackgroundFetchPhase dot_phase = BACKGROUND_FETCH_PHASE_MAP[phase];

            // Advance the active background pipeline by one pixel.
            shift_background_registers();

            // Readibility is great like this but i am repeating the
            // same fetching twice for no reason with this set up tho...
            // (ex: 0-1 -> fetch_next_background_nametable_byte() twice
            switch(dot_phase)
            {
            case BackgroundFetchPhase::NametableFetch:
                fetch_next_background_nametable_byte();
                break;

            case BackgroundFetchPhase::AttributeFetch:
                fetch_next_background_attribute_byte();
                break;

            case BackgroundFetchPhase::TileLowFetch:
                fetch_next_background_pattern_tile_low_byte();
                break;

            case BackgroundFetchPhase::TileHighFetch:
                fetch_next_background_pattern_tile_high_byte();

                // End of one 8 dot tile group in our simplified model.
                // make fetched tile data active, then step to next tile horizontally.
                if(phase == 7){
                    load_background_shift_registers();
                    increment_horizontal_background_vram_addr();
                }
                break;
            }
        }
    }

    // ============================================================
    // VBlank events
    // ============================================================
    // Because during vblank, the screen is not actively showing visible lines
    // the games use that time to safely update palettes, nametables,
    // sprites, scroll state etc. So its very important to set vblank flag
    // during this period and clear it before the last scanline (261)
    if(current_scanline == 241 && current_dot == 1){
        regs.ppustatus |= 0b10000000;

        // Vblank is the safe "graphics update window" for the game.
        // If NMI_on_vblank is enabled in PPUCTRL, then when "vblank window"
        // starts the PPU requests an NMI from the CPU.
        // The CPU then saves its current state and jumps to the NMI vector
        // at $FFFA-$FFFB in cartridge PRG_ROM, which gives the address of
        // the game's NMI handler routine (in the cartridge).
        // Games usually use that handler to update PPU related state such as
        // palettes, nametables, sprites, and scroll values.
        if(ppuctrlFields.vblank_NMI_allowed){
            NMI_request = true;
        }
    }

    // clear VBLANK, hit 0 flag (bit 6), sprite overflow flag (bit 5)
    if(current_scanline == 261 && current_dot == 1){
        regs.ppustatus &= 0b00011111;
    }

    // ============================================================
    // Scanline / frame wrap
    // ============================================================
    if(current_dot >= 341){
        current_dot = -1;
        current_scanline++;

        if(current_scanline >= 262){
            current_scanline = 0;
            frame_ready = true;
        }

        // Build the sprite list once for the NEW scanline we are about to render.
        // Only visible scanlines need this list for our current simplified sprite path.
        if(current_scanline < 240){
            // WE SHOULD CACHE THE SPRITE ROW BYTES ONCE PER SCANLINE (LATER)
            collect_visible_sprite_indices_for_scanline(current_scanline);
        }
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
        Word normalized_addr = addr;
        if(normalized_addr >= 0x3000){
            normalized_addr -= 0x1000;
        }

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
        Word normalized_addr = addr;
        if(normalized_addr >= 0x3000){
            normalized_addr -= 0x1000;
        }

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
    ppu_write_toggle = Toggler::FIRST;

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
    Word& current_vram_addr = background_scroll_state.current_vram_addr;

    Byte old_val = read_buffer;
    read_buffer = ppu_internalBus_read(current_vram_addr);

    // real palette reads have an extra buffer nuance underneath
    // i have not implemented yet tho
    bool palette_read = false;
    if(current_vram_addr >= 0x3F00 && current_vram_addr <= 0x3FFF)
        palette_read = true;

    current_vram_addr += ppuctrlFields.vram_increment_mode;
    // wrapping around in case we go pass max PPU addressable memory (0x3FFF)
    current_vram_addr %= 0x4000;

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
    ppuctrlFields.nametable_select_x = encode;

    encode = ((regs.ppuctrl & 0b00000010) >> 1);
    ppuctrlFields.nametable_select_y = encode;

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

    // PPUCTRL bits 0-1 also update the nametable select bits inside
    // the temp VRAM address (t register) directly.
    Word& temp_vram_addr = background_scroll_state.temp_vram_addr;

    temp_vram_addr &= Word(~((Word(0x1) << 10) | (Word(0x1) << 11))); // clear nametable select bits 10-11
    temp_vram_addr |= Word(ppuctrlFields.nametable_select_x) << 10;
    temp_vram_addr |= Word(ppuctrlFields.nametable_select_y) << 11;
}

void PPU::cpu_write_ppumask(Byte val)
{
    regs.ppumask = val;

    Byte encode = regs.ppumask & 0b00000001;
    ppumaskFields.greyscale = (encode == 1);

    encode = ((regs.ppumask & 0b00000010) >> 1);
    ppumaskFields.show_background_leftmost_8_pixels = (encode == 1);

    encode = ((regs.ppumask & 0b00000100) >> 2);
    ppumaskFields.show_sprites_leftmost_8_pixels = (encode == 1);

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
    // When the CPU writes to PPUSCROLL, it is basically telling the PPU:
    // how many pixels to shift horizontally into the background
    // how many pixels to shift vertically into the background
    //
    // Example: val = 13, for X scroll, that means:
    // move 13 pixels to the right into the background
    //
    // What 13 pixels means relative to 8x8 tiles:
    // 8 pixels = 1 full tile
    // leftover pixels inside the next tile = fine scroll (+5 pixels offset)
    //
    // Thus:
    // 13 / 8 = 1 -> coarse_x/tile_col_x (full tiles crossed)
    // 13 % 8 = 5 -> fine_x (extra pixels into the next tile)
    //
    // Because one byte only gives us 0..255, but background scrolling
    // can continue beyond 255 pixels into the neighboring nametable we
    // add the 8 bit (it represents whether we are in the left/right
    // nametable)
    //
    // This is important information cuz the tmp_vram_addr uses these
    // values to properly build some its packed values
    // In the actual PPU, PPUSCROLL writes directly build the temp VRAM
    // address (t register) plus the separate fine_x field.
    Word& temp_vram_addr = background_scroll_state.temp_vram_addr;

    Byte& fine_x = background_scroll_state.fine_x_scroll;

    switch (ppu_write_toggle)
    {
    case Toggler::FIRST:
        {
            ppu_write_toggle = Toggler::SECOND;
            unsigned int coarse_x = (val >> 3) & 0b11111;
            fine_x = val & 0b111;
            temp_vram_addr &= Word(0b1111111111100000); // clear coarse_x bits
            temp_vram_addr |= Word(coarse_x);
            break;
        }
    case Toggler::SECOND:
        {
            ppu_write_toggle = Toggler::FIRST;
            unsigned int coarse_y = (val >> 3) & 0b11111;
            unsigned int fine_y = val & 0b111;
            temp_vram_addr &= Word(0b0000110000011111); // clear coarse_y, fine_y, keep nametable bits and coarse_x
            temp_vram_addr |= Word(coarse_y) << 5;
            temp_vram_addr |= Word(fine_y) << 12;
            break;
        }
    }
}

void PPU::cpu_write_ppuaddr(Byte val)
{
    Word& temp_vram_addr = background_scroll_state.temp_vram_addr;
    Word& current_vram_addr = background_scroll_state.current_vram_addr;

    switch (ppu_write_toggle)
    {
    case Toggler::FIRST:
        {
            // clear lo/hi byte
            temp_vram_addr &= Word(0b0000000011111111);

            // set lo/hi byte
            temp_vram_addr |= Word(val & 0b00111111) << 8;
            ppu_write_toggle = Toggler::SECOND;
            break;
        }
    case Toggler::SECOND:
        {
            // clear lo/hi byte
            temp_vram_addr &= 0x7F00;

            // set lo/hi byte
            temp_vram_addr |= val;

            // The PPU has an address bus that can generate addresses in 0x0000 - 0x3FFF addressable space
            // if both address Bytes "chunks" already received, mirror resulting adress to PPU range
            temp_vram_addr &= 0x3FFF;
            current_vram_addr = temp_vram_addr;
            ppu_write_toggle = Toggler::FIRST;
            break;
        }
    }
}

void PPU::cpu_write_ppudata(Byte val)
{
    Word& current_vram_addr = background_scroll_state.current_vram_addr;

    ppu_internalBus_write(current_vram_addr, val);
    current_vram_addr += (ppuctrlFields.vram_increment_mode); // PPU addr ptr to next position
    current_vram_addr %= 0x4000; // dont go past max apu adressable range
}


void PPU::rebuild_temp_vram_addr_from_scroll_state()
{
    // There are two addresses:
    // Temporary VRAM address = what scroll/address writes build up
    // Current VRAM address = what rendering is actively using right now
    //
    // So:
    // CPU writes to PPUCTRL contribute the nametable bits
    // CPU writes to PPUSCROLL contribute coarse X (tile_col), coarse Y (tile_row), fine X, fine Y
    // CPU writes to PPUADDR also affect the temp/current address path
    // then current_vram_addr is loaded from that temporary state at the proper time
    //
    // ==================================================
    // PACKED LAYOUT
    // ==================================================
    // yyy NN YYYYY XXXXX (top/15 bit is not used)
    // ||| || ||||| +++++-- tile column within nametable
    // ||| || +++++-------- tile row within nametable
    // ||| ++-------------- nametable select bits (bit 10 col/lo, bit 11 row/hi)
    // +++----------------- fine Y scroll

    // IMPORTANT:
    // After moving to the real NES style write path, temp_vram_addr and fine_x
    // are now built directly by:
    // - cpu_write_ppuctrl()
    // - cpu_write_ppuscroll()
    // - cpu_write_ppuaddr()
    //
    // So this function is no longer the source of truth for scroll state
    // reconstruction and should not be used by the active rendering path.
}

void PPU::copy_horizontal_scroll_bits_from_temp_to_current_vram_addr()
{
    Word& tmp_vram_addr = background_scroll_state.temp_vram_addr;
    Word& current_vram_addr = background_scroll_state.current_vram_addr;

    current_vram_addr &= Word(0b0111101111100000); // clear horizontal bits

    bool nametable_x_set = (tmp_vram_addr & Word(0b0000010000000000));

    unsigned int coarse_x = tmp_vram_addr & Word(0b0000000000011111);

    if(nametable_x_set){
        current_vram_addr |= (Word(0x1) << 10);
    }

    current_vram_addr |= Word(coarse_x);
}

void PPU::copy_vertical_scroll_bits_from_temp_to_current_vram_addr()
{
    Word& tmp_vram_addr = background_scroll_state.temp_vram_addr;
    Word& current_vram_addr = background_scroll_state.current_vram_addr;

    current_vram_addr &= Word(0b0000010000011111); // clear vertical bits

    bool nametable_y_set = (tmp_vram_addr & Word(0b0000100000000000));

    unsigned int coarse_y = (tmp_vram_addr & Word(0b0000001111100000)) >> 5;

    unsigned int fine_y = (tmp_vram_addr & Word(0b0111000000000000)) >> 12;

    if(nametable_y_set){
        current_vram_addr |= (Word(0x1) << 11);
    }

    current_vram_addr |= (Word(coarse_y << 5) | Word(fine_y << 12));
}

void PPU::increment_horizontal_background_vram_addr()
{
    // We move the background fetch position (current_vram_addr) one
    // whole tile to the right. In other words, we advance one tile step
    // horizontally in the background nametable world.
    //
    // Important:
    // this is one full tile step, not one pixel.
    //
    // So moving horizontally means:
    // 1. usually, increase the tile column field in current_vram_addr by 1
    // 2. if we were already at the last column, wrap back to column 0
    // 2b. and flip the horizontal nametable select bit in current_vram_addr
    //
    //   NT0            NT1
    // ___________  ___________
    // |...|30|31|  |0|1|2|...|
    //
    // When moving horizontally, going right from tile column 31 means we wrap
    // back to tile column 0 in the horizontally adjacent nametable.
    //
    // Note:
    // this logical nametable transition is part of the PPU's scroll/fetch model
    // regardless of mirroring.

    // reference
    Word& current_vram_addr = background_scroll_state.current_vram_addr;

    int old_col = get_tile_col_within_nametable(current_vram_addr);
    current_vram_addr &= Word(0b1111111111100000); // clear column bits
    int new_col = old_col + 1;

    if(new_col > 31){
        new_col = 0;
        current_vram_addr ^= 0b0000010000000000; // flip horizontal bit
    }

    current_vram_addr |= Word(new_col);
}

void PPU::increment_vertical_background_vram_addr()
{
    // Move the background fetch position down by one PIXEL row
    // NOTE: Not one tile row down (mirroring horizontal increment)
    // BUT BY: One PIXEL row vertically in the background world.
    //
    // The reason is because scanlines advance one pixel row at a time
    // through tiles.
    // scanline slice 0 -> fine Y = 0
    // .
    // .
    // .
    // scanline slice 7 -> fine Y = 7
    // Only after that we move to the next tile row.

    // reference
    Word& current_vram_addr = background_scroll_state.current_vram_addr;

    int old_fine_y_scroll = get_fine_y_scroll(current_vram_addr);
    int new_fine_y_scroll = old_fine_y_scroll + 1;

    current_vram_addr &= Word(0b0000111111111111); // clear fine_y bits

    if(new_fine_y_scroll < 8){
        current_vram_addr |= Word(new_fine_y_scroll) << 12;
        return;
    }
    else{
        // fine_y_scroll equals zero (wrap around) clearing took care of it tho
        int old_row = get_tile_row_within_nametable(current_vram_addr);
        int new_row = old_row + 1;

        // Clear row bits = make them zero, because we do
        // this here there is no need to do it for steps 29 and 31
        current_vram_addr &= Word(0b1111110000011111);

        if(old_row == 29){

            // This means we are at the last visible tile row of the nametable (30 tile rows).
            // So if you move down from tile row 29 after fine Y wraps:
            // 1. tile row wraps to 0
            // 2. vertical nametable bit flips
            // Thaat moves us to the top row of the nametable below.
            // This is the vertical analog of horizontal wrap.

            current_vram_addr ^= 0b0000100000000000; // flip vertical bit
        }

        else if(old_row == 31){

            //This comes from the fact that the 5 bit tile row field can hold
            // values up to 31, even though visible background rows are
            // effectively 0..29.
            //
            // So rows 30 and 31 are kind of “extra state space” in the packed
            // address, and the hardware has this special behavior.

            // Thus:
            // tile row wraps to 0
            // but vertical nametable bit does not flip
        }

        else{ // if not special case just new row is packed into the address
            current_vram_addr |= Word(new_row) << 5;
        }
    }
}

int PPU::get_tile_col_within_nametable(Word vram_addr)
{
    // ==================================================
    // PACKED LAYOUT
    // ==================================================
    // yyy NN YYYYY XXXXX
    // ||| || ||||| +++++-- tile column within nametable
    // ||| || +++++-------- tile row within nametable
    // ||| ++-------------- nametable select bits (bit 10 col/lo, bit 11 row/hi)
    // +++----------------- fine Y scroll

    return vram_addr & Word(0b11111);
}

int PPU::get_tile_row_within_nametable(Word vram_addr)
{
    return (vram_addr & Word(0b1111100000)) >> 5;
}

int PPU::get_nametable_col_select(Word vram_addr)
{
    return (vram_addr & Word(0b10000000000)) >> 10;
}

int PPU::get_nametable_row_select(Word vram_addr)
{
    return (vram_addr & Word(0b100000000000)) >> 11;
}

int PPU::get_fine_y_scroll(Word vram_addr)
{
    return (vram_addr & Word(0b111000000000000)) >> 12;
}

int PPU::get_nametable_number(Word vram_addr)
{
    uint8_t lo = (vram_addr & Word(0b10000000000)) >> 10;
    uint8_t hi = (vram_addr & Word(0b100000000000)) >> 11;
    return int((hi << 1) | lo);
}

void PPU::fetch_next_background_nametable_byte()
{
    // current_vram_addr is the single source of truth for the actively
    // rendered/fetched background position.
    Word curr_vram_addr = background_scroll_state.current_vram_addr;

    int tile_row = get_tile_row_within_nametable(curr_vram_addr);
    int tile_col = get_tile_col_within_nametable(curr_vram_addr);

    // For any position (col_X, row_Y) in a row major tile map:
    // flat_index = row_Y * width + col_X
    int width = 32; // one nametable is 32 tiles wide
    int tile_idx = tile_row * width + tile_col;

    int nametable_number = get_nametable_number(curr_vram_addr);

    // Nametables start at $2000 in PPU address space.
    // Each nametable block is 1 KB = 1024 bytes.
    Word nametable_base_addr = 0x2000 + nametable_number * 1024;

    // The nametable cell at this tile position does not store tile graphics
    // directly. Instead, it stores a tile index [0..255], which tells the PPU
    // which tile to fetch later from the selected background pattern table.
    Byte nxt_pttrn_tile_idx = ppu_internalBus_read(nametable_base_addr + tile_idx);

    background_render_state.next_pattern_tile_idx = nxt_pttrn_tile_idx;
}

void PPU::fetch_next_background_attribute_byte()
{
    // current_vram_addr is the single source of truth for the actively
    // rendered/fetched background position.
    Word curr_vram_addr = background_scroll_state.current_vram_addr;

    int nametable_number = get_nametable_number(curr_vram_addr);
    Word nametable_base_addr = 0x2000 + nametable_number * 1024;

    int tile_row = get_tile_row_within_nametable(curr_vram_addr);
    int tile_col = get_tile_col_within_nametable(curr_vram_addr);

    int attr_row = tile_row / 4; // which 4x4 attribute region
    int attr_col = tile_col / 4;

    // The attribute table is an 8x8 grid of bytes (64 bytes total).
    // Each attribute byte controls palette selection for one 4x4 tile region,
    // so here we determine which of those 64 attribute regions this tile belongs to.
    int attr_flat_idx = attr_row * 8 + attr_col; // row major formula

    Byte attribute = ppu_internalBus_read(nametable_base_addr
                                          + 960 + attr_flat_idx);

    background_render_state.next_attr_byte = attribute;
}

void PPU::fetch_next_background_pattern_tile_low_byte()
{
    // current_vram_addr is the single source of truth for the actively
    // rendered/fetched background position.
    Word curr_vram_addr = background_scroll_state.current_vram_addr;

    // which pixel row inside the current 8x8 pattern tile we are on
    int fine_y_scroll = get_fine_y_scroll(curr_vram_addr);

    // we already set up this one in previous fetches so we can just use it
    Byte next_pattern_tile_idx = background_render_state.next_pattern_tile_idx;

    // Each tile is 16 bytes and represents an 8x8 pixel image.
    Byte tile_byte_lo = ppu_internalBus_read(ppuctrlFields.background_pattern_table_base
                                            + 16 * next_pattern_tile_idx
                                            + fine_y_scroll);

    background_render_state.next_tile_pattern_lo = tile_byte_lo;
}

void PPU::fetch_next_background_pattern_tile_high_byte()
{
    // current_vram_addr is the single source of truth for the actively
    // rendered/fetched background position.
    Word curr_vram_addr = background_scroll_state.current_vram_addr;

    int fine_y_scroll = get_fine_y_scroll(curr_vram_addr);

    Byte next_pattern_tile_idx = background_render_state.next_pattern_tile_idx;

    Byte tile_byte_hi = ppu_internalBus_read(ppuctrlFields.background_pattern_table_base
                                            + 16 * next_pattern_tile_idx
                                            + 8 + fine_y_scroll);

    background_render_state.next_tile_pattern_hi = tile_byte_hi;
}

// NOTE:
// This function assumes the low 8 bits are already cleared/zero
// by a previous register shift
void PPU::load_background_shift_registers()
{
    // References so names are not super long
    uint16_t& pattern_shift_lo = background_render_state.pattern_shift_lo;
    uint16_t& pattern_shift_hi = background_render_state.pattern_shift_hi;
    uint16_t& attr_shift_lo = background_render_state.attr_shift_lo;
    uint16_t& attr_shift_hi = background_render_state.attr_shift_hi;

    Byte next_tile_pattern_lo = background_render_state.next_tile_pattern_lo;
    Byte next_tile_pattern_hi = background_render_state.next_tile_pattern_hi;
    Byte next_attr_byte = background_render_state.next_attr_byte;

    // current_vram_addr is the single source of truth for the actively
    // rendered/fetched background position.
    Word current_vram_addr = background_scroll_state.current_vram_addr;

    int tile_col = get_tile_col_within_nametable(current_vram_addr);
    int tile_row = get_tile_row_within_nametable(current_vram_addr);

    // Load the next tile row's pattern bytes into the low 8 bits of the
    // pattern shift registers. The upper 8 bits are the currently active
    // bits being shifted out; the lower 8 bits become the upcoming tile data.
    pattern_shift_lo |= uint16_t(next_tile_pattern_lo);
    pattern_shift_hi |= uint16_t(next_tile_pattern_hi);

    // 30x32 tiles the NES screen (960 tiles)
    // attribute table palette selection depends on the tile's position inside
    // the nametable itself, not on where that tile currently appears on the
    // visible screen.
    int quadrant_row = (tile_row % 4) / 2; // finds 2x2 tile quadrant
    int quadrant_col = (tile_col % 4) / 2; // within 4x4 region

    uint8_t palette_number = 0; // [0-3] palette possibles, each 4 bytes long, each byte is a color code
    if(quadrant_row == 0 && quadrant_col == 0){ // (0,0) = top-left (bits 0-1)
        palette_number = (next_attr_byte & 0b00000011);
    }
    else if(quadrant_row == 0 && quadrant_col == 1){ // (0,1) = top-right (2-3)
        palette_number = ((next_attr_byte & 0b00001100) >> 2);
    }
    else if(quadrant_row == 1 && quadrant_col == 0){ // (1,0) = bottom-left (bits 4-5)
        palette_number = ((next_attr_byte & 0b00110000) >> 4);
    }
    else{ // (1,1) = bottom-right (bits 6-7)
        palette_number = ((next_attr_byte & 0b11000000) >> 6);
    }

    // The selected attribute bits apply to a 2x2 tile region, so horizontally
    // the same palette number stays valid for 16 pixels (2 tiles * 8 pixels).
    //
    // However, the background fetch/reload pipeline works one tile at a time,
    // meaning 8 pixels at a time. So here we only expand and load the next 8
    // repeated palette bits, not the full 16 pixel span.
    uint8_t next_8_bits_lo = (palette_number & uint8_t(0b01))
                                    ? 0b11111111
                                    : 0b00000000;
    uint8_t next_8_bits_hi = (palette_number & uint8_t(0b10))
                                    ? 0b11111111
                                    : 0b00000000;

    attr_shift_lo |= uint16_t(next_8_bits_lo);
    attr_shift_hi |= uint16_t(next_8_bits_hi);
}

void PPU::shift_background_registers()
{
    // Shift registers act like a queue of upcoming background pixel bits.
    //
    // The PPU fetches one tile row slice at a time:
    // - low pattern byte
    // - high pattern byte
    // - palette/attribute bits
    //
    // Then, each visible dot, it reads the current front bits to build one
    // background pixel and shifts the registers so the next pixel's bits
    // become the current ones.
    background_render_state.pattern_shift_lo <<= 1;
    background_render_state.pattern_shift_hi <<= 1;
    background_render_state.attr_shift_lo <<= 1;
    background_render_state.attr_shift_hi <<= 1;
}

BackgroundPixelSample PPU::sample_current_background_pixel_rgba()
{
    // references to keep names short
    uint16_t& pattern_shift_lo = background_render_state.pattern_shift_lo;
    uint16_t& pattern_shift_hi = background_render_state.pattern_shift_hi;
    uint16_t& attr_shift_lo = background_render_state.attr_shift_lo;
    uint16_t& attr_shift_hi = background_render_state.attr_shift_hi;

    // fine_x_scroll is the single source of truth for the active horizontal
    // pixel offset inside the current background tile stream.
    Byte fine_x = background_scroll_state.fine_x_scroll;

    BackgroundPixelSample sample;

    // =================================================================
    // IMPORTANT:
    // The current pixel is the bit at an offset determined by fine X
    // =================================================================
    bool pixel_lo = attr_shift_lo & Word(0x1 << (15 - fine_x));
    bool pixel_hi = attr_shift_hi & Word(0x1 << (15 - fine_x));

    unsigned int palette_number = // [0-3] one of the 4 possible palettes, note each has 4 possible entrances
                        ((pixel_hi ? 1 : 0) << 1) |
                        (pixel_lo ? 1 : 0);

    pixel_lo = pattern_shift_lo & Word(0x1 << (15 - fine_x));
    pixel_hi = pattern_shift_hi & Word(0x1 << (15 - fine_x));

    unsigned int palette_entry = // entrance within an already selected palette
                        ((pixel_hi ? 1 : 0) << 1) |
                        (pixel_lo ? 1 : 0);

    // palette_entry == 0 means background considered transparent for hit logic
    // otherwise background considered opaque for hit logic
    sample.is_opaque = palette_entry != 0;

    int base_palette_offset = palette_number * 4; // "steps" of 4 bytes (palette entrances)
    int palette_idx = base_palette_offset + palette_entry; // final idx from the start of the palette table

    Byte color_code = (palette_entry == 0)
                                        ? palette_mem[0]
                                        : palette_mem[palette_idx];
    sample.rgba = nes_color_code_to_rgba32(color_code);

    return sample;
}

void PPU::collect_visible_sprite_indices_for_scanline(int scanline)
{
    overlapping_sprites_idx.clear();
    overlapping_sprites_idx.resize(64);

    int pixel_y = scanline;
    int counter = 0;

    int sprite_height = 8;
    if(ppuctrlFields.sprite_sz == SpriteSize::SZ_8x8){
        sprite_height = 8;
    }
    else if(ppuctrlFields.sprite_sz == SpriteSize::SZ_8x16){
        sprite_height = 16;
    }

    for(int idx = 0; idx < 64; idx++){
        int base = idx * 4;

        // A sprite is 4 Bytes, Byte 0 is Y position
        // If scanline does not overlap a sprite box then continue
        if(!(pixel_y >= oam_mem[base] + 1 && pixel_y <= oam_mem[base] + sprite_height)){
            continue;
        }

        // The NES only keeps 8 sprites per scanline for rendering purposes.
        // PPU register PPUSTATUS bit 5 = sprite overflow (must be set if this happens)
        if(counter == 8){
            regs.ppustatus |= Byte(1 << 5);
            break;
        }

        overlapping_sprites_idx[counter++] = idx;
    }
    overlapping_sprites_idx.resize(counter);
}


SpritePixelSample PPU::sample_sprite_pixel(int sprite_index, int pixel_x, int pixel_y)
{
    // ===========================================================================================================
    // OAM (Object Attribute Memory) is basically a table of sprite entries.
    // Each sprite entry tells the PPU:
    // - where the sprite is on screen
    // - which tile graphic to use
    // - how to draw it
    //
    // NES has 64 sprites, 4 bytes each, thus 256 bytes total
    //
    // ---------------------------
    // The 4 bytes of one sprite |
    // ---------------------------
    // Byte 0: Y position (top of the sprite vertically)
    // Byte 1: Tile index (which pattern tile to use for the sprite graphic)
    // Byte 2: Attributes (sprite palette to use, horizontal/vertical flip, priority (in front/behind bckgrnd))
    // Byte 3: X position (left side of the sprite horizontally)
    //
    // For each visible pixel, the PPU ask:
    // Does this screen pixel lie inside that sprite’s 8x8 box?
    // If No : No need to do anything
    // If Yes:
    // -> Compute local sprite coordinates:
    //      - local x inside sprite: pixel_x - sprite_x
    //      - local y inside sprite: pixel_y - sprite_y
    // -> Fetch the correct row of the sprite’s tile pattern
    // -> Extract the correct bit pair for that local x
    // -> if the resulting sprite pixel value is nonzero, the sprite is visible at that pixel
    //
    // NOTE: For sprites, pixel value 0 means TRANSPARENT
    // - So even if a sprite covers an 8x8 box (or 8x16), not every pixel inside that box is actually visible.
    // That matters a lot for sprite 0 hit. Because sprite 0 hit happens only when:
    // - background pixel is non transparent
    // - sprite 0 pixel is non transparent
    // - both overlap at the current visible dot
    //
    // Sprite 0 specifically is just the first sprite in OAM
    // What is special is that the PPU exposes a flag:
    // PPUSTATUS bit 6
    // when sprite 0 overlaps a nontransparent background pixel.
    // Games use that as a timing signal.
    // ===========================================================================================================

    SpritePixelSample sprite_data{.sprite_index = sprite_index};

    int base = sprite_index * 4; // each sprite is 4 bytes long

    std::array<Byte, 4> sprite{};
    for(int i = 0; i < 4; i++){
        sprite[i] = oam_mem[base + i];
    }

    Byte sprite_x = sprite[3];
    Byte sprite_y = sprite[0];
    sprite_data.sprite_x = sprite_x;
    sprite_data.sprite_y = sprite_y;

    sprite_data.attr_byte = sprite[2];

    bool horizontal_flip = sprite_data.attr_byte & 0b01000000;
    bool vertical_flip   = sprite_data.attr_byte & 0b10000000;

    // bit 5: priority relative to background (0 sprite in front, 1 behind bckgrnd)
    sprite_data.behind_background = sprite_data.attr_byte & 0b00100000;

    int sprite_height = 8;
    if(ppuctrlFields.sprite_sz == SpriteSize::SZ_8x8){
        sprite_height = 8;
    }
    else if(ppuctrlFields.sprite_sz == SpriteSize::SZ_8x16){
        sprite_height = 16;
    }

    // NES special behavior:
    // For sprite OAM byte 0, the sprite is effectively
    // shown starting one scanline below that stored Y value
    if((pixel_x >= sprite_x && pixel_x <= sprite_x + 7)
        && (pixel_y >= sprite_y + 1 && pixel_y <= sprite_y + sprite_height)){

        sprite_data.local_x = pixel_x - sprite_x;
        sprite_data.local_y = pixel_y - (sprite_y + 1);

        if(horizontal_flip){
            sprite_data.local_x = 7 - sprite_data.local_x;
        }

        if(vertical_flip){
            sprite_data.local_y = (sprite_height - 1) - sprite_data.local_y;
        }

        int tile_pttrn_idx = sprite[1];

        // We find the address based on 8x8 or 8x16 sprite configuration used
        Word row_addr = resolve_sprite_pattern_row_address(tile_pttrn_idx, sprite_data.local_y);

        // Fetch the sprite pattern row for local_y
        Byte row_byte_lo = ppu_internalBus_read(row_addr);

        Byte row_byte_hi = ppu_internalBus_read(row_addr + 8);

        // Extract the bit at local_x (giving us the exact bit at this pixel
        // inside the sprite (local position))
        unsigned int bit_lo = (row_byte_lo & Byte(1 << (7 - sprite_data.local_x)))
                            ? 1
                            : 0;
        unsigned int bit_hi = (row_byte_hi & Byte(1 << (7 - sprite_data.local_x)))
                            ? 1
                            : 0;

        // entrance within an already selected palette
        // this tho tells us (if value is 0) that pixel
        // is TRANSPARENT, else if 1,2,3 VISIBLE
        unsigned int palette_entrance = (bit_hi << 1)
                                        | bit_lo;

        sprite_data.palette_entry = Byte(palette_entrance);

        if(palette_entrance != 0){
            sprite_data.is_opaque = true;

            // =============================
            // Attribute Byte for Sprites   |
            // =============================
            // bits 0-1: which sprite palette to use
            // bit 5: priority relative to background (0 sprite in front, 1 behind bckgrnd)
            // bit 6: horizontal flip (If bit set, the sprite is mirrored left right)
            // bit 7: vertical flip (If bit is set, the sprite is mirrored top bottom)

            int bit_lo = sprite_data.attr_byte & Byte(1);
            int bit_hi = (sprite_data.attr_byte & Byte(1 << 1)) >> 1;

            int palette_number = (bit_hi << 1) | bit_lo;
            int base_palette_offset = palette_number * 4; // each palette is 4 byte long
            int palette_idx = base_palette_offset + sprite_data.palette_entry;

            // 4 first palette are background palettes,
            // so we need the 4 last ones (sprites palettes)
            int sprite_palette_offset = 4 * 4;
            palette_idx += sprite_palette_offset;

            Byte color_code = palette_mem[palette_idx];
            sprite_data.sprite_rgba = nes_color_code_to_rgba32(color_code);
        }
    }

    return sprite_data;
}

Word PPU::resolve_sprite_pattern_row_address(int tile_pttrn_idx, int local_y)
{
    Word row_addr = 0;

    // The NES uses PPUCTRL bit 5 to choose sprite size:
    switch(ppuctrlFields.sprite_sz)
    {
    case SpriteSize::SZ_8x8:

        // 16 Bytes per tile pattern
        // + local_y for the exact pixel row we need
        row_addr = (ppuctrlFields.sprite_pattern_table_base
                    + 16 * tile_pttrn_idx + local_y);
        return row_addr;

    case SpriteSize::SZ_8x16:
        // ====================================================================
        // Two stacked 8x8 tiles where:
        // top half rows 0..7 local_y
        // bottom half rows 8..15 local_y
        //
        // Which row inside that 8x8 half?
        // top half: row = local_y
        // bottom half: row = local_y - 8
        //
        // --------------------------------------------------
        // Which tile number and which pattern table bank?  |
        // --------------------------------------------------
        // In 8x16 mode, the sprite uses two consecutive tiles.
        // - top half (first tile)
        // - bot half (next tile)
        //
        // In 8x16 mode, the bank is not taken from ppuctrlFields.sprite_pattern_table_base
        // the same way as 8x8 mode. Instead, the low bit of the sprite tile index selects
        // the pattern table bank.
        // - if tile index bit 0 is 0 -> bank base = $0000
        // - if tile index bit 0 is 1 -> bank base = $1000
        //
        // Then:
        // top half uses pair_base_tile
        // bottom half uses pair_base_tile + 1
        // ====================================================================
        int row_in_tile = (local_y < 8) ? local_y : local_y - 8;

        // In 8x16 mode, the sprite is built from an EVEN/ODD tile pair.
        // So first we find the even tile that starts the pair.
        int pair_base_tile = tile_pttrn_idx & ~1;

        // Then we choose top or bottom half of that 16 pixel tall sprite.
        int tile_for_this_half = (local_y < 8)
                                    ? pair_base_tile
                                    : pair_base_tile + 1;

        // Bit 0 of the ORIGINAL tile index chooses the pattern table bank.
        int bank_base = (tile_pttrn_idx & 1) ? 0x1000 : 0x0000;

        // Final address of the LOW pattern row byte for this sprite row.
        row_addr = bank_base + tile_for_this_half * 16 + row_in_tile;

        return row_addr;
    }
    return 0;
}

void PPU::apply_left_edge_render_mask(int pixel_x,
                                      BackgroundPixelSample& bg_sample,
                                      SpritePixelSample& sprite0_data,
                                      SpritePixelSample& chosen_sprite)
{
    if(pixel_x >= 8){
        return;
    }

    // If background rendering in the leftmost 8 pixels is disabled,
    // then background must behave as hidden there.
    if(!ppumaskFields.show_background_leftmost_8_pixels){
        bg_sample.is_opaque = false;
    }

    // If sprite rendering in the leftmost 8 pixels is disabled,
    // then sprites must behave as hidden there.
    if(!ppumaskFields.show_sprites_leftmost_8_pixels){
        sprite0_data.is_opaque = false;
        chosen_sprite.is_opaque = false;
    }
}

bool PPU::can_set_sprite_zero_hit_for_current_dot(
    int pixel_x,
    const BackgroundPixelSample& bg_sample,
    const SpritePixelSample& sprite0_data,
    const SpritePixelSample& chosen_sprite) const
{
    // Sprite 0 hit only matters on visible dots, for visible pixels.
    if(current_scanline < 0 || current_scanline >= 240){
        return false;
    }

    if(current_dot < 1 || current_dot > 256){
        return false;
    }

    // Both background and sprite 0 must actually exist at this dot.
    if(!bg_sample.is_opaque || !sprite0_data.is_opaque){
        return false;
    }

    // Sprite 0 must be the actual chosen sprite pixel for this dot,
    // not just an overlapping opaque sprite 0 sample in isolation.
    if(!chosen_sprite.is_opaque){
        return false;
    }

    if(chosen_sprite.sprite_index != 0){
        return false;
    }

    // Sprite 0 hit is suppressed in the leftmost 8 pixels if background
    // or sprite rendering there is masked off by PPUMASK.
    if(pixel_x < 8){
        if(!ppumaskFields.show_background_leftmost_8_pixels){
            return false;
        }

        if(!ppumaskFields.show_sprites_leftmost_8_pixels){
            return false;
        }
    }

    return true;
}

void PPU::update_sprite_zero_hit_flag(int pixel_x,
                                      const BackgroundPixelSample& bg_sample,
                                      const SpritePixelSample& sprite0_data,
                                      const SpritePixelSample& chosen_sprite)
{
    // Sprite 0 hit happens only when:
    // - background pixel is non transparent
    // - sprite 0 pixel is non transparent
    // - sprite 0 is the actual chosen sprite at the current visible dot
    if(can_set_sprite_zero_hit_for_current_dot(
            pixel_x, bg_sample, sprite0_data, chosen_sprite)){
        regs.ppustatus |= Byte(1 << 6);
    }
}

std::uint32_t PPU::composite_background_and_sprite(
    const BackgroundPixelSample& bg_sample,
    const SpritePixelSample& sprite_data)
{
    // BackgroundPixelSample is the single source of truth for the
    // current background pixel state at this dot.
    //
    // SpritePixelSample is the single source of truth for the
    // chosen sprite pixel state at this dot.
    if(!sprite_data.is_opaque){
        return bg_sample.rgba;
    }

    if(!bg_sample.is_opaque){
        return sprite_data.sprite_rgba;
    }

    if(sprite_data.behind_background){
        return bg_sample.rgba;
    }

    return sprite_data.sprite_rgba;
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
    // The NES has 2 pattern tables, 4 KB each. (usually background and foreground)
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
    // IMPORTANT:
    // Once one of the 4 possible palette_tables is selected by the
    // attribute_table attribute byte, this values tell us which
    // entrance/offset of this palete to select. It gets confusing sometimes
    // cuz both palette selection (attribute_table) and entrance selection
    // are [0-3] range (Cuz 4 paletes with 4 entrances each)
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

                    unsigned int palette_entrance =
                        ((pixel_hi ? 1 : 0) << 1) |
                        (pixel_lo ? 1 : 0);

                    std::uint32_t rgba = DEBUG_GRAYSCALE_RGBA[palette_entrance];

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
    //
    // ---------------------------------------------------
    // Attribute Table (last 64 bytes of a 1KB nametable) |
    // ---------------------------------------------------
    // If every single tile in a nametable (all 32 * 30 = 960 tiles in the screen)
    // had its own full palette selector byte that would cost more memory.
    //
    // Nintendo instead grouped tiles into regions. So one attribute
    // byte controls the palette selection for a bigger block of the screen,
    // not just one tile.
    //
    // One attribute byte covers a 4x4 tile area. Since each tile is 8x8 pixels,
    // that means 4 * 8 = 32 ( 32x32 pixel background region)
    //
    // But inside that 4x4 tile region (16 tiles), it is split into 4 quadrants
    // that are 2x2 tiles each. And each quadrant gets one of the 4
    // background palettes.
    // Note: 16 tiles * 64 bytes (attr table) = 1024 > 960 visible tiles in screen
    //
    // So:
    // - one attribute byte
    // - covers 4 quadrants (2x2 tiles each)
    // - each quadrant choose palette
    //
    // --------------------------------------------------
    // HOW THE BITS ARE ARRANGED IN ONE ATTRIBUTE BYTE  |
    //---------------------------------------------------
    // An attribute byte is 8 bits total.
    // bits 0-1 = top-left quadrant
    // bits 2-3 = top-right quadrant
    // bits 4-5 = bottom-left quadrant
    // bits 6-7 = bottom-right quadrant
    //
    // -----------------------------------------
    // HOW YOU KNOW WHICH ATTRIBUTE BYTE TO USE |
    // -----------------------------------------
    // The background is 32 x 30 tiles.
    // The attribute table (64 bytes) is arranged as:
    // - 8 columns
    // - 8 rows
    //
    // because each attribute byte covers 4x4 tiles:
    // 32 / 4 = 8
    // 30 / 4 is effectively handled in the top part of the 8 rows layout
    //
    // So for a background tile at: (tile_col, tile_row)
    // The attribute table cell is:
    // -> attr_row = tile_row / 4   this finds 4x4 qudrant
    // -> attr_col = tile_col / 4
    //
    // For any pixel (x, y) in a Row Major image buffer:
    // index = y * width + x
    //
    // Then the attribute byte offset is:
    // flat_idx = attr_row * 8 + attr_col
    // Note:
    // if we extract attribute_table from nametable with
    // only the 64 last bytes this would be the index we need
    //
    // And the attribute table address is:
    // flat_idx = nametable_base + 0x3C0 + attr_row * 8 + attr_col
    // Note:
    // If we decide to use the whole nametable as one this would be the index
    //
    // -----------------------------------------------
    // HOW DO YOU KNOW WHAT QUADRANT INSIDE THAT BYTE |
    // -----------------------------------------------
    // Inside that 4x4 tile region:
    // tile_row % 4 tells local row inside the block
    // tile_col % 4 tells local col inside the block
    //
    // But since quadrants are 2x2, what really matters is:
    // quadrant_row = (tile_row % 4) / 2 this finds 2x2 qudrant
    // quadrant_col = (tile_col % 4) / 2
    //
    // So:
    // (0,0) = top-left
    // (0,1) = top-right
    // (1,0) = bottom-left
    // (1,1) = bottom-right
    //
    // IMPORTANT:
    // This select which one of the 4 possible palettes to use, this is
    // the palette selector, then the [0-3] bytes from the nametable shape
    // information (bitmaps) provides which entrance [0-3] of this palette
    // we select (confusing cuz both [0-3] range)
    //
    // Then we choose the correct 2 bit pair from the attribute byte.
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

            int tile_start_x = tile_col * tile_pixel_size;
            int tile_start_y = tile_row * tile_pixel_size;

            // draw tile on image pixel buffer
            for(int row = 0; row < 8; row++){
                Byte row_byte_lo = tile[row];
                Byte row_byte_hi = tile[row + 8];

                for(int col = 0; col < 8; col++){
                    // 0 means “leftmost pixel” (first (left to right) pixel in row)
                    bool pixel_lo = row_byte_lo & (0x1 << (7 - col));
                    bool pixel_hi = row_byte_hi & (0x1 << (7 - col));

                    unsigned int palette_entrance =
                        ((pixel_hi ? 1 : 0) << 1) |
                        (pixel_lo ? 1 : 0);

                    std::uint32_t rgba = DEBUG_GRAYSCALE_RGBA[palette_entrance];

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

void PPU::render_current_static_nametable_background()
{
    screen_pixels.assign(256 * 240, 0); // Clear the pixel image each frame

    std::array<Byte, 960> nameTable_tiles_indices{};
    std::array<Byte, 64> attribute_table{};
    for(int chunk = 0; chunk < 1024; chunk++){
        Word addr = ppuctrlFields.base_nameTable_addr + chunk;
        if(chunk < 960){
            nameTable_tiles_indices[chunk] = ppu_internalBus_read(addr);
        }else{
            attribute_table[chunk - 960] = ppu_internalBus_read(addr);
        }
    }

    constexpr int tiles_per_row = 32;
    constexpr int tile_pixel_size = 8;

    int flat_idx = 0;
    for(int tile_row = 0; tile_row < 30; tile_row++){
        for(int tile_col = 0; tile_col < 32; tile_col++){
            Byte tile_idx = nameTable_tiles_indices[flat_idx];

            std::array<Byte, 16> tile{}; // a tile in pattern_table is 16 bytes in size
            for(int byte = 0; byte < 16; byte++){
                tile[byte] = ppu_internalBus_read(
                                ppuctrlFields.background_pattern_table_base
                                + 16 * tile_idx + byte);
            }

            int tile_start_x = tile_col * tile_pixel_size;
            int tile_start_y = tile_row * tile_pixel_size;

            // draw tile on image pixel buffer
            for(int row = 0; row < 8; row++){
                Byte row_byte_lo = tile[row];
                Byte row_byte_hi = tile[row + 8];

                for(int col = 0; col < 8; col++){
                    // 0 means “leftmost pixel” (first (left to right) pixel in row)
                    bool pixel_lo = row_byte_lo & (0x1 << (7 - col));
                    bool pixel_hi = row_byte_hi & (0x1 << (7 - col));

                    unsigned int palette_entrance = // entrance within an already selected palette
                        ((pixel_hi ? 1 : 0) << 1) |
                        (pixel_lo ? 1 : 0);

                    // finding now wich palette out of the 4 possible ones
                    int attr_row = tile_row / 4;
                    int attr_col = tile_col / 4;

                    int attr_flat_idx = attr_row * 8 + attr_col; // row major formula
                    Byte attribute = attribute_table[attr_flat_idx];

                    int quadrant_row = (tile_row % 4) / 2; // finds 2x2 tile quadrant
                    int quadrant_col = (tile_col % 4) / 2; // within 4x4 region

                    unsigned int palette_number = 0;
                    if(quadrant_row == 0 && quadrant_col == 0){ // (0,0) = top-left (bits 0-1)
                        palette_number = (attribute & 0b00000011);
                    }
                    else if(quadrant_row == 0 && quadrant_col == 1){ // (0,1) = top-right (2-3)
                        palette_number = ((attribute & 0b00001100) >> 2);
                    }
                    else if(quadrant_row == 1 && quadrant_col == 0){ // (1,0) = bottom-left (bits 4-5)
                        palette_number = ((attribute & 0b00110000) >> 4);
                    }
                    else{ // (1,1) = bottom-right (bits 6-7)
                        palette_number = ((attribute & 0b11000000) >> 6);
                    }

                    // Each background palette occupies 4 consecutive entries in palette memory,
                    // so once we know which palette number [0..3] was selected by the attribute
                    // byte, we multiply by 4 to jump to the first entry of that palette.
                    //
                    // Example:
                    // palette_number = 2  ->  this is the third background palette (0 indexed)
                    // base_palette_offset = 2 * 4 = 8
                    //
                    // So palette 2 begins at offset 8 inside the background palette region.
                    int base_palette_offset = palette_number * 4;

                    // once we have a palette the palette_entrance within it tells us wich one
                    // of the 4 possible codes inside that palette is the right one (confusing
                    // cuz both 4 so it gets confusing fast)
                    int palette_idx = base_palette_offset + palette_entrance;

                    int x = tile_start_x + col;
                    int y = tile_start_y + row;

                    // special NES behavior when palete entrance is 0:
                    // -> PPU uses universal background color: (palette_mem[0]
                    Byte color_code = (palette_entrance == 0)
                                        ? palette_mem[0]
                                        : palette_mem[palette_idx];
                    std::uint32_t rgba = nes_color_code_to_rgba32(color_code);

                    screen_pixels[y * 256 + x] = rgba;
                }
            }

            flat_idx++;
        }
    }
}