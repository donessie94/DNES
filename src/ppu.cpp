#include "../include/ppu.h"
#include "../include/cartridge.h"

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
        if(result.source == NametableSource::PPU_RAM)
            nametable_mem[result.offset] = val;
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