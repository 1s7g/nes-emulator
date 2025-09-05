#ifndef APU_H
#define APU_H

#include "types.h"

// APU registers are at $4000-$4017
// $4000-$4003 = Pulse 1
// $4004-$4007 = Pulse 2
// $4008-$400B = Triangle
// $400C-$400F = Noise
// $4010-$4013 = DMC (not implementing this one yet, its complicated)
// $4015 = Status/control
// $4017 = Frame counter

// pulse channel - theres two of these
typedef struct {
    // register values (raw writes)
    u8 reg_ctrl;      // $4000/$4004 - duty, loop, constant vol, volume
    u8 reg_sweep;     // $4001/$4005 - sweep unit
    u8 reg_timer_lo;  // $4002/$4006 - timer low
    u8 reg_timer_hi;  // $4003/$4007 - length counter load, timer high
    
    // internal state
    u16 timer;          // 11-bit timer (period)
    u16 timer_counter;  // counts down
    u8 duty_pos;        // where we are in the duty cycle (0-7)
    u8 length_counter;  // counts down, channel silenced when 0
    u8 envelope_vol;    // current envelope volume
    u8 envelope_counter;
    bool envelope_start;
    
    // sweep unit
    bool sweep_reload;
    u8 sweep_counter;
    bool sweep_enabled;
    bool sweep_negate;
    u8 sweep_period;
    u8 sweep_shift;
    
    bool enabled;
} PulseChannel;

// triangle channel
typedef struct {
    u8 reg_ctrl;      // $4008
    u8 reg_timer_lo;  // $400A
    u8 reg_timer_hi;  // $400B
    
    u16 timer;
    u16 timer_counter;
    u8 sequence_pos;    // 0-31, triangle uses 32-step sequence
    u8 length_counter;
    u8 linear_counter;
    bool linear_reload;
    bool control_flag;
    
    bool enabled;
} TriangleChannel;

// noise channel
typedef struct {
    u8 reg_ctrl;      // $400C
    u8 reg_period;    // $400E
    u8 reg_length;    // $400F
    
    u16 shift_reg;    // 15-bit shift register
    u16 timer;
    u16 timer_counter;
    u8 length_counter;
    u8 envelope_vol;
    u8 envelope_counter;
    bool envelope_start;
    bool mode;        // short mode vs long mode
    
    bool enabled;
} NoiseChannel;

typedef struct {
    PulseChannel pulse1;
    PulseChannel pulse2;
    TriangleChannel triangle;
    NoiseChannel noise;
    
    // frame counter
    u8 frame_counter_mode;  // 0 = 4-step, 1 = 5-step
    bool frame_irq_inhibit;
    u32 frame_counter;
    
    // output
    float output_sample;
    
    // sample buffer for SDL
    // we generate samples at cpu rate then downsample
    float sample_buffer[2048];
    int sample_index;
    int samples_per_frame;
    
    u64 cycle_count;
} APU;

// functions
void apu_init(APU *apu);
void apu_reset(APU *apu);
void apu_step(APU *apu);  // call once per CPU cycle
void apu_write(APU *apu, u16 addr, u8 val);
u8 apu_read(APU *apu, u16 addr);

// call this to get audio samples for SDL
float apu_get_sample(APU *apu);

#endif