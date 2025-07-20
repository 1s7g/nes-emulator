#include "bus.h"
#include <stdio.h>
#include <string.h>

void bus_init(Bus *bus) {
    memset(bus->ram, 0, sizeof(bus->ram));
    cpu_init(&bus->cpu);
    ppu_init(&bus->ppu);
    printf("[BUS] initialized\n");
}

void bus_load_cartridge(Bus *bus, const char *filename) {
    if (cartridge_load(&bus->cart, filename)) {
        cartridge_print_info(&bus->cart);
        
        // give ppu access to chr rom
        bus->ppu.chr_rom = bus->cart.chr_rom;
        bus->ppu.chr_size = bus->cart.chr_size;
        bus->ppu.mirroring = bus->cart.mirroring;
    }
}

void bus_reset(Bus *bus) {
    ppu_reset(&bus->ppu);
    
    u8 lo = bus_read(bus, 0xFFFC);
    u8 hi = bus_read(bus, 0xFFFD);
    bus->cpu.pc = (hi << 8) | lo;
    
    bus->cpu.a = 0;
    bus->cpu.x = 0;
    bus->cpu.y = 0;
    bus->cpu.sp = 0xFD;
    bus->cpu.status = 0x24;
    bus->cpu.cycles = 0;
    
    printf("[BUS] reset - CPU PC set to 0x%04X\n", bus->cpu.pc);
}

u8 bus_read(Bus *bus, u16 addr) {
    if (addr < 0x2000) {
        return bus->ram[addr & 0x07FF];
    }
    else if (addr < 0x4000) {
        // PPU registers
        return ppu_read_register(&bus->ppu, addr);
    }
    else if (addr < 0x4020) {
        // APU and I/O
        // TODO
        return 0;
    }
    else {
        // cartridge
        if (addr >= 0x8000) {
            u32 mapped_addr = addr - 0x8000;
            if (bus->cart.prg_size == 16384) {
                mapped_addr = mapped_addr & 0x3FFF;
            }
            if (mapped_addr < bus->cart.prg_size) {
                return bus->cart.prg_rom[mapped_addr];
            }
        }
        return 0;
    }
}

void bus_write(Bus *bus, u16 addr, u8 val) {
    if (addr < 0x2000) {
        bus->ram[addr & 0x07FF] = val;
    }
    else if (addr < 0x4000) {
        // PPU registers
        ppu_write_register(&bus->ppu, addr, val);
    }
    else if (addr < 0x4020) {
        // APU and I/O
        // TODO
    }
    else {
        // cartridge - usually ROM
    }
}