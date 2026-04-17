#pragma once
#include"types.h"

class CPURam {
public:
    CPURam() = default;
    Byte read(Word addr);
    void write(Word addr, Byte val);

    std::array<Byte, 2048> mem{};
};