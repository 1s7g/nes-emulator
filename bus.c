#include "bus.h"
#include <stdio.h>
#include <string.h>

void bus_init(Bus *bus) {
    memset(bus->ram, 0, sizeof(bus->ram));
    cpu_init(&bus->cpu);
    ppu_init(&bus->ppu);
    controller_init(&bus->controller1);
    controller_init(&bus->controller2);
    printf("[BUS] initialized\n");
}

void bus_load_cartridge(Bus *bus, const char *filename) {
    if (cartridge_load(&bus->cart, filename)) {
        cartridge_print_info(&bus->cart);
        
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
    // RAM - first 2KB mirrored 4 times
    if (addr < 0x2000) {
        return bus->ram[addr & 0x07FF]; // mirror every 2KB
    }
    // PPU regs
    else if (addr < 0x4000) {
        // ppu registers are mirrored every 8 bytes
        // i think? yeah $2000-$2007 repeated up to $3FFF
        return ppu_read_register(&bus->ppu, addr);
    }
    else if (addr == 0x4016) {
        // controller 1
        return controller_read(&bus->controller1);
    }
    else if (addr == 0x4017) {
        // controller 2
        return controller_read(&bus->controller2);
    }
    else if (addr < 0x4020) {
        // APU and IO registers
        // TODO: actually implement APU reads
        // printf("[BUS] read from APU/IO register $%04X\n", addr);
        return 0;
    }
    // cartridge space
    else if (addr >= 0x8000) {
        u32 mapped_addr = addr - 0x8000;
        // NROM: 16KB games mirror into both banks
        // 32KB games fill the whole space
        if (bus->cart.prg_size == 16384) {
            mapped_addr = mapped_addr % 16384;  // used to have & 0x3FFF here but this is clearer i think??
        }
        if (mapped_addr < bus->cart.prg_size) {
            return bus->cart.prg_rom[mapped_addr];
        }
        return 0; // shouldnt get here
    }
    // unmapped address space ($4020-$7FFF)
    // some mappers put stuff here but we dont support that yet
    return 0;
}

void bus_write(Bus *bus, u16 addr, u8 val) {
    if (addr < 0x2000) {
        bus->ram[addr & 0x07FF] = val;
    }
    else if (addr < 0x4000) {
        ppu_write_register(&bus->ppu, addr, val);
    }
    else if (addr == 0x4014) {
        // OAM DMA - copies 256 bytes from CPU memory to PPU OAM
        // this is how games load sprite data
        u16 base = val << 8; // val = page number, so $02 means copy from $0200
        for (int i = 0; i < 256; i++) {
            bus->ppu.oam[i] = bus_read(bus, base + i);
        }
        // TODO: this should take 513 or 514 cpu cycles
        // every other cpu cycle gets stolen or something
        // skipping this for now, games seem fine without it
        // bus->cpu.cycles += 513;
    }
    else if (addr == 0x4016) {
        controller_write(&bus->controller1, val);
        controller_write(&bus->controller2, val);
    }
    else if (addr < 0x4020) {
        // APU writes go here
        // theres a bunch of them ($4000-$4013, $4015, $4017)
        // not doing sound yet so just eat the write
    }
    // writes to $4020+ go to cartridge
    // mapper would handle this but we dont have mapper support lol
}