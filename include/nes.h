#pragma once
#include"cpu6502.h"
#include"bus.h"
#include"cpuRam.h"
#include"ppu.h"
#include"apu.h"
#include"cartridge.h"

class NES {
public:
    NES();
    CPU6502 cpu;
    Bus bus;
    CPURam cpu_ram;
    PPU ppu;
    APU apu;
    Cartridge cartridge;
};