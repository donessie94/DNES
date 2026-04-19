#pragma once
#include"types.h"

class Cartridge;

class PPU {
public:
    PPU() = default;
    void step_one_cycle();
    Byte cpu_read_register(Word addr);
    void cpu_write_register(Word addr, Byte val);

    Byte ppu_internalBus_read(Word addr);
    void ppu_internalBus_write(Word addr, Byte val);

    inline std::uint32_t nes_color_code_to_rgba32(std::uint8_t code)
    {
        const RGB rgb = NES_RGB_PALETTE[code & 0x3F];
        return (std::uint32_t(rgb[0]) << 24) |
            (std::uint32_t(rgb[1]) << 16) |
            (std::uint32_t(rgb[2]) << 8)  |
            0xFF;
    }

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
    int current_scanline{};
    int current_dot{};
    bool NMI_request{};

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

    // uint32_t = 4 Bytes and represents a single pixel
    // Red, Green, Blue, Alpha (4 Bytes)
    FrameBuffer screen_pixels{256 * 240, 0};
    FrameBuffer palette_debug_pixels{128 * 64, 0};
    FrameBuffer pattern_table_debug_pixels{256 * 128, 0};
    FrameBuffer nametable_debug_pixels{256 * 240, 0};
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