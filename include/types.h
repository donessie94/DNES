#pragma once
#include <cstdint>
#include <array>
#include <vector>
#include <string>

using Byte = uint8_t;
using Word = uint16_t;

enum class Mnemonic {
    ADC, AND, ASL, BCC, BCS, BEQ, BIT, BMI, BNE, BPL,
    BRK, BVC, BVS, CLC, CLD, CLI, CLV, CMP, CPX, CPY,
    DEC, DEX, DEY, EOR, INC, INX, INY, JMP, JSR, LDA,
    LDX, LDY, LSR, NOP, ORA, PHA, PHP, PLA, PLP, ROL,
    ROR, RTI, RTS, SBC, SEC, SED, SEI, STA, STX, STY,
    TAX, TAY, TSX, TXA, TXS, TYA, COUNT
};

enum class AddrMode {
    Implied,
    Accumulator,
    Immediate,
    ZeroPage,
    ZeroPageX,
    ZeroPageY,
    Absolute,
    AbsoluteX,
    AbsoluteY,
    Indirect,
    IndexedIndirect, // (zp,X)
    IndirectIndexed, // (zp),Y
    Relative
};

enum class MapperType : Byte {
    NROM  = 0,
    MMC1  = 1,
    UxROM = 2,
    CNROM = 3,
    MMC3  = 4
    // there are many more but for now i'll just support these ones
};

enum class CartridgeRegion : Byte {
    None = 0,
    PRG_ROM,
    PRG_RAM,
    CHR
};

enum class SpriteSize {
    SZ_8x8, SZ_8x16
};

enum class Toggler {
    FIRST = 0, SECOND = 1
};

enum class MirroringMode {
    HORIZONTAL, VERTICAL, FOUR_SCREENS
};

enum class NametableSource {
    PPU_RAM,
    CARTRIDGE_FOUR_SCREEN_RAM
};

struct NametableMappingResult {
    NametableSource source{NametableSource::PPU_RAM};
    Word offset{};
};

struct Instr {
    Mnemonic mnemonic;
    AddrMode mode;
    Byte bytes;
    Byte cycles;
    bool implemented;
    bool add_cycle_if_page_cross;
    bool add_cycle_if_branch_taken;
};

struct OperandResult {
    Byte value{};
    Word address{};
    bool page_crossed{};
    bool branch_taken{};
};

struct INesHeader
{
    inline bool set_from_data(const std::vector<Byte>& raw_rom_data)
    {
        if (raw_rom_data.size() < 16) {
            return false;
        }

        for (int i = 0; i < 4; i++) {
            magic[i] = raw_rom_data[i];
        }

        prg_rom_banks = raw_rom_data[4];
        chr_rom_banks = raw_rom_data[5];
        flags6 = raw_rom_data[6];

        if(flags6 & 0b00001000) { mirroring_mode = MirroringMode::FOUR_SCREENS; }
        else { mirroring_mode = flags6 & 0b00000001
                                ? MirroringMode::HORIZONTAL
                                : MirroringMode::VERTICAL;}

        flags7 = raw_rom_data[7];
        prg_ram_banks = raw_rom_data[8];
        flags9 = raw_rom_data[9];
        flags10 = raw_rom_data[10];

        for (int i = 0; i < 5; i++) {
            padding[i] = raw_rom_data[11 + i];
        }

        return true;
    }

    Byte magic[4]{};              // "NES\x1A" (signature)
    Byte prg_rom_banks{};         // byte 4, number of 16 KB PRG "chunks"
    Byte chr_rom_banks{};         // byte 5, in 8 KB "chunks" (0 means CHR RAM)
    Byte flags6{};                // byte 6

    // nametable mirroring mode: flags6 bit 0 = horizontal/vertical, bit 3 = four screen override
    MirroringMode mirroring_mode{};

    Byte flags7{};                // byte 7
    Byte prg_ram_banks{};         // byte 8, in 8 KB "chunks"
    Byte flags9{};                // byte 9
    Byte flags10{};               // byte 10
    Byte padding[5]{};            // bytes 11-15

};

struct TranslationResult {
    bool success{};
    CartridgeRegion region{CartridgeRegion::None};
    Word translated_offset{};
};

struct PPURegisters {
    Byte ppuctrl{};    // $2000
    Byte ppumask{};    // $2001
    Byte ppustatus{};  // $2002
    Byte oamaddr{};    // $2003
};

struct PPUCTRLDecoded {
    Word base_nameTable_addr{};          // bits 0-1: selected base nametable ($2000/$2400/$2800/$2C00)
    bool scroll_x_bit_8{};               // comes from PPUCTRL bit 0
    bool scroll_y_bit_8{};               // comes from PPUCTRL bit 1
    Byte vram_increment_mode{};          // bit 2: how much PPUADDR increments after PPUDATA access (1 or 32)
    Word sprite_pattern_table_base{};    // bit 3: sprite pattern table base for 8x8 sprites ($0000 or $1000)
    Word background_pattern_table_base{};// bit 4: background pattern table base ($0000 or $1000)
    SpriteSize sprite_sz{};              // bit 5: sprite size mode (8x8 or 8x16)
    bool master_slave{};                 // bit 6: master/slave select (normally ignored on standard NES)
    bool vblank_NMI_allowed{};           // bit 7: allow NMI when vblank starts
};

struct PPUMASKDecoded {
    bool greyscale{};                   // bit 0: render in greyscale
    bool show_background_left_8px{};    // bit 1: show background in leftmost 8 pixels
    bool show_sprites_left_8px{};       // bit 2: show sprites in leftmost 8 pixels
    bool background_rendering_enabled{};// bit 3: enable background rendering
    bool sprite_rendering_enabled{};    // bit 4: enable sprite rendering
    bool emphasize_red{};               // bit 5: color emphasis red
    bool emphasize_green{};             // bit 6: color emphasis green
    bool emphasize_blue{};              // bit 7: color emphasis blue
};

struct DebugImage {
    int width{};
    int height{};
    std::vector<std::uint32_t> pixels{};
};

static std::array<Instr, 256> build_official_instr_table()
{
    std::array<Instr, 256> table{};

    // Default every opcode to "not implemented yet".
    for (auto& e : table) {
        e = {Mnemonic::NOP, AddrMode::Implied, 1, 0, false, false, false};
    }

    auto set = [&](Byte opcode,
                   Mnemonic m,
                   AddrMode mode,
                   Byte bytes,
                   Byte cycles,
                   bool add_cycle_if_page_cross = false,
                   bool add_cycle_if_branch_taken = false) {
        table[opcode] = {
            m,
            mode,
            bytes,
            cycles,
            true,
            add_cycle_if_page_cross,
            add_cycle_if_branch_taken
        };
    };

    // ADC
    set(0x69, Mnemonic::ADC, AddrMode::Immediate,       2, 2);
    set(0x65, Mnemonic::ADC, AddrMode::ZeroPage,        2, 3);
    set(0x75, Mnemonic::ADC, AddrMode::ZeroPageX,       2, 4);
    set(0x6D, Mnemonic::ADC, AddrMode::Absolute,        3, 4);
    set(0x7D, Mnemonic::ADC, AddrMode::AbsoluteX,       3, 4, true);
    set(0x79, Mnemonic::ADC, AddrMode::AbsoluteY,       3, 4, true);
    set(0x61, Mnemonic::ADC, AddrMode::IndexedIndirect, 2, 6);
    set(0x71, Mnemonic::ADC, AddrMode::IndirectIndexed, 2, 5, true);

    // AND
    set(0x29, Mnemonic::AND, AddrMode::Immediate,       2, 2);
    set(0x25, Mnemonic::AND, AddrMode::ZeroPage,        2, 3);
    set(0x35, Mnemonic::AND, AddrMode::ZeroPageX,       2, 4);
    set(0x2D, Mnemonic::AND, AddrMode::Absolute,        3, 4);
    set(0x3D, Mnemonic::AND, AddrMode::AbsoluteX,       3, 4, true);
    set(0x39, Mnemonic::AND, AddrMode::AbsoluteY,       3, 4, true);
    set(0x21, Mnemonic::AND, AddrMode::IndexedIndirect, 2, 6);
    set(0x31, Mnemonic::AND, AddrMode::IndirectIndexed, 2, 5, true);

    // ASL
    set(0x0A, Mnemonic::ASL, AddrMode::Accumulator,     1, 2);
    set(0x06, Mnemonic::ASL, AddrMode::ZeroPage,        2, 5);
    set(0x16, Mnemonic::ASL, AddrMode::ZeroPageX,       2, 6);
    set(0x0E, Mnemonic::ASL, AddrMode::Absolute,        3, 6);
    set(0x1E, Mnemonic::ASL, AddrMode::AbsoluteX,       3, 7);

    // Branches
    set(0x90, Mnemonic::BCC, AddrMode::Relative,        2, 2, false, true);
    set(0xB0, Mnemonic::BCS, AddrMode::Relative,        2, 2, false, true);
    set(0xF0, Mnemonic::BEQ, AddrMode::Relative,        2, 2, false, true);
    set(0x30, Mnemonic::BMI, AddrMode::Relative,        2, 2, false, true);
    set(0xD0, Mnemonic::BNE, AddrMode::Relative,        2, 2, false, true);
    set(0x10, Mnemonic::BPL, AddrMode::Relative,        2, 2, false, true);
    set(0x50, Mnemonic::BVC, AddrMode::Relative,        2, 2, false, true);
    set(0x70, Mnemonic::BVS, AddrMode::Relative,        2, 2, false, true);

    // BIT
    set(0x24, Mnemonic::BIT, AddrMode::ZeroPage,        2, 3);
    set(0x2C, Mnemonic::BIT, AddrMode::Absolute,        3, 4);

    // BRK
    set(0x00, Mnemonic::BRK, AddrMode::Implied,         1, 7);

    // Flag clear/set
    set(0x18, Mnemonic::CLC, AddrMode::Implied,         1, 2);
    set(0xD8, Mnemonic::CLD, AddrMode::Implied,         1, 2);
    set(0x58, Mnemonic::CLI, AddrMode::Implied,         1, 2);
    set(0xB8, Mnemonic::CLV, AddrMode::Implied,         1, 2);

    set(0x38, Mnemonic::SEC, AddrMode::Implied,         1, 2);
    set(0xF8, Mnemonic::SED, AddrMode::Implied,         1, 2);
    set(0x78, Mnemonic::SEI, AddrMode::Implied,         1, 2);

    // CMP
    set(0xC9, Mnemonic::CMP, AddrMode::Immediate,       2, 2);
    set(0xC5, Mnemonic::CMP, AddrMode::ZeroPage,        2, 3);
    set(0xD5, Mnemonic::CMP, AddrMode::ZeroPageX,       2, 4);
    set(0xCD, Mnemonic::CMP, AddrMode::Absolute,        3, 4);
    set(0xDD, Mnemonic::CMP, AddrMode::AbsoluteX,       3, 4, true);
    set(0xD9, Mnemonic::CMP, AddrMode::AbsoluteY,       3, 4, true);
    set(0xC1, Mnemonic::CMP, AddrMode::IndexedIndirect, 2, 6);
    set(0xD1, Mnemonic::CMP, AddrMode::IndirectIndexed, 2, 5, true);

    // CPX
    set(0xE0, Mnemonic::CPX, AddrMode::Immediate,       2, 2);
    set(0xE4, Mnemonic::CPX, AddrMode::ZeroPage,        2, 3);
    set(0xEC, Mnemonic::CPX, AddrMode::Absolute,        3, 4);

    // CPY
    set(0xC0, Mnemonic::CPY, AddrMode::Immediate,       2, 2);
    set(0xC4, Mnemonic::CPY, AddrMode::ZeroPage,        2, 3);
    set(0xCC, Mnemonic::CPY, AddrMode::Absolute,        3, 4);

    // DEC
    set(0xC6, Mnemonic::DEC, AddrMode::ZeroPage,        2, 5);
    set(0xD6, Mnemonic::DEC, AddrMode::ZeroPageX,       2, 6);
    set(0xCE, Mnemonic::DEC, AddrMode::Absolute,        3, 6);
    set(0xDE, Mnemonic::DEC, AddrMode::AbsoluteX,       3, 7);

    // DEX / DEY
    set(0xCA, Mnemonic::DEX, AddrMode::Implied,         1, 2);
    set(0x88, Mnemonic::DEY, AddrMode::Implied,         1, 2);

    // EOR
    set(0x49, Mnemonic::EOR, AddrMode::Immediate,       2, 2);
    set(0x45, Mnemonic::EOR, AddrMode::ZeroPage,        2, 3);
    set(0x55, Mnemonic::EOR, AddrMode::ZeroPageX,       2, 4);
    set(0x4D, Mnemonic::EOR, AddrMode::Absolute,        3, 4);
    set(0x5D, Mnemonic::EOR, AddrMode::AbsoluteX,       3, 4, true);
    set(0x59, Mnemonic::EOR, AddrMode::AbsoluteY,       3, 4, true);
    set(0x41, Mnemonic::EOR, AddrMode::IndexedIndirect, 2, 6);
    set(0x51, Mnemonic::EOR, AddrMode::IndirectIndexed, 2, 5, true);

    // INC
    set(0xE6, Mnemonic::INC, AddrMode::ZeroPage,        2, 5);
    set(0xF6, Mnemonic::INC, AddrMode::ZeroPageX,       2, 6);
    set(0xEE, Mnemonic::INC, AddrMode::Absolute,        3, 6);
    set(0xFE, Mnemonic::INC, AddrMode::AbsoluteX,       3, 7);

    // INX / INY
    set(0xE8, Mnemonic::INX, AddrMode::Implied,         1, 2);
    set(0xC8, Mnemonic::INY, AddrMode::Implied,         1, 2);

    // JMP / JSR
    set(0x4C, Mnemonic::JMP, AddrMode::Absolute,        3, 3);
    set(0x6C, Mnemonic::JMP, AddrMode::Indirect,        3, 5);
    set(0x20, Mnemonic::JSR, AddrMode::Absolute,        3, 6);

    // LDA
    set(0xA9, Mnemonic::LDA, AddrMode::Immediate,       2, 2);
    set(0xA5, Mnemonic::LDA, AddrMode::ZeroPage,        2, 3);
    set(0xB5, Mnemonic::LDA, AddrMode::ZeroPageX,       2, 4);
    set(0xAD, Mnemonic::LDA, AddrMode::Absolute,        3, 4);
    set(0xBD, Mnemonic::LDA, AddrMode::AbsoluteX,       3, 4, true);
    set(0xB9, Mnemonic::LDA, AddrMode::AbsoluteY,       3, 4, true);
    set(0xA1, Mnemonic::LDA, AddrMode::IndexedIndirect, 2, 6);
    set(0xB1, Mnemonic::LDA, AddrMode::IndirectIndexed, 2, 5, true);

    // LDX
    set(0xA2, Mnemonic::LDX, AddrMode::Immediate,       2, 2);
    set(0xA6, Mnemonic::LDX, AddrMode::ZeroPage,        2, 3);
    set(0xB6, Mnemonic::LDX, AddrMode::ZeroPageY,       2, 4);
    set(0xAE, Mnemonic::LDX, AddrMode::Absolute,        3, 4);
    set(0xBE, Mnemonic::LDX, AddrMode::AbsoluteY,       3, 4, true);

    // LDY
    set(0xA0, Mnemonic::LDY, AddrMode::Immediate,       2, 2);
    set(0xA4, Mnemonic::LDY, AddrMode::ZeroPage,        2, 3);
    set(0xB4, Mnemonic::LDY, AddrMode::ZeroPageX,       2, 4);
    set(0xAC, Mnemonic::LDY, AddrMode::Absolute,        3, 4);
    set(0xBC, Mnemonic::LDY, AddrMode::AbsoluteX,       3, 4, true);

    // LSR
    set(0x4A, Mnemonic::LSR, AddrMode::Accumulator,     1, 2);
    set(0x46, Mnemonic::LSR, AddrMode::ZeroPage,        2, 5);
    set(0x56, Mnemonic::LSR, AddrMode::ZeroPageX,       2, 6);
    set(0x4E, Mnemonic::LSR, AddrMode::Absolute,        3, 6);
    set(0x5E, Mnemonic::LSR, AddrMode::AbsoluteX,       3, 7);

    // NOP
    set(0xEA, Mnemonic::NOP, AddrMode::Implied,         1, 2);

    // ORA
    set(0x09, Mnemonic::ORA, AddrMode::Immediate,       2, 2);
    set(0x05, Mnemonic::ORA, AddrMode::ZeroPage,        2, 3);
    set(0x15, Mnemonic::ORA, AddrMode::ZeroPageX,       2, 4);
    set(0x0D, Mnemonic::ORA, AddrMode::Absolute,        3, 4);
    set(0x1D, Mnemonic::ORA, AddrMode::AbsoluteX,       3, 4, true);
    set(0x19, Mnemonic::ORA, AddrMode::AbsoluteY,       3, 4, true);
    set(0x01, Mnemonic::ORA, AddrMode::IndexedIndirect, 2, 6);
    set(0x11, Mnemonic::ORA, AddrMode::IndirectIndexed, 2, 5, true);

    // Stack ops
    set(0x48, Mnemonic::PHA, AddrMode::Implied,         1, 3);
    set(0x08, Mnemonic::PHP, AddrMode::Implied,         1, 3);
    set(0x68, Mnemonic::PLA, AddrMode::Implied,         1, 4);
    set(0x28, Mnemonic::PLP, AddrMode::Implied,         1, 4);

    // ROL
    set(0x2A, Mnemonic::ROL, AddrMode::Accumulator,     1, 2);
    set(0x26, Mnemonic::ROL, AddrMode::ZeroPage,        2, 5);
    set(0x36, Mnemonic::ROL, AddrMode::ZeroPageX,       2, 6);
    set(0x2E, Mnemonic::ROL, AddrMode::Absolute,        3, 6);
    set(0x3E, Mnemonic::ROL, AddrMode::AbsoluteX,       3, 7);

    // ROR
    set(0x6A, Mnemonic::ROR, AddrMode::Accumulator,     1, 2);
    set(0x66, Mnemonic::ROR, AddrMode::ZeroPage,        2, 5);
    set(0x76, Mnemonic::ROR, AddrMode::ZeroPageX,       2, 6);
    set(0x6E, Mnemonic::ROR, AddrMode::Absolute,        3, 6);
    set(0x7E, Mnemonic::ROR, AddrMode::AbsoluteX,       3, 7);

    // RTI / RTS
    set(0x40, Mnemonic::RTI, AddrMode::Implied,         1, 6);
    set(0x60, Mnemonic::RTS, AddrMode::Implied,         1, 6);

    // SBC
    set(0xE9, Mnemonic::SBC, AddrMode::Immediate,       2, 2);
    set(0xE5, Mnemonic::SBC, AddrMode::ZeroPage,        2, 3);
    set(0xF5, Mnemonic::SBC, AddrMode::ZeroPageX,       2, 4);
    set(0xED, Mnemonic::SBC, AddrMode::Absolute,        3, 4);
    set(0xFD, Mnemonic::SBC, AddrMode::AbsoluteX,       3, 4, true);
    set(0xF9, Mnemonic::SBC, AddrMode::AbsoluteY,       3, 4, true);
    set(0xE1, Mnemonic::SBC, AddrMode::IndexedIndirect, 2, 6);
    set(0xF1, Mnemonic::SBC, AddrMode::IndirectIndexed, 2, 5, true);

    // STA
    set(0x85, Mnemonic::STA, AddrMode::ZeroPage,        2, 3);
    set(0x95, Mnemonic::STA, AddrMode::ZeroPageX,       2, 4);
    set(0x8D, Mnemonic::STA, AddrMode::Absolute,        3, 4);
    set(0x9D, Mnemonic::STA, AddrMode::AbsoluteX,       3, 5);
    set(0x99, Mnemonic::STA, AddrMode::AbsoluteY,       3, 5);
    set(0x81, Mnemonic::STA, AddrMode::IndexedIndirect, 2, 6);
    set(0x91, Mnemonic::STA, AddrMode::IndirectIndexed, 2, 6);

    // STX
    set(0x86, Mnemonic::STX, AddrMode::ZeroPage,        2, 3);
    set(0x96, Mnemonic::STX, AddrMode::ZeroPageY,       2, 4);
    set(0x8E, Mnemonic::STX, AddrMode::Absolute,        3, 4);

    // STY
    set(0x84, Mnemonic::STY, AddrMode::ZeroPage,        2, 3);
    set(0x94, Mnemonic::STY, AddrMode::ZeroPageX,       2, 4);
    set(0x8C, Mnemonic::STY, AddrMode::Absolute,        3, 4);

    // Transfers
    set(0xAA, Mnemonic::TAX, AddrMode::Implied,         1, 2);
    set(0xA8, Mnemonic::TAY, AddrMode::Implied,         1, 2);
    set(0xBA, Mnemonic::TSX, AddrMode::Implied,         1, 2);
    set(0x8A, Mnemonic::TXA, AddrMode::Implied,         1, 2);
    set(0x9A, Mnemonic::TXS, AddrMode::Implied,         1, 2);
    set(0x98, Mnemonic::TYA, AddrMode::Implied,         1, 2);

    return table;
}

inline std::array<Instr, 256> instr_table = build_official_instr_table();