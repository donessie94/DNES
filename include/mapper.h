#pragma once
#include"types.h"

// shared interface, every different mapper must create its specific way to respond to it
class Mapper {
public:
    virtual ~Mapper() = default;

    virtual TranslationResult translate_cpu_read_addr(Word addr) = 0;
    virtual TranslationResult translate_cpu_write_addr(Word addr) = 0;
    virtual TranslationResult translate_ppu_read_addr(Word addr) = 0;
    virtual TranslationResult translate_ppu_write_addr(Word addr) = 0;
    Byte prg_rom_banks{};
	Byte chr_rom_banks{};
    bool chr_is_ram{};
};

class NROM : public Mapper {
public:
    TranslationResult translate_cpu_read_addr(Word addr) override;
    TranslationResult translate_cpu_write_addr(Word addr) override;
    TranslationResult translate_ppu_read_addr(Word addr) override;
    TranslationResult translate_ppu_write_addr(Word addr) override;
};