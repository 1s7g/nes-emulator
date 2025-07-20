#ifndef BUS_H
#define BUS_H

#include "types.h"
#include "cpu.h"
#include "cartridge.h"
#include "ppu.h"

typedef struct Bus {
    CPU cpu;
    PPU ppu;
    Cartridge cart;
    
    u8 ram[2048];
    
} Bus;

void bus_init(Bus *bus);
void bus_load_cartridge(Bus *bus, const char *filename);
void bus_reset(Bus *bus);

u8 bus_read(Bus *bus, u16 addr);
void bus_write(Bus *bus, u16 addr, u8 val);

#endif