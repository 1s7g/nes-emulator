#include <stdio.h>
#include <stdlib.h>
#include "cpu.h"

// NES Emulator
// day 2 (or whatever) - more opcodes, testing branches

int main(int argc, char *argv[]) {
    printf("=== NES Emulator ===\n");
    printf("version 0.0.2 (more opcodes, still no rom loading)\n\n");
    
    CPU cpu;
    cpu_init(&cpu);
    
    // test program: count from 0 to 5 using a loop
    // this tests LDA, CMP, BNE, INX and a few others
    //
    // basically:
    //   LDX #$00     ; x = 0
    //   LDA #$05     ; a = 5
    //   STA $10      ; store 5 at addr $10 (our "target")
    //   loop:
    //     INX          ; x++
    //     CPX $10      ; compare x with target
    //     BNE loop     ; if not equal, keep going
    //   NOP            ; done!

    u8 test_program[] = {
        0xA2, 0x00,  // LDX #$00
        0xA9, 0x05,  // LDA #$05
        0x85, 0x10,  // STA $10
        // loop starts at offset 6 (0x8006)
        0xE8,        // INX
        0xE4, 0x10,  // CPX $10
        0xD0, 0xFB,  // BNE -5 (back to INX)
        0xEA,        // NOP - we made it!
    };
    
    cpu_load_program(&cpu, test_program, sizeof(test_program), 0x8000);
    cpu_load_program(&cpu, (u8[]){0x00, 0x80}, 2, 0xFFFC);
    
    cpu_reset(&cpu);
    
    printf("--- running loop test ---\n");
    // run enough steps for the loop to complete
    for (int i = 0; i < 25; i++) {
        cpu_print_state(&cpu);
        cpu_step(&cpu);
    }
    printf("--- done ---\n\n");
    
    printf("X register should be 0x05: got 0x%02X %s\n", 
           cpu.x, cpu.x == 0x05 ? "(PASS)" : "(FAIL!!)");
    
    return 0;
}