#pragma once
#include"types.h"
#include"mapper.h"
#include<fstream>
#include <memory>

class Cartridge {
public:
    Cartridge() = default;
    bool initialize_from_file(const std::string& path);
    Byte cpu_read(Word addr);
    void cpu_write(Word addr, Byte val);
    Byte ppu_read(Word addr);
    void ppu_write(Word addr, Byte val);
    inline MirroringMode get_mirroring_mode() { return INES_header.mirroring_mode; }

private:
    bool load_raw_file(const std::string& path);
    bool parse_header();
    bool load_prg_rom();
    void load_prg_ram();
    bool load_chr();
    bool initialize_mapper_from_header();
    bool create_nrom_mapper();
	bool create_mmc1_mapper();
	bool create_uxrom_mapper();
	bool create_cnrom_mapper();
	bool create_mmc3_mapper();

    bool has_trainer{};
    std::vector<Byte> prg_rom{}; // game machine code (program)
    std::vector<Byte> prg_ram{};
    std::vector<Byte> chr{}; // graphic assets data (Pattern Memory)
    bool chr_is_ram{};
    std::vector<Byte> raw_rom_data{};
    INesHeader INES_header;
    std::unique_ptr<Mapper> mapper;
    std::array<Byte, 2048> extra_nametable_ram{}; // in case 4 Screen mirroring mode is used
};

