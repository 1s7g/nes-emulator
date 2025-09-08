#include "cpu.h"
#include "bus.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// ok so the cpu needs access to the bus for memory reads/writes
// this is a circular dependency which is kinda ugly but whatever
// ill make it work by passing a void pointer and casting it
// there might be a better way to do this but im tired

static Bus *cpu_bus = NULL;

void cpu_set_bus(void *bus) {
    cpu_bus = (Bus*)bus;
}

// read/write through the bus
u8 cpu_read(CPU *cpu, u16 addr) {
    (void)cpu; // unused now, might remove from signature later
    if (cpu_bus) {
        return bus_read(cpu_bus, addr);
    }
    return 0;
}

void cpu_write(CPU *cpu, u16 addr, u8 val) {
    (void)cpu;
    if (cpu_bus) {
        bus_write(cpu_bus, addr, val);
    }
}

void cpu_init(CPU *cpu) {
    // zero out everything
    memset(cpu, 0, sizeof(CPU));
    
    printf("[CPU] initialized\n");
}

void cpu_reset(CPU *cpu) {
    // on reset, the 6502 reads the reset vector from 0xFFFC-0xFFFD
    // thats where the program starts
    cpu->a = 0;
    cpu->x = 0;
    cpu->y = 0;
    cpu->sp = 0xFD; // stack starts at 0xFD for some reason. dont ask me why
    cpu->status = 0x24; // flags start with unused and interrupt disable set
    
    // read reset vector
    u8 lo = cpu_read(cpu, 0xFFFC);
    u8 hi = cpu_read(cpu, 0xFFFD);
    cpu->pc = (hi << 8) | lo;
    
    cpu->cycles = 0;
    
    printf("[CPU] reset - PC set to 0x%04X\n", cpu->pc);
}

// flag helpers
void cpu_set_flag(CPU *cpu, u8 flag, bool value) {
    if (value) {
        cpu->status |= flag;
    } else {
        cpu->status &= ~flag;
    }
}

bool cpu_get_flag(CPU *cpu, u8 flag) {
    return (cpu->status & flag) != 0;
}

void cpu_update_zero_and_negative(CPU *cpu, u8 value) {
    cpu_set_flag(cpu, FLAG_Z, value == 0);
    cpu_set_flag(cpu, FLAG_N, value & 0x80); // bit 7 = negative
}

// stack operations (stack is at page 0x01, so 0x0100-0x01FF)
void cpu_push(CPU *cpu, u8 val) {
    cpu_write(cpu, 0x0100 + cpu->sp, val);
    cpu->sp--;
    // NOTE: stack grows downward. yeah its weird
}

u8 cpu_pop(CPU *cpu) {
    cpu->sp++;
    return cpu_read(cpu, 0x0100 + cpu->sp);
}

void cpu_push16(CPU *cpu, u16 val) {
    cpu_push(cpu, (val >> 8) & 0xFF); // high byte first
    cpu_push(cpu, val & 0xFF);        // then low byte
}

void cpu_nmi(CPU *cpu) {
    // NMI is like BRK but:
    // - doesn't set B flag
    // - reads vector from $FFFA instead of $FFFE
    cpu_push16(cpu, cpu->pc);
    cpu_push(cpu, (cpu->status | FLAG_U) & ~FLAG_B);
    cpu_set_flag(cpu, FLAG_I, true);
    
    u8 lo = cpu_read(cpu, 0xFFFA);
    u8 hi = cpu_read(cpu, 0xFFFB);
    cpu->pc = (hi << 8) | lo;
    
    cpu->cycles += 7;
}

u16 cpu_pop16(CPU *cpu) {
    u16 lo = cpu_pop(cpu);
    u16 hi = cpu_pop(cpu);
    return (hi << 8) | lo;
}

// i used this to dump memory when debugging the stack
// dont need it anymore but keeping it just in case
/*
void cpu_dump_stack(CPU *cpu) {
    printf("--- stack dump ---\n");
    for (int i = 0xFF; i >= cpu->sp; i--) {
        printf("  $01%02X: %02X\n", i, cpu_read(cpu, 0x0100 + i));
    }
    printf("--- end stack ---\n");
}
*/

void cpu_print_state(CPU *cpu) {
    printf("A:%02X X:%02X Y:%02X SP:%02X PC:%04X ", 
           cpu->a, cpu->x, cpu->y, cpu->sp, cpu->pc);
    printf("[%c%c_%c%c%c%c%c] cycles:%lu\n",
           cpu_get_flag(cpu, FLAG_N) ? 'N' : '.',
           cpu_get_flag(cpu, FLAG_V) ? 'V' : '.',
           cpu_get_flag(cpu, FLAG_B) ? 'B' : '.',
           cpu_get_flag(cpu, FLAG_D) ? 'D' : '.',
           cpu_get_flag(cpu, FLAG_I) ? 'I' : '.',
           cpu_get_flag(cpu, FLAG_Z) ? 'Z' : '.',
           cpu_get_flag(cpu, FLAG_C) ? 'C' : '.',
           (unsigned long)cpu->cycles);
}


// ============================================================
// ADDRESSING MODES
// the 6502 has like a million addressing modes and its annoying
// ============================================================

// immediate - value is the next byte
u16 addr_immediate(CPU *cpu) {
    return cpu->pc++;
}

// zero page - address is in zero page (0x0000-0x00FF)
u16 addr_zeropage(CPU *cpu) {
    u8 addr = cpu_read(cpu, cpu->pc++);
    return addr; // automatically in zero page since its u8
}

// zero page X - zero page + X register, wraps around
u16 addr_zeropage_x(CPU *cpu) {
    u8 addr = cpu_read(cpu, cpu->pc++);
    return (addr + cpu->x) & 0xFF; // wrap around in zero page
}

// zero page Y - same but with Y
u16 addr_zeropage_y(CPU *cpu) {
    u8 addr = cpu_read(cpu, cpu->pc++);
    return (addr + cpu->y) & 0xFF;
}

// absolute - full 16-bit address
u16 addr_absolute(CPU *cpu) {
    u8 lo = cpu_read(cpu, cpu->pc++);
    u8 hi = cpu_read(cpu, cpu->pc++);
    return (hi << 8) | lo;
}

// absolute X - absolute + X
u16 addr_absolute_x(CPU *cpu) {
    u8 lo = cpu_read(cpu, cpu->pc++);
    u8 hi = cpu_read(cpu, cpu->pc++);
    u16 addr = ((hi << 8) | lo) + cpu->x;
    // TODO: page crossing adds an extra cycle for some instructions
    // ill deal with that later
    return addr;
}

// absolute Y - absolute + Y
u16 addr_absolute_y(CPU *cpu) {
    u8 lo = cpu_read(cpu, cpu->pc++);
    u8 hi = cpu_read(cpu, cpu->pc++);
    u16 addr = ((hi << 8) | lo) + cpu->y;
    return addr;
}

// indirect - only used by JMP, reads address from address
u16 addr_indirect(CPU *cpu) {
    u8 lo = cpu_read(cpu, cpu->pc++);
    u8 hi = cpu_read(cpu, cpu->pc++);
    u16 ptr = (hi << 8) | lo;
    
    // theres a famous bug in the 6502 where if the pointer is at
    // the end of a page (xxFF), it wraps within the page instead
    // of crossing to the next page. lol
    if ((ptr & 0x00FF) == 0x00FF) {
        u8 a = cpu_read(cpu, ptr);
        u8 b = cpu_read(cpu, ptr & 0xFF00); // wraps to start of page!!
        return (b << 8) | a;
    } else {
        u8 a = cpu_read(cpu, ptr);
        u8 b = cpu_read(cpu, ptr + 1);
        return (b << 8) | a;
    }
}

// indexed indirect (X) - used with (addr,X) syntax
u16 addr_indexed_indirect(CPU *cpu) {
    u8 base = cpu_read(cpu, cpu->pc++);
    u8 ptr = (base + cpu->x) & 0xFF; // wraps in zero page
    u8 lo = cpu_read(cpu, ptr);
    u8 hi = cpu_read(cpu, (ptr + 1) & 0xFF);
    return (hi << 8) | lo;
}

// indirect indexed (Y) - used with (addr),Y syntax
// confusing name vs the one above, thanks 6502
u16 addr_indirect_indexed(CPU *cpu) {
    u8 ptr = cpu_read(cpu, cpu->pc++);
    u8 lo = cpu_read(cpu, ptr);
    u8 hi = cpu_read(cpu, (ptr + 1) & 0xFF);
    u16 addr = ((hi << 8) | lo) + cpu->y;
    // TODO: page crossing cycle penalty here too
    return addr;
}

// relative - for branches, signed offset from current PC
u16 addr_relative(CPU *cpu) {
    s8 offset = (s8)cpu_read(cpu, cpu->pc++);
    return cpu->pc + offset;
}


// ============================================================
// THE BIG ONE: execute an instruction
// this is gonna be a massive switch statement and i hate it
// ============================================================

void cpu_step(CPU *cpu) {
    u8 opcode = cpu_read(cpu, cpu->pc++);
    
    //printf("[CPU] executing opcode 0x%02X at PC=0x%04X\n", opcode, cpu->pc - 1);
    // ^ uncomment when debugging, will spam the console tho
    
    switch (opcode) {
        
        // ===== LDA - Load Accumulator =====
        case 0xA9: { // LDA immediate
            u16 addr = addr_immediate(cpu);
            cpu->a = cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 2;
            break;
        }
        case 0xA5: { // LDA zero page
            u16 addr = addr_zeropage(cpu);
            cpu->a = cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 3;
            break;
        }
        case 0xB5: { // LDA zero page X
            u16 addr = addr_zeropage_x(cpu);
            cpu->a = cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0xAD: { // LDA absolute
            u16 addr = addr_absolute(cpu);
            cpu->a = cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0xBD: { // LDA absolute X
            u16 addr = addr_absolute_x(cpu);
            cpu->a = cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 4; // +1 if page crossed, TODO
            break;
        }
        case 0xB9: { // LDA absolute Y
            u16 addr = addr_absolute_y(cpu);
            cpu->a = cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0xA1: { // LDA (indirect,X)
            u16 addr = addr_indexed_indirect(cpu);
            cpu->a = cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 6;
            break;
        }
        case 0xB1: { // LDA (indirect),Y
            u16 addr = addr_indirect_indexed(cpu);
            cpu->a = cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 5;
            break;
        }
        
        // ===== LDX - Load X Register =====
        case 0xA2: { // LDX immediate
            u16 addr = addr_immediate(cpu);
            cpu->x = cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->x);
            cpu->cycles += 2;
            break;
        }
        case 0xA6: { // LDX zero page
            u16 addr = addr_zeropage(cpu);
            cpu->x = cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->x);
            cpu->cycles += 3;
            break;
        }
        case 0xAE: { // LDX absolute
            u16 addr = addr_absolute(cpu);
            cpu->x = cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->x);
            cpu->cycles += 4;
            break;
        }

        // ===== LDY - Load Y Register =====
        case 0xA0: { // LDY immediate
            u16 addr = addr_immediate(cpu);
            cpu->y = cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->y);
            cpu->cycles += 2;
            break;
        }
        case 0xA4: { // LDY zero page
            u16 addr = addr_zeropage(cpu);
            cpu->y = cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->y);
            cpu->cycles += 3;
            break;
        }
        case 0xAC: { // LDY absolute
            u16 addr = addr_absolute(cpu);
            cpu->y = cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->y);
            cpu->cycles += 4;
            break;
        }

        // ===== STA - Store Accumulator =====
        case 0x85: { // STA zero page
            u16 addr = addr_zeropage(cpu);
            cpu_write(cpu, addr, cpu->a);
            cpu->cycles += 3;
            break;
        }
        case 0x95: { // STA zero page X
            u16 addr = addr_zeropage_x(cpu);
            cpu_write(cpu, addr, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0x8D: { // STA absolute
            u16 addr = addr_absolute(cpu);
            cpu_write(cpu, addr, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0x9D: { // STA absolute X
            u16 addr = addr_absolute_x(cpu);
            cpu_write(cpu, addr, cpu->a);
            cpu->cycles += 5;
            break;
        }
        case 0x99: { // STA absolute Y
            u16 addr = addr_absolute_y(cpu);
            cpu_write(cpu, addr, cpu->a);
            cpu->cycles += 5;
            break;
        }

        // ===== STX - Store X =====
        case 0x86: { // STX zero page
            u16 addr = addr_zeropage(cpu);
            cpu_write(cpu, addr, cpu->x);
            cpu->cycles += 3;
            break;
        }
        case 0x8E: { // STX absolute
            u16 addr = addr_absolute(cpu);
            cpu_write(cpu, addr, cpu->x);
            cpu->cycles += 4;
            break;
        }

        // ===== STY - Store Y =====
        case 0x84: { // STY zero page
            u16 addr = addr_zeropage(cpu);
            cpu_write(cpu, addr, cpu->y);
            cpu->cycles += 3;
            break;
        }
        case 0x8C: { // STY absolute
            u16 addr = addr_absolute(cpu);
            cpu_write(cpu, addr, cpu->y);
            cpu->cycles += 4;
            break;
        }

        // ===== Transfer instructions =====
        case 0xAA: // TAX
            cpu->x = cpu->a;
            cpu_update_zero_and_negative(cpu, cpu->x);
            cpu->cycles += 2;
            break;
        case 0xA8: // TAY
            cpu->y = cpu->a;
            cpu_update_zero_and_negative(cpu, cpu->y);
            cpu->cycles += 2;
            break;
        case 0x8A: // TXA
            cpu->a = cpu->x;
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 2;
            break;
        case 0x98: // TYA
            cpu->a = cpu->y;
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 2;
            break;
        case 0xBA: // TSX
            cpu->x = cpu->sp;
            cpu_update_zero_and_negative(cpu, cpu->x);
            cpu->cycles += 2;
            break;
        case 0x9A: // TXS
            cpu->sp = cpu->x;
            cpu->cycles += 2;
            break;

        // ===== Flag instructions =====
        case 0x18: // CLC
            cpu_set_flag(cpu, FLAG_C, false);
            cpu->cycles += 2;
            break;
        case 0x38: // SEC
            cpu_set_flag(cpu, FLAG_C, true);
            cpu->cycles += 2;
            break;
        case 0x58: // CLI
            cpu_set_flag(cpu, FLAG_I, false);
            cpu->cycles += 2;
            break;
        case 0x78: // SEI
            cpu_set_flag(cpu, FLAG_I, true);
            cpu->cycles += 2;
            break;
        case 0xD8: // CLD
            cpu_set_flag(cpu, FLAG_D, false);
            cpu->cycles += 2;
            break;
        case 0xF8: // SED
            cpu_set_flag(cpu, FLAG_D, true);
            cpu->cycles += 2;
            break;
        case 0xB8: // CLV
            cpu_set_flag(cpu, FLAG_V, false);
            cpu->cycles += 2;
            break;

        // ===== INC/DEC =====
        case 0xE8: // INX
            cpu->x++;
            cpu_update_zero_and_negative(cpu, cpu->x);
            cpu->cycles += 2;
            break;
        case 0xC8: // INY
            cpu->y++;
            cpu_update_zero_and_negative(cpu, cpu->y);
            cpu->cycles += 2;
            break;
        case 0xCA: // DEX
            cpu->x--;
            cpu_update_zero_and_negative(cpu, cpu->x);
            cpu->cycles += 2;
            break;
        case 0x88: // DEY
            cpu->y--;
            cpu_update_zero_and_negative(cpu, cpu->y);
            cpu->cycles += 2;
            break;

        // ===== NOP =====
        case 0xEA: // NOP - does nothing, my favorite instruction
            cpu->cycles += 2;
            break;

        // ===== JMP =====
        case 0x4C: { // JMP absolute
            u16 addr = addr_absolute(cpu);
            cpu->pc = addr;
            cpu->cycles += 3;
            break;
        }
        case 0x6C: { // JMP indirect (the buggy one lol)
            u16 addr = addr_indirect(cpu);
            cpu->pc = addr;
            cpu->cycles += 5;
            break;
        }

        // ===== JSR/RTS =====
        case 0x20: { // JSR - jump to subroutine
            u16 addr = addr_absolute(cpu);
            cpu_push16(cpu, cpu->pc - 1); // push return address - 1
            cpu->pc = addr;
            cpu->cycles += 6;
            break;
        }
        case 0x60: { // RTS - return from subroutine
            cpu->pc = cpu_pop16(cpu) + 1;
            cpu->cycles += 6;
            break;
        }

        // ===== Stack operations =====
        case 0x48: // PHA - push accumulator
            cpu_push(cpu, cpu->a);
            cpu->cycles += 3;
            break;
        case 0x68: // PLA - pull accumulator
            cpu->a = cpu_pop(cpu);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        case 0x08: // PHP - push status
            cpu_push(cpu, cpu->status | FLAG_B | FLAG_U);
            cpu->cycles += 3;
            break;
        case 0x28: // PLP - pull status
            cpu->status = cpu_pop(cpu);
            cpu_set_flag(cpu, FLAG_B, false);
            cpu_set_flag(cpu, FLAG_U, true);
            cpu->cycles += 4;
            break;
        
        // ===== ADC - Add with Carry =====
        // this instruction is pain. overflow flag logic makes my brain hurt
        case 0x69: { // ADC immediate
            u16 addr = addr_immediate(cpu);
            u8 val = cpu_read(cpu, addr);
            u16 result = cpu->a + val + (cpu_get_flag(cpu, FLAG_C) ? 1 : 0);
            
            // overflow: set if sign of result differs from sign of both inputs
            // i had to read like 4 different explanations to understand this
            cpu_set_flag(cpu, FLAG_V, (~(cpu->a ^ val) & (cpu->a ^ result)) & 0x80);
            cpu_set_flag(cpu, FLAG_C, result > 0xFF);
            cpu->a = result & 0xFF;
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 2;
            break;
        }
        case 0x65: { // ADC zero page
            u16 addr = addr_zeropage(cpu);
            u8 val = cpu_read(cpu, addr);
            u16 result = cpu->a + val + (cpu_get_flag(cpu, FLAG_C) ? 1 : 0);
            cpu_set_flag(cpu, FLAG_V, (~(cpu->a ^ val) & (cpu->a ^ result)) & 0x80);
            cpu_set_flag(cpu, FLAG_C, result > 0xFF);
            cpu->a = result & 0xFF;
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 3;
            break;
        }
        case 0x6D: { // ADC absolute
            u16 addr = addr_absolute(cpu);
            u8 val = cpu_read(cpu, addr);
            u16 result = cpu->a + val + (cpu_get_flag(cpu, FLAG_C) ? 1 : 0);
            cpu_set_flag(cpu, FLAG_V, (~(cpu->a ^ val) & (cpu->a ^ result)) & 0x80);
            cpu_set_flag(cpu, FLAG_C, result > 0xFF);
            cpu->a = result & 0xFF;
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }

        // ===== SBC - Subtract with Carry =====
        // honestly just copied the ADC logic and inverted the value
        // apparently thats how the 6502 actually does it internally?
        case 0xE9: { // SBC immediate
            u16 addr = addr_immediate(cpu);
            u8 val = cpu_read(cpu, addr);
            u8 inverted = val ^ 0xFF; // ones complement, same as ~val but clearer maybe
            u16 result = cpu->a + inverted + (cpu_get_flag(cpu, FLAG_C) ? 1 : 0);
            cpu_set_flag(cpu, FLAG_V, (~(cpu->a ^ inverted) & (cpu->a ^ result)) & 0x80);
            cpu_set_flag(cpu, FLAG_C, result > 0xFF);
            cpu->a = result & 0xFF;
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 2;
            break;
        }
        case 0xE5: { // SBC zero page
            u16 addr = addr_zeropage(cpu);
            u8 val = cpu_read(cpu, addr);
            u8 inverted = val ^ 0xFF;
            u16 result = cpu->a + inverted + (cpu_get_flag(cpu, FLAG_C) ? 1 : 0);
            cpu_set_flag(cpu, FLAG_V, (~(cpu->a ^ inverted) & (cpu->a ^ result)) & 0x80);
            cpu_set_flag(cpu, FLAG_C, result > 0xFF);
            cpu->a = result & 0xFF;
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 3;
            break;
        }
        case 0xED: { // SBC absolute
            u16 addr = addr_absolute(cpu);
            u8 val = cpu_read(cpu, addr);
            u8 inverted = val ^ 0xFF;
            u16 result = cpu->a + inverted + (cpu_get_flag(cpu, FLAG_C) ? 1 : 0);
            cpu_set_flag(cpu, FLAG_V, (~(cpu->a ^ inverted) & (cpu->a ^ result)) & 0x80);
            cpu_set_flag(cpu, FLAG_C, result > 0xFF);
            cpu->a = result & 0xFF;
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }

        // ===== AND =====
        case 0x29: { // AND immediate
            u16 addr = addr_immediate(cpu);
            cpu->a &= cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 2;
            break;
        }
        case 0x25: { // AND zero page
            u16 addr = addr_zeropage(cpu);
            cpu->a &= cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 3;
            break;
        }
        case 0x2D: { // AND absolute
            u16 addr = addr_absolute(cpu);
            cpu->a &= cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }

        // ===== ORA - OR =====
        case 0x09: { // ORA immediate
            u16 addr = addr_immediate(cpu);
            cpu->a |= cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 2;
            break;
        }
        case 0x05: { // ORA zero page
            u16 addr = addr_zeropage(cpu);
            cpu->a |= cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 3;
            break;
        }
        case 0x0D: { // ORA absolute
            u16 addr = addr_absolute(cpu);
            cpu->a |= cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }

        // ===== EOR - XOR =====
        case 0x49: { // EOR immediate
            u16 addr = addr_immediate(cpu);
            cpu->a ^= cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 2;
            break;
        }
        case 0x45: { // EOR zero page
            u16 addr = addr_zeropage(cpu);
            cpu->a ^= cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 3;
            break;
        }
        case 0x4D: { // EOR absolute
            u16 addr = addr_absolute(cpu);
            cpu->a ^= cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }

        // ===== CMP - Compare Accumulator =====
        case 0xC9: { // CMP immediate
            u16 addr = addr_immediate(cpu);
            u8 val = cpu_read(cpu, addr);
            cpu_set_flag(cpu, FLAG_C, cpu->a >= val);
            cpu_set_flag(cpu, FLAG_Z, cpu->a == val);
            cpu_set_flag(cpu, FLAG_N, (cpu->a - val) & 0x80);
            cpu->cycles += 2;
            break;
        }
        case 0xC5: { // CMP zero page
            u16 addr = addr_zeropage(cpu);
            u8 val = cpu_read(cpu, addr);
            cpu_set_flag(cpu, FLAG_C, cpu->a >= val);
            cpu_set_flag(cpu, FLAG_Z, cpu->a == val);
            cpu_set_flag(cpu, FLAG_N, (cpu->a - val) & 0x80);
            cpu->cycles += 3;
            break;
        }
        case 0xCD: { // CMP absolute
            u16 addr = addr_absolute(cpu);
            u8 val = cpu_read(cpu, addr);
            cpu_set_flag(cpu, FLAG_C, cpu->a >= val);
            cpu_set_flag(cpu, FLAG_Z, cpu->a == val);
            cpu_set_flag(cpu, FLAG_N, (cpu->a - val) & 0x80);
            cpu->cycles += 4;
            break;
        }

        // ===== CPX - Compare X =====
        case 0xE0: { // CPX immediate
            u16 addr = addr_immediate(cpu);
            u8 val = cpu_read(cpu, addr);
            cpu_set_flag(cpu, FLAG_C, cpu->x >= val);
            cpu_set_flag(cpu, FLAG_Z, cpu->x == val);
            cpu_set_flag(cpu, FLAG_N, (cpu->x - val) & 0x80);
            cpu->cycles += 2;
            break;
        }
        case 0xE4: { // CPX zero page
            u16 addr = addr_zeropage(cpu);
            u8 val = cpu_read(cpu, addr);
            cpu_set_flag(cpu, FLAG_C, cpu->x >= val);
            cpu_set_flag(cpu, FLAG_Z, cpu->x == val);
            cpu_set_flag(cpu, FLAG_N, (cpu->x - val) & 0x80);
            cpu->cycles += 3;
            break;
        }

        // ===== CPY - Compare Y =====
        case 0xC0: { // CPY immediate
            u16 addr = addr_immediate(cpu);
            u8 val = cpu_read(cpu, addr);
            cpu_set_flag(cpu, FLAG_C, cpu->y >= val);
            cpu_set_flag(cpu, FLAG_Z, cpu->y == val);
            cpu_set_flag(cpu, FLAG_N, (cpu->y - val) & 0x80);
            cpu->cycles += 2;
            break;
        }
        case 0xC4: { // CPY zero page
            u16 addr = addr_zeropage(cpu);
            u8 val = cpu_read(cpu, addr);
            cpu_set_flag(cpu, FLAG_C, cpu->y >= val);
            cpu_set_flag(cpu, FLAG_Z, cpu->y == val);
            cpu_set_flag(cpu, FLAG_N, (cpu->y - val) & 0x80);
            cpu->cycles += 3;
            break;
        }

        // ===== BRANCHES =====
        // all branches work the same way: check flag, jump if condition met
        case 0x10: { // BPL - branch if positive (N=0)
            u16 addr = addr_relative(cpu);
            if (!cpu_get_flag(cpu, FLAG_N)) {
                cpu->pc = addr;
                cpu->cycles++; // +1 for taking the branch
            }
            cpu->cycles += 2;
            break;
        }
        case 0x30: { // BMI - branch if minus (N=1)
            u16 addr = addr_relative(cpu);
            if (cpu_get_flag(cpu, FLAG_N)) {
                cpu->pc = addr;
                cpu->cycles++;
            }
            cpu->cycles += 2;
            break;
        }
        case 0x50: { // BVC - branch if overflow clear
            u16 addr = addr_relative(cpu);
            if (!cpu_get_flag(cpu, FLAG_V)) {
                cpu->pc = addr;
                cpu->cycles++;
            }
            cpu->cycles += 2;
            break;
        }
        case 0x70: { // BVS - branch if overflow set
            u16 addr = addr_relative(cpu);
            if (cpu_get_flag(cpu, FLAG_V)) {
                cpu->pc = addr;
                cpu->cycles++;
            }
            cpu->cycles += 2;
            break;
        }
        case 0x90: { // BCC - branch if carry clear
            u16 addr = addr_relative(cpu);
            if (!cpu_get_flag(cpu, FLAG_C)) {
                cpu->pc = addr;
                cpu->cycles++;
            }
            cpu->cycles += 2;
            break;
        }
        case 0xB0: { // BCS - branch if carry set
            u16 addr = addr_relative(cpu);
            if (cpu_get_flag(cpu, FLAG_C)) {
                cpu->pc = addr;
                cpu->cycles++;
            }
            cpu->cycles += 2;
            break;
        }
        case 0xD0: { // BNE - branch if not equal (Z=0)
            u16 addr = addr_relative(cpu);
            if (!cpu_get_flag(cpu, FLAG_Z)) {
                cpu->pc = addr;
                cpu->cycles++;
            }
            cpu->cycles += 2;
            break;
        }
        case 0xF0: { // BEQ - branch if equal (Z=1)
            u16 addr = addr_relative(cpu);
            if (cpu_get_flag(cpu, FLAG_Z)) {
                cpu->pc = addr;
                cpu->cycles++;
            }
            cpu->cycles += 2;
            break;
        }

        // ===== INC/DEC memory =====
        case 0xE6: { // INC zero page
            u16 addr = addr_zeropage(cpu);
            u8 val = cpu_read(cpu, addr) + 1;
            cpu_write(cpu, addr, val);
            cpu_update_zero_and_negative(cpu, val);
            cpu->cycles += 5;
            break;
        }
        case 0xEE: { // INC absolute
            u16 addr = addr_absolute(cpu);
            u8 val = cpu_read(cpu, addr) + 1;
            cpu_write(cpu, addr, val);
            cpu_update_zero_and_negative(cpu, val);
            cpu->cycles += 6;
            break;
        }
        case 0xC6: { // DEC zero page
            u16 addr = addr_zeropage(cpu);
            u8 val = cpu_read(cpu, addr) - 1;
            cpu_write(cpu, addr, val);
            cpu_update_zero_and_negative(cpu, val);
            cpu->cycles += 5;
            break;
        }
        case 0xCE: { // DEC absolute
            u16 addr = addr_absolute(cpu);
            u8 val = cpu_read(cpu, addr) - 1;
            cpu_write(cpu, addr, val);
            cpu_update_zero_and_negative(cpu, val);
            cpu->cycles += 6;
            break;
        }

        // ===== SHIFTS AND ROTATES =====
        case 0x0A: { // ASL accumulator
            cpu_set_flag(cpu, FLAG_C, cpu->a & 0x80);
            cpu->a <<= 1;
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 2;
            break;
        }
        case 0x06: { // ASL zero page
            u16 addr = addr_zeropage(cpu);
            u8 val = cpu_read(cpu, addr);
            cpu_set_flag(cpu, FLAG_C, val & 0x80);
            val <<= 1;
            cpu_write(cpu, addr, val);
            cpu_update_zero_and_negative(cpu, val);
            cpu->cycles += 5;
            break;
        }
        case 0x4A: { // LSR accumulator
            cpu_set_flag(cpu, FLAG_C, cpu->a & 0x01);
            cpu->a >>= 1;
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 2;
            break;
        }
        case 0x46: { // LSR zero page
            u16 addr = addr_zeropage(cpu);
            u8 val = cpu_read(cpu, addr);
            cpu_set_flag(cpu, FLAG_C, val & 0x01);
            val >>= 1;
            cpu_write(cpu, addr, val);
            cpu_update_zero_and_negative(cpu, val);
            cpu->cycles += 5;
            break;
        }
        case 0x2A: { // ROL accumulator
            u8 old_carry = cpu_get_flag(cpu, FLAG_C) ? 1 : 0;
            cpu_set_flag(cpu, FLAG_C, cpu->a & 0x80);
            cpu->a = (cpu->a << 1) | old_carry;
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 2;
            break;
        }
        case 0x26: { // ROL zero page
            u16 addr = addr_zeropage(cpu);
            u8 val = cpu_read(cpu, addr);
            u8 old_carry = cpu_get_flag(cpu, FLAG_C) ? 1 : 0;
            cpu_set_flag(cpu, FLAG_C, val & 0x80);
            val = (val << 1) | old_carry;
            cpu_write(cpu, addr, val);
            cpu_update_zero_and_negative(cpu, val);
            cpu->cycles += 5;
            break;
        }
        case 0x6A: { // ROR accumulator
            u8 old_carry = cpu_get_flag(cpu, FLAG_C) ? 0x80 : 0;
            cpu_set_flag(cpu, FLAG_C, cpu->a & 0x01);
            cpu->a = (cpu->a >> 1) | old_carry;
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 2;
            break;
        }
        case 0x66: { // ROR zero page
            u16 addr = addr_zeropage(cpu);
            u8 val = cpu_read(cpu, addr);
            u8 old_carry = cpu_get_flag(cpu, FLAG_C) ? 0x80 : 0;
            cpu_set_flag(cpu, FLAG_C, val & 0x01);
            val = (val >> 1) | old_carry;
            cpu_write(cpu, addr, val);
            cpu_update_zero_and_negative(cpu, val);
            cpu->cycles += 5;
            break;
        }

        // ===== BIT =====
        case 0x24: { // BIT zero page
            u16 addr = addr_zeropage(cpu);
            u8 val = cpu_read(cpu, addr);
            cpu_set_flag(cpu, FLAG_Z, (cpu->a & val) == 0);
            cpu_set_flag(cpu, FLAG_V, val & 0x40);
            cpu_set_flag(cpu, FLAG_N, val & 0x80);
            cpu->cycles += 3;
            break;
        }
        case 0x2C: { // BIT absolute
            u16 addr = addr_absolute(cpu);
            u8 val = cpu_read(cpu, addr);
            cpu_set_flag(cpu, FLAG_Z, (cpu->a & val) == 0);
            cpu_set_flag(cpu, FLAG_V, val & 0x40);
            cpu_set_flag(cpu, FLAG_N, val & 0x80);
            cpu->cycles += 4;
            break;
        }

        // ===== BRK and RTI =====
        case 0x00: { // BRK
            cpu->pc++; // BRK skips the next byte (padding byte)
            cpu_push16(cpu, cpu->pc);
            cpu_push(cpu, cpu->status | FLAG_B | FLAG_U);
            cpu_set_flag(cpu, FLAG_I, true);
            u8 lo = cpu_read(cpu, 0xFFFE);
            u8 hi = cpu_read(cpu, 0xFFFF);
            cpu->pc = (hi << 8) | lo;
            cpu->cycles += 7;
            break;
        }
        case 0x40: { // RTI - return from interrupt
            cpu->status = cpu_pop(cpu);
            cpu_set_flag(cpu, FLAG_B, false);
            cpu_set_flag(cpu, FLAG_U, true);
            cpu->pc = cpu_pop16(cpu);
            cpu->cycles += 6;
            break;
        }
                // ===== More LDA addressing modes =====
        // (we had most of these but lets make sure)
        
        // ===== More LDX addressing modes =====
        case 0xB6: { // LDX zero page Y
            u16 addr = addr_zeropage_y(cpu);
            cpu->x = cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->x);
            cpu->cycles += 4;
            break;
        }
        case 0xBE: { // LDX absolute Y
            u16 addr = addr_absolute_y(cpu);
            cpu->x = cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->x);
            cpu->cycles += 4; // TODO: +1 if page boundary crossed
            break;
        }

        // ===== More LDY addressing modes =====
        case 0xB4: { // LDY zero page X
            u16 addr = addr_zeropage_x(cpu);
            cpu->y = cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->y);
            cpu->cycles += 4;
            break;
        }
        case 0xBC: { // LDY absolute X
            u16 addr = addr_absolute_x(cpu);
            cpu->y = cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->y);
            cpu->cycles += 4;
            break;
        }
        
        // TODO: theres more LDY addressing modes i think?
        // havent needed them yet so skipping for now

        // ===== More STA addressing modes =====
        case 0x81: { // STA (indirect,X)
            u16 addr = addr_indexed_indirect(cpu);
            cpu_write(cpu, addr, cpu->a);
            cpu->cycles += 6;
            break;
        }
        case 0x91: { // STA (indirect),Y
            u16 addr = addr_indirect_indexed(cpu);
            cpu_write(cpu, addr, cpu->a);
            cpu->cycles += 6;
            break;
        }

        // ===== More STX =====
        case 0x96: { // STX zero page Y
            u16 addr = addr_zeropage_y(cpu);
            cpu_write(cpu, addr, cpu->x);
            cpu->cycles += 4;
            break;
        }

        // ===== More STY =====
        case 0x94: { // STY zero page X
            u16 addr = addr_zeropage_x(cpu);
            cpu_write(cpu, addr, cpu->y);
            cpu->cycles += 4;
            break;
        }

        // ===== More ADC =====
        case 0x75: { // ADC zero page X
            u16 addr = addr_zeropage_x(cpu);
            u8 val = cpu_read(cpu, addr);
            u16 result = cpu->a + val + (cpu_get_flag(cpu, FLAG_C) ? 1 : 0);
            cpu_set_flag(cpu, FLAG_V, (~(cpu->a ^ val) & (cpu->a ^ result)) & 0x80);
            cpu_set_flag(cpu, FLAG_C, result > 0xFF);
            cpu->a = result & 0xFF;
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0x7D: { // ADC absolute X
            u16 addr = addr_absolute_x(cpu);
            u8 val = cpu_read(cpu, addr);
            u16 result = cpu->a + val + (cpu_get_flag(cpu, FLAG_C) ? 1 : 0);
            cpu_set_flag(cpu, FLAG_V, (~(cpu->a ^ val) & (cpu->a ^ result)) & 0x80);
            cpu_set_flag(cpu, FLAG_C, result > 0xFF);
            cpu->a = result & 0xFF;
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0x79: { // ADC absolute Y
            u16 addr = addr_absolute_y(cpu);
            u8 val = cpu_read(cpu, addr);
            u16 result = cpu->a + val + (cpu_get_flag(cpu, FLAG_C) ? 1 : 0);
            cpu_set_flag(cpu, FLAG_V, (~(cpu->a ^ val) & (cpu->a ^ result)) & 0x80);
            cpu_set_flag(cpu, FLAG_C, result > 0xFF);
            cpu->a = result & 0xFF;
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0x61: { // ADC (indirect,X)
            u16 addr = addr_indexed_indirect(cpu);
            u8 val = cpu_read(cpu, addr);
            u16 result = cpu->a + val + (cpu_get_flag(cpu, FLAG_C) ? 1 : 0);
            cpu_set_flag(cpu, FLAG_V, (~(cpu->a ^ val) & (cpu->a ^ result)) & 0x80);
            cpu_set_flag(cpu, FLAG_C, result > 0xFF);
            cpu->a = result & 0xFF;
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 6;
            break;
        }
        case 0x71: { // ADC (indirect),Y
            u16 addr = addr_indirect_indexed(cpu);
            u8 val = cpu_read(cpu, addr);
            u16 result = cpu->a + val + (cpu_get_flag(cpu, FLAG_C) ? 1 : 0);
            cpu_set_flag(cpu, FLAG_V, (~(cpu->a ^ val) & (cpu->a ^ result)) & 0x80);
            cpu_set_flag(cpu, FLAG_C, result > 0xFF);
            cpu->a = result & 0xFF;
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 5;
            break;
        }

        // ===== More SBC =====
        case 0xF5: { // SBC zero page X
            u16 addr = addr_zeropage_x(cpu);
            u8 val = cpu_read(cpu, addr);
            u8 inv = val ^ 0xFF;
            u16 result = cpu->a + inv + (cpu_get_flag(cpu, FLAG_C) ? 1 : 0);
            cpu_set_flag(cpu, FLAG_V, (~(cpu->a ^ inv) & (cpu->a ^ result)) & 0x80);
            cpu_set_flag(cpu, FLAG_C, result > 0xFF);
            cpu->a = result & 0xFF;
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0xFD: { // SBC absolute X
            u16 addr = addr_absolute_x(cpu);
            u8 val = cpu_read(cpu, addr);
            u8 inv = val ^ 0xFF;
            u16 result = cpu->a + inv + (cpu_get_flag(cpu, FLAG_C) ? 1 : 0);
            cpu_set_flag(cpu, FLAG_V, (~(cpu->a ^ inv) & (cpu->a ^ result)) & 0x80);
            cpu_set_flag(cpu, FLAG_C, result > 0xFF);
            cpu->a = result & 0xFF;
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0xF9: { // SBC absolute Y
            u16 addr = addr_absolute_y(cpu);
            u8 val = cpu_read(cpu, addr);
            u8 inv = val ^ 0xFF;
            u16 result = cpu->a + inv + (cpu_get_flag(cpu, FLAG_C) ? 1 : 0);
            cpu_set_flag(cpu, FLAG_V, (~(cpu->a ^ inv) & (cpu->a ^ result)) & 0x80);
            cpu_set_flag(cpu, FLAG_C, result > 0xFF);
            cpu->a = result & 0xFF;
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0xE1: { // SBC (indirect,X)
            u16 addr = addr_indexed_indirect(cpu);
            u8 val = cpu_read(cpu, addr);
            u8 inv = val ^ 0xFF;
            u16 result = cpu->a + inv + (cpu_get_flag(cpu, FLAG_C) ? 1 : 0);
            cpu_set_flag(cpu, FLAG_V, (~(cpu->a ^ inv) & (cpu->a ^ result)) & 0x80);
            cpu_set_flag(cpu, FLAG_C, result > 0xFF);
            cpu->a = result & 0xFF;
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 6;
            break;
        }
        case 0xF1: { // SBC (indirect),Y
            u16 addr = addr_indirect_indexed(cpu);
            u8 val = cpu_read(cpu, addr);
            u8 inv = val ^ 0xFF;
            u16 result = cpu->a + inv + (cpu_get_flag(cpu, FLAG_C) ? 1 : 0);
            cpu_set_flag(cpu, FLAG_V, (~(cpu->a ^ inv) & (cpu->a ^ result)) & 0x80);
            cpu_set_flag(cpu, FLAG_C, result > 0xFF);
            cpu->a = result & 0xFF;
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 5;
            break;
        }

        // ===== More AND =====
        case 0x35: { // AND zero page X
            u16 addr = addr_zeropage_x(cpu);
            cpu->a &= cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0x3D: { // AND absolute X
            u16 addr = addr_absolute_x(cpu);
            cpu->a &= cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0x39: { // AND absolute Y
            u16 addr = addr_absolute_y(cpu);
            cpu->a &= cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0x21: { // AND (indirect,X)
            u16 addr = addr_indexed_indirect(cpu);
            cpu->a &= cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 6;
            break;
        }
        case 0x31: { // AND (indirect),Y
            u16 addr = addr_indirect_indexed(cpu);
            cpu->a &= cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 5;
            break;
        }

        // ===== More ORA =====
        case 0x15: { // ORA zero page X
            u16 addr = addr_zeropage_x(cpu);
            cpu->a |= cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0x1D: { // ORA absolute X
            u16 addr = addr_absolute_x(cpu);
            cpu->a |= cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0x19: { // ORA absolute Y
            u16 addr = addr_absolute_y(cpu);
            cpu->a |= cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0x01: { // ORA (indirect,X)
            u16 addr = addr_indexed_indirect(cpu);
            cpu->a |= cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 6;
            break;
        }
        case 0x11: { // ORA (indirect),Y
            u16 addr = addr_indirect_indexed(cpu);
            cpu->a |= cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 5;
            break;
        }

        // ===== More EOR =====
        case 0x55: { // EOR zero page X
            u16 addr = addr_zeropage_x(cpu);
            cpu->a ^= cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0x5D: { // EOR absolute X
            u16 addr = addr_absolute_x(cpu);
            cpu->a ^= cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0x59: { // EOR absolute Y
            u16 addr = addr_absolute_y(cpu);
            cpu->a ^= cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 4;
            break;
        }
        case 0x41: { // EOR (indirect,X)
            u16 addr = addr_indexed_indirect(cpu);
            cpu->a ^= cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 6;
            break;
        }
        case 0x51: { // EOR (indirect),Y
            u16 addr = addr_indirect_indexed(cpu);
            cpu->a ^= cpu_read(cpu, addr);
            cpu_update_zero_and_negative(cpu, cpu->a);
            cpu->cycles += 5;
            break;
        }

        // ===== More CMP =====
        case 0xD5: { // CMP zero page X
            u16 addr = addr_zeropage_x(cpu);
            u8 val = cpu_read(cpu, addr);
            cpu_set_flag(cpu, FLAG_C, cpu->a >= val);
            cpu_set_flag(cpu, FLAG_Z, cpu->a == val);
            cpu_set_flag(cpu, FLAG_N, (cpu->a - val) & 0x80);
            cpu->cycles += 4;
            break;
        }
        case 0xDD: { // CMP absolute X
            u16 addr = addr_absolute_x(cpu);
            u8 val = cpu_read(cpu, addr);
            cpu_set_flag(cpu, FLAG_C, cpu->a >= val);
            cpu_set_flag(cpu, FLAG_Z, cpu->a == val);
            cpu_set_flag(cpu, FLAG_N, (cpu->a - val) & 0x80);
            cpu->cycles += 4;
            break;
        }
        case 0xD9: { // CMP absolute Y
            u16 addr = addr_absolute_y(cpu);
            u8 val = cpu_read(cpu, addr);
            cpu_set_flag(cpu, FLAG_C, cpu->a >= val);
            cpu_set_flag(cpu, FLAG_Z, cpu->a == val);
            cpu_set_flag(cpu, FLAG_N, (cpu->a - val) & 0x80);
            cpu->cycles += 4;
            break;
        }
        case 0xC1: { // CMP (indirect,X)
            u16 addr = addr_indexed_indirect(cpu);
            u8 val = cpu_read(cpu, addr);
            cpu_set_flag(cpu, FLAG_C, cpu->a >= val);
            cpu_set_flag(cpu, FLAG_Z, cpu->a == val);
            cpu_set_flag(cpu, FLAG_N, (cpu->a - val) & 0x80);
            cpu->cycles += 6;
            break;
        }
        case 0xD1: { // CMP (indirect),Y
            u16 addr = addr_indirect_indexed(cpu);
            u8 val = cpu_read(cpu, addr);
            cpu_set_flag(cpu, FLAG_C, cpu->a >= val);
            cpu_set_flag(cpu, FLAG_Z, cpu->a == val);
            cpu_set_flag(cpu, FLAG_N, (cpu->a - val) & 0x80);
            cpu->cycles += 5;
            break;
        }

        // ===== More CPX =====
        case 0xEC: { // CPX absolute
            u16 addr = addr_absolute(cpu);
            u8 val = cpu_read(cpu, addr);
            cpu_set_flag(cpu, FLAG_C, cpu->x >= val);
            cpu_set_flag(cpu, FLAG_Z, cpu->x == val);
            cpu_set_flag(cpu, FLAG_N, (cpu->x - val) & 0x80);
            cpu->cycles += 4;
            break;
        }

        // ===== More CPY =====
        case 0xCC: { // CPY absolute
            u16 addr = addr_absolute(cpu);
            u8 val = cpu_read(cpu, addr);
            cpu_set_flag(cpu, FLAG_C, cpu->y >= val);
            cpu_set_flag(cpu, FLAG_Z, cpu->y == val);
            cpu_set_flag(cpu, FLAG_N, (cpu->y - val) & 0x80);
            cpu->cycles += 4;
            break;
        }

        // ===== More INC/DEC =====
        case 0xF6: { // INC zero page X
            u16 addr = addr_zeropage_x(cpu);
            u8 val = cpu_read(cpu, addr) + 1;
            cpu_write(cpu, addr, val);
            cpu_update_zero_and_negative(cpu, val);
            cpu->cycles += 6;
            break;
        }
        case 0xFE: { // INC absolute X
            u16 addr = addr_absolute_x(cpu);
            u8 val = cpu_read(cpu, addr) + 1;
            cpu_write(cpu, addr, val);
            cpu_update_zero_and_negative(cpu, val);
            cpu->cycles += 7;
            break;
        }
        case 0xD6: { // DEC zero page X
            u16 addr = addr_zeropage_x(cpu);
            u8 val = cpu_read(cpu, addr) - 1;
            cpu_write(cpu, addr, val);
            cpu_update_zero_and_negative(cpu, val);
            cpu->cycles += 6;
            break;
        }
        case 0xDE: { // DEC absolute X
            u16 addr = addr_absolute_x(cpu);
            u8 val = cpu_read(cpu, addr) - 1;
            cpu_write(cpu, addr, val);
            cpu_update_zero_and_negative(cpu, val);
            cpu->cycles += 7;
            break;
        }

        // ===== More shifts =====
        case 0x16: { // ASL zero page X
            u16 addr = addr_zeropage_x(cpu);
            u8 val = cpu_read(cpu, addr);
            cpu_set_flag(cpu, FLAG_C, val & 0x80);
            val <<= 1;
            cpu_write(cpu, addr, val);
            cpu_update_zero_and_negative(cpu, val);
            cpu->cycles += 6;
            break;
        }
        case 0x0E: { // ASL absolute
            u16 addr = addr_absolute(cpu);
            u8 val = cpu_read(cpu, addr);
            cpu_set_flag(cpu, FLAG_C, val & 0x80);
            val <<= 1;
            cpu_write(cpu, addr, val);
            cpu_update_zero_and_negative(cpu, val);
            cpu->cycles += 6;
            break;
        }
        case 0x1E: { // ASL absolute X
            u16 addr = addr_absolute_x(cpu);
            u8 val = cpu_read(cpu, addr);
            cpu_set_flag(cpu, FLAG_C, val & 0x80);
            val <<= 1;
            cpu_write(cpu, addr, val);
            cpu_update_zero_and_negative(cpu, val);
            cpu->cycles += 7;
            break;
        }
        case 0x56: { // LSR zero page X
            u16 addr = addr_zeropage_x(cpu);
            u8 val = cpu_read(cpu, addr);
            cpu_set_flag(cpu, FLAG_C, val & 0x01);
            val >>= 1;
            cpu_write(cpu, addr, val);
            cpu_update_zero_and_negative(cpu, val);
            cpu->cycles += 6;
            break;
        }
        case 0x4E: { // LSR absolute
            u16 addr = addr_absolute(cpu);
            u8 val = cpu_read(cpu, addr);
            cpu_set_flag(cpu, FLAG_C, val & 0x01);
            val >>= 1;
            cpu_write(cpu, addr, val);
            cpu_update_zero_and_negative(cpu, val);
            cpu->cycles += 6;
            break;
        }
        case 0x5E: { // LSR absolute X
            u16 addr = addr_absolute_x(cpu);
            u8 val = cpu_read(cpu, addr);
            cpu_set_flag(cpu, FLAG_C, val & 0x01);
            val >>= 1;
            cpu_write(cpu, addr, val);
            cpu_update_zero_and_negative(cpu, val);
            cpu->cycles += 7;
            break;
        }
        case 0x36: { // ROL zero page X
            u16 addr = addr_zeropage_x(cpu);
            u8 val = cpu_read(cpu, addr);
            u8 old_c = cpu_get_flag(cpu, FLAG_C) ? 1 : 0;
            cpu_set_flag(cpu, FLAG_C, val & 0x80);
            val = (val << 1) | old_c;
            cpu_write(cpu, addr, val);
            cpu_update_zero_and_negative(cpu, val);
            cpu->cycles += 6;
            break;
        }
        case 0x2E: { // ROL absolute
            u16 addr = addr_absolute(cpu);
            u8 val = cpu_read(cpu, addr);
            u8 old_c = cpu_get_flag(cpu, FLAG_C) ? 1 : 0;
            cpu_set_flag(cpu, FLAG_C, val & 0x80);
            val = (val << 1) | old_c;
            cpu_write(cpu, addr, val);
            cpu_update_zero_and_negative(cpu, val);
            cpu->cycles += 6;
            break;
        }
        case 0x3E: { // ROL absolute X
            u16 addr = addr_absolute_x(cpu);
            u8 val = cpu_read(cpu, addr);
            u8 old_c = cpu_get_flag(cpu, FLAG_C) ? 1 : 0;
            cpu_set_flag(cpu, FLAG_C, val & 0x80);
            val = (val << 1) | old_c;
            cpu_write(cpu, addr, val);
            cpu_update_zero_and_negative(cpu, val);
            cpu->cycles += 7;
            break;
        }
        case 0x76: { // ROR zero page X
            u16 addr = addr_zeropage_x(cpu);
            u8 val = cpu_read(cpu, addr);
            u8 old_c = cpu_get_flag(cpu, FLAG_C) ? 0x80 : 0;
            cpu_set_flag(cpu, FLAG_C, val & 0x01);
            val = (val >> 1) | old_c;
            cpu_write(cpu, addr, val);
            cpu_update_zero_and_negative(cpu, val);
            cpu->cycles += 6;
            break;
        }
        case 0x6E: { // ROR absolute
            u16 addr = addr_absolute(cpu);
            u8 val = cpu_read(cpu, addr);
            u8 old_c = cpu_get_flag(cpu, FLAG_C) ? 0x80 : 0;
            cpu_set_flag(cpu, FLAG_C, val & 0x01);
            val = (val >> 1) | old_c;
            cpu_write(cpu, addr, val);
            cpu_update_zero_and_negative(cpu, val);
            cpu->cycles += 6;
            break;
        }
        case 0x7E: { // ROR absolute X
            u16 addr = addr_absolute_x(cpu);
            u8 val = cpu_read(cpu, addr);
            u8 old_c = cpu_get_flag(cpu, FLAG_C) ? 0x80 : 0;
            cpu_set_flag(cpu, FLAG_C, val & 0x01);
            val = (val >> 1) | old_c;
            cpu_write(cpu, addr, val);
            cpu_update_zero_and_negative(cpu, val);
            cpu->cycles += 7;
            break;
        }
        default:
            printf("[CPU] ERROR: unknown opcode 0x%02X at PC=0x%04X\n", 
                   opcode, cpu->pc - 1);
            printf("[CPU] im gonna crash now lol\n");
            cpu_print_state(cpu);
            exit(1);
            break;
    }
}