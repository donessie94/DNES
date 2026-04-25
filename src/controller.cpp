#include "../include/controller.h"

void Controller::set_button(Button button, bool pressed)
{
    Byte mask = Byte(1 << Byte(button));

    if(pressed){
        live_buttons |= mask;
    }
    else{
        live_buttons &= Byte(~mask);
    }
}

void Controller::write_strobe(Byte val)
{
    bool new_strobe = (val & Byte(0x1)) != 0;

    // When strobe is high, the controller keeps re latching
    // and reads keep returning the current A button state.
    //
    // When strobe goes from 1 -> 0, we latch the current
    // button state so the CPU can read it serially.
    if(strobe && !new_strobe){
        latch_buttons();
    }

    strobe = new_strobe;

    if(strobe){
        latch_buttons();
    }
}

Byte Controller::read()
{
    // If strobe is high, hardware keeps presenting button A.
    if(strobe){
        return live_buttons & Byte(0x1);
    }

    // Return current low bit, then shift for the next read.
    Byte result = shift_register & Byte(0x1);

    // After 8 reads, many emulators shift in 1s.
    // That is a common/simple NES behavior to emulate.
    shift_register = Byte((shift_register >> 1) | 0x80);

    return result;
}

void Controller::reset()
{
    live_buttons = 0;
    shift_register = 0;
    strobe = false;
}

void Controller::latch_buttons()
{
    shift_register = live_buttons;
}