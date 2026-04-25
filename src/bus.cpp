#include "../include/bus.h"
#include "../include/cpuRam.h"
#include "../include/ppu.h"
#include "../include/apu.h"
#include "../include/cartridge.h"
#include "../include/controller.h"


Byte Bus::cpu_read(Word addr) // addr is 16 bits so max range is 0xFFFF
{
    if(addr <= 0x1FFF)                     // CPU RAM + mirrors
        return cpu_ram_ref->read(addr);

    if(addr <= 0x3FFF)                     // PPU registers + mirrors
        return ppu_ref->cpu_read_register(addr);

    // Controller 1 serial read port
    if(addr == 0x4016)
        return controller1_ref->read();

    // Controller 2 serial read port
    if(addr == 0x4017)
        return controller2_ref->read();

    if(addr <= 0x4015)                     // APU registers
        return apu_ref->cpu_read_register(addr);

    if(addr <= 0x401F)                     // normally disabled/test mode area
        return apu_ref->cpu_read_register(addr);

    return cartridge_ref->cpu_read(addr);  // cartridge space: $4020–$FFFF
}

void Bus::cpu_write(Word addr, Byte val)
{
    // SPECIAL CASE: PPU OAM
    // val = CPU high byte adress (page number)
    // Thus we copy CPU page XX00-XXFF (256 bytes the whole page)
    // into PPU OAM memory
    // NOTE: real OAMDMA stalls the CPU for 513/514 cycles (MUST IMPLEMENT)
    if(addr == 0x4014){
        Word page_start = Word(val << 8);
        std::size_t idx = 0;
        while(idx < 256){
            ppu_ref->oam_mem[idx] = cpu_read(page_start + idx);
            idx++;
        }
        return;
    }

    // Controller strobe write.
    // Writing bit 0 of $4016 controls latching for both controllers.
    if(addr == 0x4016){
        controller1_ref->write_strobe(val);
        controller2_ref->write_strobe(val);
        return;
    }

    if(addr <= 0x1FFF) { cpu_ram_ref->write(addr, val); return; }
    if(addr <= 0x3FFF) { ppu_ref->cpu_write_register(addr, val); return; }
    if(addr <= 0x4017) { apu_ref->cpu_write_register(addr, val); return; }
    if(addr <= 0x401F) { apu_ref->cpu_write_register(addr, val); return; }

    cartridge_ref->cpu_write(addr, val);
}