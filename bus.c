#include "bus.h"
#include <stdio.h>
#include <string.h>

void bus_init(Bus *bus) {
    memset(bus->ram, 0, sizeof(bus->ram));
    cpu_init(&bus->cpu);
    printf("[BUS] initialized\n");
}

void bus_load_cartridge(Bus *bus, const char *filename) {
    if (cartridge_load(&bus->cart, filename)) {
        cartridge_print_info(&bus->cart);
    }
}

void bus_reset(Bus *bus) {
    // the cpu needs to be able to read memory to get the reset vector
    // so we pass a pointer to the bus... but wait the cpu doesnt have that yet
    // TODO: this is getting messy, need to refactor
    
    // for now just manually set PC to the reset vector
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
    // NES memory map:
    // $0000-$07FF: 2KB internal RAM
    // $0800-$1FFF: mirrors of RAM (repeats every 2KB)
    // $2000-$2007: PPU registers
    // $2008-$3FFF: mirrors of PPU registers
    // $4000-$4017: APU and I/O registers
    // $4018-$401F: normally disabled
    // $4020-$FFFF: cartridge space (PRG ROM, PRG RAM, mappers)
    
    if (addr < 0x2000) {
        // RAM (with mirroring)
        return bus->ram[addr & 0x07FF];
    }
    else if (addr < 0x4000) {
        // PPU registers (mirrored every 8 bytes)
        u16 ppu_reg = 0x2000 + (addr & 0x0007);
        
        if (ppu_reg == 0x2002) {
            // HACK: fake the vblank flag so games dont get stuck
            // in their "wait for vblank" loops
            // real ppu will replace this later obviously
            // just toggle bit 7 based on cycle count or something
            static int fake_vblank_counter = 0;
            fake_vblank_counter++;
            if (fake_vblank_counter > 3) {
                fake_vblank_counter = 0;
                return 0x80; // vblank flag set
            }
        }
        
        return 0;
    }
    else if (addr < 0x4020) {
        // APU and I/O
        // TODO: implement APU and controllers
        return 0;
    }
    else {
        // cartridge space
        // for mapper 0 (NROM):
        // $8000-$BFFF: first 16KB of PRG ROM
        // $C000-$FFFF: last 16KB of PRG ROM (or mirror of first if only 16KB)
        
        if (addr >= 0x8000) {
            u32 mapped_addr = addr - 0x8000;
            
            // if only 16KB PRG ROM, mirror it
            if (bus->cart.prg_size == 16384) {
                mapped_addr = mapped_addr & 0x3FFF;
            }
            
            if (mapped_addr < bus->cart.prg_size) {
                return bus->cart.prg_rom[mapped_addr];
            }
        }
        
        // unmapped, return 0
        return 0;
    }
}

void bus_write(Bus *bus, u16 addr, u8 val) {
    if (addr < 0x2000) {
        // RAM (with mirroring)
        bus->ram[addr & 0x07FF] = val;
    }
    else if (addr < 0x4000) {
        // PPU registers
        // TODO
    }
    else if (addr < 0x4020) {
        // APU and I/O
        // TODO
    }
    else {
        // cartridge space - usually ROM so writes are ignored
        // some mappers have PRG RAM though
        // TODO: mapper support
    }
}