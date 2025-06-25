#ifndef BUS_H
#define BUS_H

#include "types.h"
#include "cpu.h"
#include "cartridge.h"

// the bus connects all the components together
// cpu reads/writes go through here and get routed to the right place

typedef struct {
    CPU cpu;
    Cartridge cart;
    
    u8 ram[2048];  // 2KB internal RAM (mirrored a bunch of times)
    
    // ppu will go here eventually
    // apu will go here eventually
    // controllers will go here eventually
    
} Bus;

void bus_init(Bus *bus);
void bus_load_cartridge(Bus *bus, const char *filename);
void bus_reset(Bus *bus);

u8 bus_read(Bus *bus, u16 addr);
void bus_write(Bus *bus, u16 addr, u8 val);

#endif