#pragma once
#include "types.h"
#include <deque>

class APU {
public:
    APU() = default;
    Byte cpu_read_register(Word addr);
    void cpu_write_register(Word addr, Byte val);
    void step_one_cycle();
};