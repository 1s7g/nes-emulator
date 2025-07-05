#include <stdio.h>
#include <stdlib.h>
#include "bus.h"

// NES Emulator
// finally loading real roms!!

int main(int argc, char *argv[]) {
    printf("=== NES Emulator ===\n");
    printf("version 0.1.0 (can load roms now!)\n\n");
    
    if (argc < 2) {
        printf("Usage: nes <rom_file.nes>\n");
        return 1;
    }
    
    Bus bus;
    bus_init(&bus);
    
    // connect cpu to bus
    cpu_set_bus(&bus);
    
    // load the rom
    bus_load_cartridge(&bus, argv[1]);
    
    // check if it loaded
    if (bus.cart.prg_rom == NULL) {
        printf("Failed to load ROM\n");
        return 1;
    }
    
    // reset everything
    bus_reset(&bus);
    
    // run some instructions to see if it works
    printf("\n--- running cpu ---\n");
    for (int i = 0; i < 50000; i++) {
        // only print every 5000 steps
        if (i % 5000 == 0 || i > 49990) {
            printf("[%d] ", i);
            cpu_print_state(&bus.cpu);
        }
        cpu_step(&bus.cpu);
    }
    printf("--- done ---\n");
    
    // cleanup
    cartridge_free(&bus.cart);
    
    return 0;
}