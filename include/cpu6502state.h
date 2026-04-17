#pragma once
#include"types.h"

struct StatusFlags {
    inline Byte to_byte() const
    {
        return Byte(
            (carry ? 1 : 0) |
            ((zero ? 1 : 0) << 1) |
            ((interrupt_disable ? 1 : 0) << 2) |
            ((decimal ? 1 : 0) << 3) |
            ((brk ? 1 : 0) << 4) |
            ((unused ? 1 : 0) << 5) |
            ((overflow ? 1 : 0) << 6) |
            ((negative ? 1 : 0) << 7)
        );
    }

    inline void set_from_byte(Byte value)
    {
        carry = ((value & 0x01) != 0);
        zero = ((value & 0x02) != 0);
        interrupt_disable = ((value & 0x04) != 0);
        decimal = ((value & 0x08) != 0);
        brk = ((value & 0x10) != 0);
        unused = ((value & 0x20) != 0);
        overflow = ((value & 0x40) != 0);
        negative = ((value & 0x80) != 0);
    }

    bool carry{};              // Carry
    bool zero{};               // Zero
    bool interrupt_disable{};  // Interrupt Disable
    bool decimal{};            // Decimal
    bool brk{};                // Break
    bool unused{};             // Unused / always set in some contexts
    bool overflow{};           // Overflow
    bool negative{};           // Negative
};

struct CpuRegisters {
    Byte a{};         // Accumulator
    Byte x{};         // Indexes
    Byte y{};
    Word pc{};        // Program Counter
    Byte sp{};        // Stack Pointer
    StatusFlags p{};  // Status Register
};