#include "../include/apu.h"
#include <algorithm>
#include "apu.h"

Byte APU::cpu_read_register(Word addr)
{
    return Byte();
}

void APU::cpu_write_register(Word addr, Byte val)
{
    if(addr == 0x4000){
        pulse_channel_state.reg0 = val;
    }
    else if(addr == 0x4001){
        pulse_channel_state.reg1 = val;
    }
    else if(addr == 0x4002){
        pulse_channel_state.reg2 = val;
    }
    // ===================================================================
    // On the real NES, writing $4003 is kind of a “trigger-ish” write
    // for the pulse channel because it completes the timer and loads
    // length-related state. SO:
    // - store the byte
    // - reset waveform position in a clean way
    // - reload the timer counter from the full timer period
    // Because after the CPU finishes writing the pulse setup, we want
    // the pulse timer to start from the current configured timer period
    // and begin stepping the waveform from a known state.
    // ===================================================================
    else if(addr == 0x4003){
        pulse_channel_state.reg3 = val;
        pulse_channel_state.duty_step = 0;
        pulse_channel_state.timer_counter = get_pulse1_timer_period();
    }
    // ===================================================================
    // $4015 write - ---D NT21
    // bit 0 = enable pulse 1
    // bit 1 = enable pulse 2
    // bit 2 = enable triangle
    // bit 3 = enable noise
    // bit 4 = enable DMC
    // bits 7-5 are unused on write
    //
    // And in a simple first pass we can also clear/reset its runtime state
    // ===================================================================
    else if(addr == 0x4015){
        bool enabled = ((val & 0b1) != 0);
        pulse_channel_state.enabled = enabled;
        if(!enabled){
            pulse_channel_state.duty_step = 0;
            pulse_channel_state.timer_counter = 0;
        }
    }
}

// Step one tick of APU time
void APU::step_one_cycle()
{


    clock_pulse1_timer(); // advances duty_step for pulse 1
}

Byte APU::get_pulse1_duty_mode() const
{
    // $4000 - DDLC VVVV
    // D = duty cycle select        (bits 7-6)
    //
    // Extract the 2 bit duty selector from $4000
    // Basically which duty cycle pattern pulse 1 should use. Or same
    // which of the 4 waveform patterns is selected.

    return (pulse_channel_state.reg0 & Byte(0b11 << 6)) >> 6;
}

bool APU::pulse1_length_counter_halted() const
{
    // $4000 - DDLC VVVV
    // L = length counter halt      (bit 5)
    //
    // if length counter halt == false, the length counter is allowed to count down normally
    // if length counter halt == true, that countdown is halted

    return (pulse_channel_state.reg0 & Byte(0x1 << 5)) != 0;
}

bool APU::pulse1_uses_constant_volume() const
{
    // $4000 - DDLC VVVV
    // C = constant volume flag     (bit 4)
    //
    // 0 -> do not use constant volume
    // 1 -> use constant volume

    return (pulse_channel_state.reg0 & Byte(0x1 << 4)) != 0;
}

Byte APU::get_pulse1_volume_parameter() const
{
    // $4000 - DDLC VVVV
    // V = volume / envelope period (bits 3-0)
    //
    // The low nibble VVVV does not always mean the same thing.
    // Its meaning depends on bit 4.
    //
    // Case 1: (constant volume flag = 1)
    // -> the low 4 bits are used as a fixed volume level
    //
    // Case 2: (constant volume flag = 0)
    // -> the low 4 bits are used as the envelope period / divider parameter
    // So the channel’s volume is not fixed. Instead, it changes over time
    // according to the envelope unit.
    //
    // -----------------------
    // What is an envelope?   |
    // -----------------------
    // An envelope is automatic volume shaping over time. Instead of the sound
    // always staying at the same loudness, the APU can make it decay/change automatically.
    // So the envelope logic is like a tiny automatic volume controller.
    //
    // -----------------------------------
    // What does “envelope period” mean?  |
    // -----------------------------------
    // It means the low 4 bits help control how often the envelope updates.
    // So if constant volume is off, the low nibble is no longer “the volume value itself.”
    // Instead it is more like:
    // The timing parameter that controls the speed of the volume decay/update.

    return pulse_channel_state.reg0 & Byte(0b1111);
}

bool APU::pulse1_sweep_enabled() const
{
    // $4001 - EPPP NSSS
    // E = sweep enabled            (bit 7)
    //
    // ---------------------------
    // What is the sweep unit?    |
    // ---------------------------
    // The sweep unit is a piece of APU hardware that can automatically change
    // the pulse channel’s timer period over time.
    //
    // Since the timer period controls pitch:
    // changing timer period changes pitch
    // So the sweep unit is like a tiny automatic pitch modifier.
    //
    // For the pulse channel:
    // smaller timer period -> waveform advances faster -> higher pitch
    // larger timer period -> waveform advances slower -> lower pitch
    // So if hardware periodically changes the timer period, we hear pitch movement.
    //
    // If E = 0:
    // sweep logic is off
    // pulse timer stays as CPU set it, unless CPU writes again
    // If E = 1:
    // sweep logic is active
    // according to the other bits in $4001, the hardware periodically modifies the timer period

    return (pulse_channel_state.reg1 & Byte(0x1 << 7)) != 0;
}

Byte APU::get_pulse1_sweep_period_bits() const
{
    // $4001 - EPPP NSSS
    // P = sweep period             (bits 6-4)
    //
    // Sweep period means how many sweep timing ticks should pass before the
    // sweep unit updates the pulse timer period again.
    // So it is the timing parameter for the automatic pitch change mechanism.
    //
    // If sweep is enabled, the APU does not constantly modify the pitch every CPU cycle.
    // Instead, it waits some amount of time, then applies one sweep update,
    // then waits again, then applies another, and so on.
    //
    // So:
    // smaller sweep period -> updates happen more often
    // larger sweep period -> updates happen less often
    // That changes how quickly the pitch bends over time.
    //
    // NOTE:
    // This returns the raw 3 bit packed field. The effective sweep divider
    // period used by the hardware logic is typically this value + 1.

    return (pulse_channel_state.reg1 & Byte(0b111 << 4)) >> 4;
}

bool APU::pulse1_sweep_negated() const
{
    // $4001 - EPPP NSSS
    // N = sweep negate             (bit 3)
    //
    // Sweep Negate means when the sweep unit applies its automatic timer
    // change, should it go in the subtract direction instead of the add direction?

    return (pulse_channel_state.reg1 & Byte(0x1 << 3)) != 0;
}

Byte APU::get_pulse1_sweep_shift_count() const
{
    // $4001 - EPPP NSSS
    // S = sweep shift count        (bits 2-0)
    //
    // Sweep shift count means how much to right-shift the current pulse timer
    // period when computing the sweep change amount. So it controls the size of
    // each automatic pitch step.
    //
    // The sweep unit does not use a fixed change like:
    // add 3 or subtract 5. Instead, it computes the change from the
    // current timer period:
    // change = timer_period >> shift_count
    //
    // So:
    // lower shift count -> bigger sweep effect
    // higher shift count -> smaller sweep effect

    return pulse_channel_state.reg1 & Byte(0b111);
}

Byte APU::get_pulse1_timer_low_byte() const
{
    // $4002 - TTTT TTTT
    // T = timer low 8 bits         (bits 7-0)
    //
    // $4002 provides the low 8 bits of the 11-bit pulse timer period.
    // Together with the low 3 bits of $4003, it determines how fast
    // the pulse waveform advances, which in turn affects pitch.

    return pulse_channel_state.reg2;
}

Byte APU::get_pulse1_length_counter_load() const
{
    // $4003 - LLLL LTTT
    // L = length counter load      (bits 7-3)
    //
    // These bits are an index/code that tells the APU what starting length
    // to load into the pulse channel’s length counter
    //
    // -----------------------------
    // What is the length counter?  |
    // -----------------------------
    // The length counter is a little hardware countdown used to control
    // how long the channel is allowed to keep sounding.
    // - CPU writes $4003
    // - That loads a length value into the channel
    // - Over time, if length counting is not halted, that value counts down
    // - When it reaches zero, the channel stops producing sound
    // So it is basically a hardware “sound duration limiter.”
    //
    // -----------------------------------------
    // Why “load” and not just “length bits”?   |
    // -----------------------------------------
    // Because writing $4003 does not mean:
    // “store these exact 5 bits and directly use them as the countdown”
    // Instead, those 5 bits select a value from a length table.
    // So:
    // length_counter = LENGTH_TABLE[(reg3 >> 3) & 0b11111]
    // So this 5-bit field is a lookup code.

    return (pulse_channel_state.reg3 >> 3) & Byte(0b11111);
}

Byte APU::get_pulse1_timer_high_bits() const
{
    // $4003 - LLLL LTTT
    // T = timer high 3 bits        (bits 2-0)
    //
    // These 3 bits are the upper part of the 11-bit pulse timer period.
    // Combined with the full 8 bits from $4002, they form the timer value
    // that controls how fast the pulse waveform advances.

    return pulse_channel_state.reg3 & Byte(0b111);
}

Word APU::get_pulse1_timer_period() const
{
    // Pulse 1 timer period is an 11-bit value built from:
    // - low  8 bits from $4002
    // - high 3 bits from the low 3 bits of $4003
    // It answers: how long should we wait between waveform step advances?
    //
    // This timer period controls how fast the pulse waveform advances,
    // which in turn affects pitch.
    //
    // smaller timer period -> waveform advances faster -> higher pitch
    // larger timer period -> waveform advances slower -> lower pitch

    return (Word(get_pulse1_timer_high_bits()) << 8)
            | Word(get_pulse1_timer_low_byte());
}

void APU::clock_pulse1_timer()
{
    // ===========================================================================
    // This fucntion advances the pulse 1 timer hardware by one APU time step.
    //
    // The pulse channel has an 8-step waveform sequence.
    // The timer’s job is to control when you move to the next step in that sequence.
    // So the timer is like a countdown basically.
    //
    // If it has not expired yet
    // - keep waiting
    // - decrement the countdown
    //
    // If it expires
    // reload the countdown from the pulse timer period
    // advance the waveform position (duty_step) by one
    //
    // Btw in emulator/systems language, “clocking” a component usually means:
    // "advance it by one hardware timing step".
    // ===========================================================================

    if(!pulse_channel_state.enabled){
        return;
    }

    // ===========================================================================
    // Duty Step is the current position inside the pulse channel’s
    // repeating 8-step waveform pattern
    //
    // So the pulse channel does not output one constant high/low value forever.
    // Instead, it cycles through an 8-step sequence.
    //
    // The pulse channel supports 4 different waveform shapes. They are all
    // 8 steps long, but they differ in how many of those 8 steps are
    // high vs low, and where those highs/lows occur.
    // So:
    // - duty select chooses which pattern
    // - duty_step chooses where we currently are inside that pattern
    //
    // Example: Duty Mode 0 (12.5% duty)
    // array: 0 1 0 0 0 0 0 0  <- ("8 steps", [0 to 7])
    //
    // This output bit is not yet the final audio sample.
    // It is more like the raw waveform state:
    // 0 = low
    // 1 = high
    // Later, this gets combined with:
    // volume
    // envelope
    // length counter
    // sweep effects
    // mixer
    // ===========================================================================

    if(pulse_channel_state.timer_counter == 0){
        pulse_channel_state.timer_counter = get_pulse1_timer_period();
        pulse_channel_state.duty_step =
            Byte((pulse_channel_state.duty_step + 1) % 8);
    }
    else{
        pulse_channel_state.timer_counter--;
    }
}

Byte APU::get_pulse1_current_waveform_bit() const
{
    // Returns the raw waveform bit
    return PULSE_DUTY_TABLE[get_pulse1_duty_mode()][pulse_channel_state.duty_step];
}
