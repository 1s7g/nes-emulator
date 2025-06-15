#ifndef CPU_H
#define CPU_H

#include "types.h"

// 6502 CPU Status flags
// these are bits in the status register (P)
// the layout is: NV-BDIZC
// bit 5 is unused (always 1) which is kinda annoying but whatever
#define FLAG_C 0x01  // carry
#define FLAG_Z 0x02  // zero
#define FLAG_I 0x04  // interrupt disable
#define FLAG_D 0x08  // decimal mode (NES doesnt use this but the flag exists)
#define FLAG_B 0x10  // break
#define FLAG_U 0x20  // unused, always 1
#define FLAG_V 0x40  // overflow
#define FLAG_N 0x80  // negative

typedef struct {
    // registers
    u8 a;      // accumulator
    u8 x;      // index register x
    u8 y;      // index register y
    u8 sp;     // stack pointer
    u16 pc;    // program counter
    u8 status; // status register (the flags)

    // not really "registers" but we need to track these
    u64 cycles; // total cycles executed... might be useful later?? idk
    // TODO: do i need anything else here
} CPU;

// functions
void cpu_init(CPU *cpu);
void cpu_reset(CPU *cpu);
void cpu_step(CPU *cpu); // execute one instruction
void cpu_print_state(CPU *cpu); // for debugging, gonna need this A LOT

// helper stuff for flags
void cpu_set_flag(CPU *cpu, u8 flag, bool value);
bool cpu_get_flag(CPU *cpu, u8 flag);

// these two flags get set so often im making helpers
void cpu_update_zero_and_negative(CPU *cpu, u8 value);

#endif