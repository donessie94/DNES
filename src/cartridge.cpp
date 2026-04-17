#include "../include/cartridge.h"
#include "cartridge.h"
#include <iostream>

bool Cartridge::initialize_from_file(const std::string &path)
{
    if(!load_raw_file(path))                { return false; }
    if(!parse_header())                     { return false; }
    if(!load_prg_rom())                     { return false; }
    if(!load_chr())                         { return false; }
    if(!initialize_mapper_from_header())    { return false; }
    load_prg_ram();

    return true;
}

Byte Cartridge::cpu_read(Word addr)
{
    TranslationResult tr_result = mapper->translate_cpu_read_addr(addr);
    if(!tr_result.success) return 0x00;
    switch (tr_result.region)
    {
    case CartridgeRegion::PRG_RAM: return prg_ram[tr_result.translated_offset];
    case CartridgeRegion::PRG_ROM: return prg_rom[tr_result.translated_offset];
    }
    return 0x00;
}

void Cartridge::cpu_write(Word addr, Byte val)
{
    TranslationResult tr_result = mapper->translate_cpu_write_addr(addr);
    if(!tr_result.success) return;
    switch (tr_result.region)
    {
    case CartridgeRegion::PRG_RAM:
        prg_ram[tr_result.translated_offset] = val;
    }
}

Byte Cartridge::ppu_read(Word addr)
{
    TranslationResult tr_result = mapper->translate_ppu_read_addr(addr);
    if(!tr_result.success) return 0x00;
    switch (tr_result.region){
    case CartridgeRegion::CHR: return chr[tr_result.translated_offset];
    }
    return 0x00;
}

void Cartridge::ppu_write(Word addr, Byte val)
{
    TranslationResult tr_result = mapper->translate_ppu_write_addr(addr);
    if(!tr_result.success || !chr_is_ram) return;
    switch (tr_result.region)
    {
    case CartridgeRegion::CHR:
        chr[tr_result.translated_offset] = val;
    }
}

bool Cartridge::load_raw_file(const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) { // if file was not open
        std::cout<<"File failed to open"<<'\n';
        return false;
    }

    file.seekg(0, std::ios::end); // moves the file’s read position to the end of the file

    // now we ask how far the "seekg" position is from the start (giving us file size)
    std::streamsize size = file.tellg();
    if (size <= 0) {
        return false;
    }

    file.seekg(0, std::ios::beg); // moves the file’s read position to the start of the file

    std::vector<Byte> buffer(size); // our buffer for the ROM now with the right size

    Byte* buff_start = buffer.data(); // buffer.data() gives a raw pointer to first element of the vector.

    // reinterpret_cast<char*> cuz ifstream::read() expects a char* buffer but our buffer is Byte*
    char* buff_start_casted = reinterpret_cast<char*>(buff_start);

    if (!file.read(buff_start_casted, size)) {
        return false;
    }

    raw_rom_data = std::move(buffer); // save the raw data to our actual class buffer
    return true;
}

bool Cartridge::parse_header()
{
    return INES_header.set_from_data(raw_rom_data);
}

bool Cartridge::load_prg_rom()
{
    std::size_t chunks_16KB = INES_header.prg_rom_banks;
    has_trainer = (INES_header.flags6 & 0x04) != 0; // present if bit 2 of flags6 is set
    std::size_t trainer_size = has_trainer ? 512 : 0;
    std::size_t start_idx = 16 + trainer_size; // first 16 bytes are the iNES header, then optional trainer
    std::size_t prg_rom_size = 16384 * chunks_16KB; // each PRG ROM chunk is exactly 16 KB = 16384 bytes

    // safety check so we do not read past the loaded ROM buffer
    if (raw_rom_data.size() < start_idx + prg_rom_size) {
        return false;
    }

    prg_rom.resize(prg_rom_size);
    std::copy( // std::copy(from_first, from_last, destination_begin)
        raw_rom_data.begin() + start_idx,
        raw_rom_data.begin() + start_idx + prg_rom_size,
        prg_rom.begin()
    );

    return true;
}

void Cartridge::load_prg_ram()
{
    std::size_t chunks_8KB = INES_header.prg_ram_banks;
    // In old iNES, 0 usually means assume 8 KB PRG RAM
    std::size_t prg_ram_size = (chunks_8KB == 0) ? 8192 : 8192 * chunks_8KB;
    prg_ram.resize(prg_ram_size);
}

bool Cartridge::load_chr()
{
    std::size_t chunks_8KB = INES_header.chr_rom_banks;
    chr_is_ram = (chunks_8KB == 0);

    std::size_t trainer_size = has_trainer ? 512 : 0;
    std::size_t prg_rom_size = 16384 * INES_header.prg_rom_banks;
    std::size_t start_idx = 16 + trainer_size + prg_rom_size;

    // If no CHR ROM present, assume 8KB of CHR RAM
    if (chr_is_ram) {
        chr.resize(8192);
        return true;
    }

    // Otherwise load CHR ROM from file
    std::size_t chr_size = 8192 * chunks_8KB;
    if (raw_rom_data.size() < start_idx + chr_size) {
        return false;
    }

    chr.resize(chr_size);

    std::copy(
        raw_rom_data.begin() + start_idx,
        raw_rom_data.begin() + start_idx + chr_size,
        chr.begin()
    );

    return true;
}

bool Cartridge::initialize_mapper_from_header()
{
    // retrieve mapper id
    Byte mapper_id_lo = (INES_header.flags6 & 0xF0) >> 4;
    Byte mapper_id_hi = INES_header.flags7 & 0xF0;
    MapperType id = MapperType(mapper_id_hi | mapper_id_lo);
    switch (id)
    {
    case MapperType::NROM:  return create_nrom_mapper();
    case MapperType::MMC1:  return create_mmc1_mapper();
    case MapperType::UxROM: return create_uxrom_mapper();
    case MapperType::CNROM: return create_cnrom_mapper();
    case MapperType::MMC3:  return create_mmc3_mapper();
    }
    return false; // not supported mappers yet wont get initialized
}

bool Cartridge::create_nrom_mapper()
{
    mapper = std::make_unique<NROM>(); // unique ptr version of (ptr = new NROM)
    mapper->prg_rom_banks   = INES_header.prg_rom_banks;
    mapper->chr_is_ram      = chr_is_ram;
    mapper->chr_rom_banks   = INES_header.chr_rom_banks;
    return true;
}

bool Cartridge::create_mmc1_mapper()
{
    return false;
}

bool Cartridge::create_uxrom_mapper()
{
    return false;
}

bool Cartridge::create_cnrom_mapper()
{
    return false;
}

bool Cartridge::create_mmc3_mapper()
{
    return false;
}
