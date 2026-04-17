#pragma once
#include"types.h"

class CPURam;
class APU;
class PPU;
class Cartridge;

class Bus {
public:
    Bus() = default;
    Byte cpu_read(Word addr);
    void cpu_write(Word addr, Byte val);

    CPURam* cpu_ram_ref = nullptr;
    APU* apu_ref = nullptr;
    PPU* ppu_ref = nullptr;
    Cartridge* cartridge_ref = nullptr;
};