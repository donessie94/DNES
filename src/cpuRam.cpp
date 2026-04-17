#include "../include/cpuRam.h"

Byte CPURam::read(Word addr)
{
    // 0x800 = 0b100000000000 is 2KB, we only need the lower 11 bits
    // and upper bits are "mirrored" to our 2KB actual space
    // Example: 0x0005 & 0x07FF = 0x0005 AND 0x0805 & 0x07FF = 0x0005 (SAME)
    addr &= 0x07FF; // addr & 0b011111111111 (getting the lower 11 bits)
    return mem[addr];
}

void CPURam::write(Word addr, Byte val)
{
    addr &= 0x07FF;
    mem[addr] = val;
}