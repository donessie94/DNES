#pragma once
#include"types.h"

class Cartridge;

class PPU {
public:
    PPU() = default;
    Byte cpu_read_register(Word addr);
    void cpu_write_register(Word addr, Byte val);

    Byte ppu_internalBus_read(Word addr);
    void ppu_internalBus_write(Word addr, Byte val);

    NametableMappingResult map_horizontal_nametable_addr(Word addr);
    NametableMappingResult map_vertical_nametable_addr(Word addr);
    NametableMappingResult map_four_screen_nametable_addr(Word addr);

    Byte cpu_read_ppuctrl();
    Byte cpu_read_ppumask();
    Byte cpu_read_ppustatus();
    Byte cpu_read_oamaddr();
    Byte cpu_read_oamdata();
    Byte cpu_read_ppuscroll();
    Byte cpu_read_ppuaddr();
    Byte cpu_read_ppudata();

    void cpu_write_ppuctrl(Byte val);
    void cpu_write_ppumask(Byte val);
    void cpu_write_ppustatus(Byte val);
    void cpu_write_oamaddr(Byte val);
    void cpu_write_oamdata(Byte val);
    void cpu_write_ppuscroll(Byte val);
    void cpu_write_ppuaddr(Byte val);
    void cpu_write_ppudata(Byte val);

    DebugImage build_pattern_table_debug_image();
    DebugImage build_palette_debug_image();
    DebugImage build_nametable_debug_image();

    PPURegisters regs{};        // true simple PPU registers: control, mask, status, and OAM address

    Word ppu_addr{};            // current 16-bit PPU memory address built through two writes to $2006
    Toggler ppu_addr_toggle{};  // next $2006 write is the first byte or second byte?
    Byte read_buffer{};

    Word ppu_scroll_x{};        // horizontal scroll info written through the first write to $2005
    Word ppu_scroll_y{};        // vertical scroll info written through the second write to $2005
    Toggler ppu_scroll_toggle{};// next $2005 write is X-scroll or Y-scroll?

    Byte oam_dma{}; // last value written to $4014, used as the CPU memory page for sprite DMA

    PPUCTRLDecoded ppuctrlFields{};
    PPUMASKDecoded ppumaskFields{};

    std::array<Byte, 2048> nametable_mem{};
    std::array<Byte, 256>  oam_mem{};
    std::array<Byte, 32>   palette_mem{};

    Cartridge* cartridge_ref{};
};

using CPURegisterReadHandler = Byte (PPU::*)();
inline static constexpr std::array<CPURegisterReadHandler, 8> cpu_register_read_handlers = {
    &PPU::cpu_read_ppuctrl,    // $2000
    &PPU::cpu_read_ppumask,    // $2001
    &PPU::cpu_read_ppustatus,  // $2002
    &PPU::cpu_read_oamaddr,    // $2003
    &PPU::cpu_read_oamdata,    // $2004
    &PPU::cpu_read_ppuscroll,  // $2005
    &PPU::cpu_read_ppuaddr,    // $2006
    &PPU::cpu_read_ppudata     // $2007
};

using CPURegisterWriteHandler = void (PPU::*)(Byte);
inline static constexpr std::array<CPURegisterWriteHandler, 8> cpu_register_write_handlers = {
    &PPU::cpu_write_ppuctrl,    // $2000
    &PPU::cpu_write_ppumask,    // $2001
    &PPU::cpu_write_ppustatus,  // $2002
    &PPU::cpu_write_oamaddr,    // $2003
    &PPU::cpu_write_oamdata,    // $2004
    &PPU::cpu_write_ppuscroll,  // $2005
    &PPU::cpu_write_ppuaddr,    // $2006
    &PPU::cpu_write_ppudata     // $2007
};

using NametableAddressMapHandler = NametableMappingResult (PPU::*)(Word);
inline static constexpr std::array<NametableAddressMapHandler, 3> nametable_address_map_handlers = {
    &PPU::map_horizontal_nametable_addr,   // MirroringMode::HORIZONTAL
    &PPU::map_vertical_nametable_addr,     // MirroringMode::VERTICAL
    &PPU::map_four_screen_nametable_addr   // MirroringMode::FOUR_SCREENS
};