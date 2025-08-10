#include "controller.h"

void controller_init(Controller *ctrl) {
    ctrl->buttons = 0;
    ctrl->shift_reg = 0;
    ctrl->strobe = false;
}

void controller_write(Controller *ctrl, u8 val) {
    bool new_strobe = (val & 0x01) != 0;
    ctrl->strobe = new_strobe;
    
    if (ctrl->strobe) {
        ctrl->shift_reg = ctrl->buttons;
    } else {
        ctrl->shift_reg = ctrl->buttons;
    }
}

u8 controller_read(Controller *ctrl) {
    u8 result;
    
    if (ctrl->strobe) {
        result = (ctrl->buttons & BTN_A) ? 1 : 0;
    } else {
        result = ctrl->shift_reg & 0x01;
        ctrl->shift_reg >>= 1;
        ctrl->shift_reg |= 0x80;
    }
    
    return result & 0x01;
}

void controller_set_buttons(Controller *ctrl, u8 buttons) {
    ctrl->buttons = buttons;
    if (ctrl->strobe) {
        ctrl->shift_reg = buttons;
    }
}