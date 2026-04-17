#include "../include/nes.h"

NES::NES()
{
    // Connecting the components
    cpu.bus_ref         = &this->bus;
    bus.cpu_ram_ref     = &this->cpu_ram;
    bus.apu_ref         = &this->apu;
    bus.ppu_ref         = &this->ppu;
    bus.cartridge_ref   = &this->cartridge;
    ppu.cartridge_ref   = &this->cartridge;
}