#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "types.h"

// NES controller buttons (active high)
#define BTN_A       0x01
#define BTN_B       0x02
#define BTN_SELECT  0x04
#define BTN_START   0x08
#define BTN_UP      0x10
#define BTN_DOWN    0x20
#define BTN_LEFT    0x40
#define BTN_RIGHT   0x80

typedef struct {
    u8 buttons;      // current button state
    u8 shift_reg;    // shift register for serial output
    bool strobe;     // strobe mode
} Controller;

void controller_init(Controller *ctrl);
void controller_write(Controller *ctrl, u8 val);
u8 controller_read(Controller *ctrl);
void controller_set_buttons(Controller *ctrl, u8 buttons);

#endif