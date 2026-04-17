#pragma once
#include"types.h"

class APU {
public:
    APU() = default;
    Byte cpu_read_register(Word addr);
    void cpu_write_register(Word addr, Byte val);
};