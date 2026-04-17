#include "../include/mapper.h"
#include "mapper.h"

TranslationResult NROM::translate_cpu_read_addr(Word addr)
{
    // security check
    if(addr < 0x6000 || addr > 0xFFFF) return {.success = false};

    // 0x1FFF size so 8191 + 1 = 8192 addressable memory cells of 1 Byte each (8KB window)
    // CPU: 0x6000 - 0x7FFF -> Cartridge: 0x0000 to 0x1FFF (and mirror always here)
    if (addr <= 0x7FFF){    // Unbanked PRG-RAM, mirrored as necessary to fill entire 8 KB window
        Word translated_addr = 0x1FFF - (0x7FFF - addr);
        // no mirroring needs here cuz we allocate always 8KB of RAM in cartridge
        return {true, CartridgeRegion::PRG_RAM, translated_addr};
    }

    // $BFFF - $8000 = $3FFF size = 16384 bytes (16384 memory slots of a byte each)
    if (addr <= 0xBFFF){ // First 16 KiB of PRG-ROM
        Word translated_addr  = 0x3FFF - (0xBFFF - addr);
        return {true, CartridgeRegion::PRG_ROM, translated_addr};
    }

    // 0xFFFF - 0xC000 = 0x3FFF size = 16384 bytes = 16 Kib
    if (addr <= 0xFFFF){ // Last 16 KiB of PRG-ROM (NROM-256 -> rom_bank > 1) or mirror of $8000-$BFFF (NROM-128)
        Word translated_addr  = 0x3FFF - (0xFFFF - addr);
        if(prg_rom_banks > 1) { translated_addr += 16384; } // if 32Kib exist (instead of 16KiB) then map to upper 16Kibs bank
        return {true, CartridgeRegion::PRG_ROM, translated_addr};
    }

    return {.success = false};
}

TranslationResult NROM::translate_cpu_write_addr(Word addr)
{
    // Only RAM is allowed to be written to by the CPU
    if(addr < 0x6000 || addr > 0xFFFF) return {.success = false};
    if (addr <= 0x7FFF){
        Word translated_addr = 0x1FFF - (0x7FFF - addr);
        return {true, CartridgeRegion::PRG_RAM, translated_addr};
    }
    return {.success = false};
}

TranslationResult NROM::translate_ppu_read_addr(Word addr)
{
    if(addr > 0x1FFF) return {.success = false}; // Word cant be less than 0 (uint16)
    Word translated_addr = addr; // since it starts from 0 it is as it is
    return {true, CartridgeRegion::CHR, translated_addr};
}

TranslationResult NROM::translate_ppu_write_addr(Word addr)
{
    if(addr > 0x1FFF || !chr_is_ram) return {.success = false};
    Word translated_addr = addr;
    return {true, CartridgeRegion::CHR, translated_addr};
}
