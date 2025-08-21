#ifndef CPU_H
#define CPU_H

#include "types.h"

// 6502 CPU Status flags
// layout is: NV-BDIZC
// N = negative, V = overflow, - = unused (always 1??), B = break
// D = decimal (NES doesnt use this but the flag exists for some reason)
// I = interrupt disable, Z = zero, C = carry
// i had to look this up like 5 times before i remembered it
#define FLAG_C 0x01  // carry - set when addition overflows or subtraction underflows
#define FLAG_Z 0x02  // zero - set when result is 0
#define FLAG_I 0x04  // interrupt disable - when set, ignores IRQ
#define FLAG_D 0x08  // decimal - unused on NES, whatever
#define FLAG_B 0x10  // break - set when BRK instruction fires
#define FLAG_U 0x20  // unused, always 1, annoying to deal with
#define FLAG_V 0x40  // overflow - signed overflow, harder to understand than carry
#define FLAG_N 0x80  // negative - copy of bit 7 of result

typedef struct {
    // registers
    u8 a;      // accumulator
    u8 x;      // X
    u8 y;      // Y
    u8 sp;     // stack pointer (starts at 0xFD on reset)
    u16 pc;    // program counter
    u8 status; // NV-BDIZC flags

    u64 cycles; // total cycles, using this for timing stuff
    // might need to track per-instruction cycles separately later?
    // some ppl on nesdev forums say you need cycle-accurate timing
    // but it works fine without it so far
} CPU;

// functions
void cpu_init(CPU *cpu);
void cpu_reset(CPU *cpu);
void cpu_step(CPU *cpu);
void cpu_print_state(CPU *cpu);
void cpu_set_bus(void *bus);
void cpu_nmi(CPU *cpu);

void cpu_set_flag(CPU *cpu, u8 flag, bool value);
bool cpu_get_flag(CPU *cpu, u8 flag);
void cpu_update_zero_and_negative(CPU *cpu, u8 value);

#endif