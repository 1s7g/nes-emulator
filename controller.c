#include "controller.h"

void controller_init(Controller *ctrl) {
    ctrl->buttons = 0;
    ctrl->shift_reg = 0;
    ctrl->strobe = false;
}

void controller_write(Controller *ctrl, u8 val) {
    // bit 0 controls strobe
    ctrl->strobe = (val & 0x01) != 0;
    if (ctrl->strobe) {
        // while strobe is high, continuously reload shift register
        ctrl->shift_reg = ctrl->buttons;
    }
}

u8 controller_read(Controller *ctrl) {
    u8 result;
    
    if (ctrl->strobe) {
        // strobe mode - always return A button state
        result = ctrl->buttons & BTN_A ? 1 : 0;
    } else {
        // return bit 0 of shift register, then shift
        result = ctrl->shift_reg & 0x01;
        ctrl->shift_reg >>= 1;
        // after all 8 bits are read, returns 1s (open bus behavior)
        ctrl->shift_reg |= 0x80;
    }
    
    // only bit 0 is controller data, rest is open bus
    // but we just return 0 for those
    return result;
}

void controller_set_buttons(Controller *ctrl, u8 buttons) {
    ctrl->buttons = buttons;
}