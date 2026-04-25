#pragma once
#include "types.h"

class Controller {
public:
    enum class Button : Byte {
        A      = 0,
        B      = 1,
        Select = 2,
        Start  = 3,
        Up     = 4,
        Down   = 5,
        Left   = 6,
        Right  = 7
    };

    Controller() = default;

    void set_button(Button button, bool pressed);

    void write_strobe(Byte val);
    Byte read();

    void reset();

private:
    void latch_buttons();

private:
    // Current real time state of the 8 controller buttons.
    Byte live_buttons{0};

    // Latched copy that gets shifted out one bit at a time
    // when the CPU reads $4016 / $4017.
    Byte shift_register{0};

    // $4016 bit 0 controls controller strobing/latching.
    bool strobe{false};
};