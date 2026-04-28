#pragma once
#include "types.h"
#include <deque>

class APU {
public:
    APU() = default;
    Byte cpu_read_register(Word addr);
    void cpu_write_register(Word addr, Byte val);
    void step_one_cycle();

    // Pulse 1 register raw storage helpers
    //
    // $4000 - DDLC VVVV
    // D = duty cycle select        (bits 7-6)
    // L = length counter halt      (bit 5)
    // C = constant volume flag     (bit 4)
    // V = volume / envelope period (bits 3-0)
    inline void set_pulse1_reg0(Byte val)   { pulse_channel_state.reg0 = val; }
    inline Byte get_pulse1_reg0() const     { return pulse_channel_state.reg0; }

    Byte get_pulse1_duty_mode() const;
    bool pulse1_length_counter_halted() const;
    bool pulse1_uses_constant_volume() const;
    Byte get_pulse1_volume_parameter() const;


    // $4001 - EPPP NSSS
    // E = sweep enabled            (bit 7)
    // P = sweep period             (bits 6-4)
    // N = sweep negate             (bit 3)
    // S = sweep shift count        (bits 2-0)
    inline void set_pulse1_reg1(Byte val)   { pulse_channel_state.reg1 = val; }
    inline Byte get_pulse1_reg1() const     { return pulse_channel_state.reg1; }

    bool pulse1_sweep_enabled() const;
    Byte get_pulse1_sweep_period_bits() const;
    bool pulse1_sweep_negated() const;
    Byte get_pulse1_sweep_shift_count() const;


    // $4002 - TTTT TTTT
    // T = timer low 8 bits         (bits 7-0)
    inline void set_pulse1_reg2(Byte val)   { pulse_channel_state.reg2 = val; }
    inline Byte get_pulse1_reg2() const     { return pulse_channel_state.reg2; }

    Byte get_pulse1_timer_low_byte() const;


    // $4003 - LLLL LTTT
    // L = length counter load      (bits 7-3)
    // T = timer high 3 bits        (bits 2-0)
    inline void set_pulse1_reg3(Byte val)   { pulse_channel_state.reg3 = val; }
    inline Byte get_pulse1_reg3() const     { return pulse_channel_state.reg3; }

    Byte get_pulse1_length_counter_load() const;
    Byte get_pulse1_timer_high_bits() const;


    // Pulse 1 derived value helpers
    //
    // Timer period is built from:
    // low  8 bits = $4002
    // high 3 bits = low 3 bits of $4003
    Word get_pulse1_timer_period() const;


    // Pulse 1 runtime state helpers
    //
    inline void set_pulse1_enabled(bool enabled)    { pulse_channel_state.enabled = enabled; }
    inline bool pulse1_is_enabled() const           { return pulse_channel_state.enabled; }

    void clock_pulse1_timer();
    inline Byte get_pulse1_duty_step() const        { return pulse_channel_state.duty_step; }

    Byte get_pulse1_current_waveform_bit() const;

    PulseChannelState pulse_channel_state{};
};