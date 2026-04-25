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

    void rebuild_temp_vram_addr_from_scroll_state();
    void copy_horizontal_scroll_bits_from_temp_to_current_vram_addr();
    void copy_vertical_scroll_bits_from_temp_to_current_vram_addr();
    void increment_horizontal_background_vram_addr();
    void increment_vertical_background_vram_addr();

    int get_tile_col_within_nametable(Word vram_addr);
    int get_tile_row_within_nametable(Word vram_addr);
    int get_nametable_col_select(Word vram_addr);
    int get_nametable_row_select(Word vram_addr);
    int get_nametable_number(Word vram_addr);
    int get_fine_y_scroll(Word vram_addr); // fine_x is stored separatedly

    void fetch_next_background_nametable_byte();
    void fetch_next_background_attribute_byte();
    void fetch_next_background_pattern_tile_low_byte();
    void fetch_next_background_pattern_tile_high_byte();

    void load_background_shift_registers();
    void shift_background_registers();
    BackgroundPixelSample sample_current_background_pixel_rgba();

    void collect_visible_sprite_indices_for_scanline(int scanline);
    std::vector<int> overlapping_sprites_idx{}; // collects once per scanline the overlapping idx of sprites

    SpritePixelSample sample_sprite_pixel(int sprite_index, int pixel_x, int pixel_y);
    Word resolve_sprite_pattern_row_address(int tile_pttrn_idx, int local_y);

    void apply_left_edge_render_mask(int pixel_x,
                                    BackgroundPixelSample& bg_sample,
                                    SpritePixelSample& sprite0_data,
                                    SpritePixelSample& chosen_sprite);

    bool can_set_sprite_zero_hit_for_current_dot(int pixel_x, const BackgroundPixelSample& bg_sample,
                                    const SpritePixelSample& sprite0_data,
                                    const SpritePixelSample& chosen_sprite) const;

    void update_sprite_zero_hit_flag(int pixel_x,
                                    const BackgroundPixelSample& bg_sample,
                                    const SpritePixelSample& sprite0_data,
                                    const SpritePixelSample& chosen_sprite);

    uint32_t composite_background_and_sprite(const BackgroundPixelSample& bg_sample,
                                                const SpritePixelSample& sprite_data);

    DebugImage build_pattern_table_debug_image();
    DebugImage build_palette_debug_image();
    DebugImage build_nametable_debug_image();

    void render_current_static_nametable_background();

    PPURegisters regs{};        // true simple PPU registers: control, mask, status, and OAM address
    int current_scanline{};
    int current_dot{};
    bool NMI_request{};

    // Shared first/second write toggle used by PPUSCROLL / PPUADDR.
    // This is the CPU facing write latch ("w") in the real PPU model.
    Toggler ppu_write_toggle{};
    Byte read_buffer{};

    Byte oam_dma{}; // last value written to $4014, used as the CPU memory page for sprite DMA

    PPUCTRLDecoded ppuctrlFields{};
    PPUMASKDecoded ppumaskFields{};

    std::array<Byte, 2048> nametable_mem{};
    std::array<Byte, 256>  oam_mem{};
    std::array<Byte, 32>   palette_mem{};

    Cartridge* cartridge_ref{};

    // uint32_t = 4 Bytes and represents a single pixel
    // Red, Green, Blue, Alpha (4 Bytes)
    FrameBuffer screen_pixels = FrameBuffer(256 * 240, 0); // initializing it

    // --------------------------------------
    // Background rendering pipeline state. |
    // --------------------------------------
    // The PPU fetches background data for the next tile, stores it in small
    // "next" byte latches/variables, and periodically loads that data into shift
    // registers. Then, during visible dots, one background pixel's bits are
    // shifted out each cycle.
    BackgroundRenderPipeline background_render_state{};

    // --------------------------------------
    // Background Scroll / Fetch State      |
    // --------------------------------------
    // The NES background is organized as a 32x30 tile grid (960 tile indices).
    //
    // Important:
    // attribute table palette selection depends on the tile's position inside
    // the nametable itself, not on where that tile currently appears on the
    // visible screen.
    // In other words, scrolling may change where a tile is shown on screen,
    // but the attribute table still interprets that tile using its original
    // nametable local tile row and tile column.
    //
    // These fields track where the background pipeline is currently fetching from
    // inside the scrolling nametable world.
    // They let the PPU know:
    // - which nametable is currently active
    // - which tile inside that nametable is being used
    // - and which fine pixel offset inside that tile should be rendered/fetched
    //
    // This is what allows scrolling to work correctly without relying only on
    // visible screen position.
    //
    // IMPORTANT:
    // This is now the single source of truth for the active/rendered address path
    // and the CPU facing scroll/address write path:
    // - current_vram_addr ("v")
    // - temp_vram_addr ("t")
    // - fine_x_scroll ("x")
    // - write_toggle ("w")
    //
    // Older parallel state like:
    // - ppu_addr
    // - full_scroll_x_pixels
    // - full_scroll_y_pixels
    // - ppu_addr_toggle
    // - ppu_scroll_toggle
    //
    // should not exist anymore because they create split brain bugs.
    BackgroundScrollState background_scroll_state{};

    bool frame_ready{};
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