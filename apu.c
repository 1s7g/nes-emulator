#include "apu.h"
#include <string.h>
#include <stdio.h>

// length counter lookup table
// why these specific values? i have no idea, ask nintendo
static const u8 length_table[32] = {
    10, 254, 20,  2, 40,  4, 80,  6, 160,  8, 60, 10, 14, 12, 26, 14,
    12,  16, 24, 18, 48, 20, 96, 22, 192, 24, 72, 26, 16, 28, 32, 30
};

// duty cycle lookup - pulse channels use these
// each entry is 8 bits representing one duty cycle pattern
static const u8 duty_table[4][8] = {
    {0, 1, 0, 0, 0, 0, 0, 0},  // 12.5%
    {0, 1, 1, 0, 0, 0, 0, 0},  // 25%
    {0, 1, 1, 1, 1, 0, 0, 0},  // 50%
    {1, 0, 0, 1, 1, 1, 1, 1}   // 25% negated
};

// triangle channel sequence - 32 steps
static const u8 triangle_table[32] = {
    15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
    0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15
};

// noise period lookup table
static const u16 noise_period_table[16] = {
    4, 8, 16, 32, 64, 96, 128, 160, 202, 254, 380, 508, 762, 1016, 2034, 4068
};

void apu_init(APU *apu) {
    memset(apu, 0, sizeof(APU));
    
    // noise shift register starts at 1
    apu->noise.shift_reg = 1;
    
    // gonna target ~44100 samples per second
    // nes runs at ~1.789773 MHz cpu clock
    // thats roughly 29780 cpu cycles per frame at 60fps
    // so we need about 735 samples per frame (44100/60)
    apu->samples_per_frame = 735;
    
    printf("[APU] initialized\n");
}

void apu_reset(APU *apu) {
    apu_init(apu);
    printf("[APU] reset\n");
}

// clock the envelope units
static void clock_envelope(PulseChannel *pulse) {
    if (pulse->envelope_start) {
        pulse->envelope_start = false;
        pulse->envelope_vol = 15;
        pulse->envelope_counter = (pulse->reg_ctrl & 0x0F);
    } else {
        if (pulse->envelope_counter > 0) {
            pulse->envelope_counter--;
        } else {
            pulse->envelope_counter = (pulse->reg_ctrl & 0x0F);
            if (pulse->envelope_vol > 0) {
                pulse->envelope_vol--;
            } else if (pulse->reg_ctrl & 0x20) {
                // loop flag
                pulse->envelope_vol = 15;
            }
        }
    }
}

static void clock_noise_envelope(NoiseChannel *noise) {
    if (noise->envelope_start) {
        noise->envelope_start = false;
        noise->envelope_vol = 15;
        noise->envelope_counter = (noise->reg_ctrl & 0x0F);
    } else {
        if (noise->envelope_counter > 0) {
            noise->envelope_counter--;
        } else {
            noise->envelope_counter = (noise->reg_ctrl & 0x0F);
            if (noise->envelope_vol > 0) {
                noise->envelope_vol--;
            } else if (noise->reg_ctrl & 0x20) {
                noise->envelope_vol = 15;
            }
        }
    }
}

// clock length counters
static void clock_length_counters(APU *apu) {
    // pulse 1
    if (apu->pulse1.length_counter > 0 && !(apu->pulse1.reg_ctrl & 0x20)) {
        apu->pulse1.length_counter--;
    }
    // pulse 2
    if (apu->pulse2.length_counter > 0 && !(apu->pulse2.reg_ctrl & 0x20)) {
        apu->pulse2.length_counter--;
    }
    // triangle
    if (apu->triangle.length_counter > 0 && !apu->triangle.control_flag) {
        apu->triangle.length_counter--;
    }
    // noise
    if (apu->noise.length_counter > 0 && !(apu->noise.reg_ctrl & 0x20)) {
        apu->noise.length_counter--;
    }
}

// clock sweep units
static void clock_sweep(PulseChannel *pulse, bool is_pulse1) {
    // calculate target period
    u16 change = pulse->timer >> pulse->sweep_shift;
    u16 target;
    if (pulse->sweep_negate) {
        target = pulse->timer - change;
        if (is_pulse1) target--; // pulse 1 has different negate behavior, why nintendo
    } else {
        target = pulse->timer + change;
    }
    
    // mute if target > 0x7FF or timer < 8
    bool mute = (target > 0x7FF) || (pulse->timer < 8);
    
    if (pulse->sweep_counter == 0 && pulse->sweep_enabled && !mute && pulse->sweep_shift > 0) {
        pulse->timer = target;
    }
    
    if (pulse->sweep_counter == 0 || pulse->sweep_reload) {
        pulse->sweep_counter = pulse->sweep_period;
        pulse->sweep_reload = false;
    } else {
        pulse->sweep_counter--;
    }
}

// clock linear counter (triangle only)
static void clock_linear_counter(TriangleChannel *tri) {
    if (tri->linear_reload) {
        tri->linear_counter = tri->reg_ctrl & 0x7F;
    } else if (tri->linear_counter > 0) {
        tri->linear_counter--;
    }
    
    if (!tri->control_flag) {
        tri->linear_reload = false;
    }
}

// frame counter - clocks various units at 240hz / 192hz
static void clock_frame_counter(APU *apu) {
    // this is called every cpu cycle, but we only do stuff at specific points
    // 4-step sequence: clocks at 3728.5, 7456.5, 11185.5, 14914.5 cpu cycles
    // just gonna approximate
    
    int step = -1;
    
    if (apu->frame_counter_mode == 0) {
        // 4-step mode
        if (apu->frame_counter == 3729) step = 0;
        else if (apu->frame_counter == 7457) step = 1;
        else if (apu->frame_counter == 11186) step = 2;
        else if (apu->frame_counter == 14915) {
            step = 3;
            apu->frame_counter = 0;
        }
    } else {
        // 5-step mode
        if (apu->frame_counter == 3729) step = 0;
        else if (apu->frame_counter == 7457) step = 1;
        else if (apu->frame_counter == 11186) step = 2;
        else if (apu->frame_counter == 14915) step = 3;
        else if (apu->frame_counter == 18641) {
            step = 4;
            apu->frame_counter = 0;
        }
    }
    
    apu->frame_counter++;
    
    if (step < 0) return;
    
    // envelope and linear counter clock every step
    clock_envelope(&apu->pulse1);
    clock_envelope(&apu->pulse2);
    clock_noise_envelope(&apu->noise);
    clock_linear_counter(&apu->triangle);
    
    // length counter and sweep clock on steps 1 and 3 (4-step) or 0,2,4 (5-step)
    bool clock_length = false;
    if (apu->frame_counter_mode == 0) {
        clock_length = (step == 1 || step == 3);
    } else {
        clock_length = (step == 0 || step == 2 || step == 4);
    }
    
    if (clock_length) {
        clock_length_counters(apu);
        clock_sweep(&apu->pulse1, true);
        clock_sweep(&apu->pulse2, false);
    }
}

// get output from pulse channel
static u8 pulse_output(PulseChannel *pulse) {
    if (!pulse->enabled) return 0;
    if (pulse->length_counter == 0) return 0;
    if (pulse->timer < 8) return 0;  // muted
    
    u8 duty = (pulse->reg_ctrl >> 6) & 0x03;
    u8 duty_out = duty_table[duty][pulse->duty_pos];
    
    if (duty_out == 0) return 0;
    
    // constant volume or envelope
    if (pulse->reg_ctrl & 0x10) {
        return pulse->reg_ctrl & 0x0F;  // constant volume
    } else {
        return pulse->envelope_vol;
    }
}

// get output from triangle channel
static u8 triangle_output(TriangleChannel *tri) {
    if (!tri->enabled) return 0;
    if (tri->length_counter == 0) return 0;
    if (tri->linear_counter == 0) return 0;
    
    return triangle_table[tri->sequence_pos];
}

// get output from noise channel
static u8 noise_output(NoiseChannel *noise) {
    if (!noise->enabled) return 0;
    if (noise->length_counter == 0) return 0;
    if (noise->shift_reg & 1) return 0;  // bit 0 mutes
    
    if (noise->reg_ctrl & 0x10) {
        return noise->reg_ctrl & 0x0F;
    } else {
        return noise->envelope_vol;
    }
}

void apu_step(APU *apu) {
    apu->cycle_count++;
    
    // frame counter runs every cycle
    clock_frame_counter(apu);
    
    // triangle timer clocks every cpu cycle
    if (apu->triangle.timer_counter == 0) {
        apu->triangle.timer_counter = apu->triangle.timer;
        if (apu->triangle.length_counter > 0 && apu->triangle.linear_counter > 0) {
            apu->triangle.sequence_pos = (apu->triangle.sequence_pos + 1) & 31;
        }
    } else {
        apu->triangle.timer_counter--;
    }
    
    // pulse and noise timers clock every other cpu cycle (apu cycle)
    if (apu->cycle_count % 2 == 0) {
        // pulse 1
        if (apu->pulse1.timer_counter == 0) {
            apu->pulse1.timer_counter = apu->pulse1.timer;
            apu->pulse1.duty_pos = (apu->pulse1.duty_pos + 1) & 7;
        } else {
            apu->pulse1.timer_counter--;
        }
        
        // pulse 2
        if (apu->pulse2.timer_counter == 0) {
            apu->pulse2.timer_counter = apu->pulse2.timer;
            apu->pulse2.duty_pos = (apu->pulse2.duty_pos + 1) & 7;
        } else {
            apu->pulse2.timer_counter--;
        }
        
        // noise
        if (apu->noise.timer_counter == 0) {
            apu->noise.timer_counter = apu->noise.timer;
            // clock shift register
            u8 bit = apu->noise.mode ? 6 : 1;
            u8 feedback = (apu->noise.shift_reg & 1) ^ ((apu->noise.shift_reg >> bit) & 1);
            apu->noise.shift_reg >>= 1;
            apu->noise.shift_reg |= (feedback << 14);
        } else {
            apu->noise.timer_counter--;
        }
    }
    
    // mix channels and generate sample
    // we dont output every cycle, just sample periodically
    // roughly 40 cpu cycles per sample at 44100hz
    if (apu->cycle_count % 40 == 0) {
        u8 p1 = pulse_output(&apu->pulse1);
        u8 p2 = pulse_output(&apu->pulse2);
        u8 tri = triangle_output(&apu->triangle);
        u8 noi = noise_output(&apu->noise);
        
        // mixing formula from nesdev wiki
        // not gonna be super accurate but good enough
        float pulse_out = 0.00752f * (p1 + p2);
        float tnd_out = 0.00851f * tri + 0.00494f * noi;
        
        apu->output_sample = pulse_out + tnd_out;
        
        // store in buffer
        if (apu->sample_index < 2048) {
            apu->sample_buffer[apu->sample_index++] = apu->output_sample;
        }
    }
}

void apu_write(APU *apu, u16 addr, u8 val) {
    switch (addr) {
        // Pulse 1
        case 0x4000:
            apu->pulse1.reg_ctrl = val;
            break;
        case 0x4001:
            apu->pulse1.reg_sweep = val;
            apu->pulse1.sweep_enabled = (val & 0x80) != 0;
            apu->pulse1.sweep_period = (val >> 4) & 0x07;
            apu->pulse1.sweep_negate = (val & 0x08) != 0;
            apu->pulse1.sweep_shift = val & 0x07;
            apu->pulse1.sweep_reload = true;
            break;
        case 0x4002:
            apu->pulse1.reg_timer_lo = val;
            apu->pulse1.timer = (apu->pulse1.timer & 0x700) | val;
            break;
        case 0x4003:
            apu->pulse1.reg_timer_hi = val;
            apu->pulse1.timer = (apu->pulse1.timer & 0xFF) | ((val & 0x07) << 8);
            if (apu->pulse1.enabled) {
                apu->pulse1.length_counter = length_table[val >> 3];
            }
            apu->pulse1.envelope_start = true;
            apu->pulse1.duty_pos = 0;
            break;
            
        // Pulse 2
        case 0x4004:
            apu->pulse2.reg_ctrl = val;
            break;
        case 0x4005:
            apu->pulse2.reg_sweep = val;
            apu->pulse2.sweep_enabled = (val & 0x80) != 0;
            apu->pulse2.sweep_period = (val >> 4) & 0x07;
            apu->pulse2.sweep_negate = (val & 0x08) != 0;
            apu->pulse2.sweep_shift = val & 0x07;
            apu->pulse2.sweep_reload = true;
            break;
        case 0x4006:
            apu->pulse2.reg_timer_lo = val;
            apu->pulse2.timer = (apu->pulse2.timer & 0x700) | val;
            break;
        case 0x4007:
            apu->pulse2.reg_timer_hi = val;
            apu->pulse2.timer = (apu->pulse2.timer & 0xFF) | ((val & 0x07) << 8);
            if (apu->pulse2.enabled) {
                apu->pulse2.length_counter = length_table[val >> 3];
            }
            apu->pulse2.envelope_start = true;
            apu->pulse2.duty_pos = 0;
            break;
            
        // Triangle
        case 0x4008:
            apu->triangle.reg_ctrl = val;
            apu->triangle.control_flag = (val & 0x80) != 0;
            break;
        case 0x4009:
            // unused
            break;
        case 0x400A:
            apu->triangle.reg_timer_lo = val;
            apu->triangle.timer = (apu->triangle.timer & 0x700) | val;
            break;
        case 0x400B:
            apu->triangle.reg_timer_hi = val;
            apu->triangle.timer = (apu->triangle.timer & 0xFF) | ((val & 0x07) << 8);
            if (apu->triangle.enabled) {
                apu->triangle.length_counter = length_table[val >> 3];
            }
            apu->triangle.linear_reload = true;
            break;
            
        // Noise
        case 0x400C:
            apu->noise.reg_ctrl = val;
            break;
        case 0x400D:
            // unused
            break;
        case 0x400E:
            apu->noise.reg_period = val;
            apu->noise.mode = (val & 0x80) != 0;
            apu->noise.timer = noise_period_table[val & 0x0F];
            break;
        case 0x400F:
            apu->noise.reg_length = val;
            if (apu->noise.enabled) {
                apu->noise.length_counter = length_table[val >> 3];
            }
            apu->noise.envelope_start = true;
            break;
            
        // Status
        case 0x4015:
            apu->pulse1.enabled = (val & 0x01) != 0;
            apu->pulse2.enabled = (val & 0x02) != 0;
            apu->triangle.enabled = (val & 0x04) != 0;
            apu->noise.enabled = (val & 0x08) != 0;
            
            if (!apu->pulse1.enabled) apu->pulse1.length_counter = 0;
            if (!apu->pulse2.enabled) apu->pulse2.length_counter = 0;
            if (!apu->triangle.enabled) apu->triangle.length_counter = 0;
            if (!apu->noise.enabled) apu->noise.length_counter = 0;
            break;
            
        // Frame counter
        case 0x4017:
            apu->frame_counter_mode = (val & 0x80) ? 1 : 0;
            apu->frame_irq_inhibit = (val & 0x40) != 0;
            apu->frame_counter = 0;
            // if mode 1, clock everything immediately
            if (apu->frame_counter_mode == 1) {
                clock_envelope(&apu->pulse1);
                clock_envelope(&apu->pulse2);
                clock_noise_envelope(&apu->noise);
                clock_linear_counter(&apu->triangle);
                clock_length_counters(apu);
                clock_sweep(&apu->pulse1, true);
                clock_sweep(&apu->pulse2, false);
            }
            break;
    }
}

u8 apu_read(APU *apu, u16 addr) {
    if (addr == 0x4015) {
        u8 status = 0;
        if (apu->pulse1.length_counter > 0) status |= 0x01;
        if (apu->pulse2.length_counter > 0) status |= 0x02;
        if (apu->triangle.length_counter > 0) status |= 0x04;
        if (apu->noise.length_counter > 0) status |= 0x08;
        // TODO: DMC and frame interrupt flags
        return status;
    }
    return 0;
}

float apu_get_sample(APU *apu) {
    return apu->output_sample;
}