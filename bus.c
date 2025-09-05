#include "bus.h"
#include <stdio.h>
#include <string.h>

void bus_init(Bus *bus) {
    memset(bus->ram, 0, sizeof(bus->ram));
    cpu_init(&bus->cpu);
    ppu_init(&bus->ppu);
    apu_init(&bus->apu);
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
        
        // default chr banks (no banking = just linear)
        bus->ppu.chr_bank_0 = 0x0000;
        bus->ppu.chr_bank_1 = 0x1000;
        
        // init mapper state
        if (bus->cart.mapper == 1) {
            printf("[BUS] mapper 1 (MMC1) detected, initializing\n");
            bus->cart.shift_reg = 0;
            bus->cart.shift_count = 0;
            bus->cart.mmc1_ctrl = 0x0C;  // power on default: prg mode 3
            bus->cart.mmc1_chr0 = 0;
            bus->cart.mmc1_chr1 = 0;
            bus->cart.mmc1_prg = 0;
            memset(bus->cart.prg_ram, 0, sizeof(bus->cart.prg_ram));
        }
    }
}

void bus_reset(Bus *bus) {
    ppu_reset(&bus->ppu);
    apu_reset(&bus->apu);
    
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
        return bus->ram[addr & 0x07FF];
    }
    // PPU regs
    else if (addr < 0x4000) {
        return ppu_read_register(&bus->ppu, addr);
    }
    // APU status
    else if (addr == 0x4015) {
        return apu_read(&bus->apu, addr);
    }
    else if (addr == 0x4016) {
        return controller_read(&bus->controller1);
    }
    else if (addr == 0x4017) {
        return controller_read(&bus->controller2);
    }
    else if (addr < 0x4020) {
        // other APU/IO - just return 0 for now
        return 0;
    }
    // PRG RAM ($6000-$7FFF)
    else if (addr >= 0x6000 && addr < 0x8000) {
        if (bus->cart.mapper == 1) {
            return bus->cart.prg_ram[addr - 0x6000];
        }
        return 0;
    }
    // cartridge PRG ROM ($8000-$FFFF)
    else if (addr >= 0x8000) {
        if (bus->cart.mapper == 0) {
            // NROM - simple, no banking
            u32 mapped_addr = addr - 0x8000;
            if (bus->cart.prg_size == 16384) {
                mapped_addr = mapped_addr % 16384;
            }
            if (mapped_addr < bus->cart.prg_size) {
                return bus->cart.prg_rom[mapped_addr];
            }
            return 0;
        }
        else if (bus->cart.mapper == 1) {
            // MMC1 - bank switched PRG ROM
            u8 prg_mode = (bus->cart.mmc1_ctrl >> 2) & 0x03;
            u8 bank = bus->cart.mmc1_prg & 0x0F;
            u32 num_banks = bus->cart.prg_size / 16384;
            
            if (num_banks == 0) num_banks = 1;
            
            if (prg_mode <= 1) {
                u32 base = ((bank & 0xFE) % num_banks) * 16384;
                return bus->cart.prg_rom[(base + (addr - 0x8000)) % bus->cart.prg_size];
            }
            else if (prg_mode == 2) {
                if (addr < 0xC000) {
                    return bus->cart.prg_rom[addr - 0x8000];
                } else {
                    u32 base = (bank % num_banks) * 16384;
                    return bus->cart.prg_rom[(base + (addr - 0xC000)) % bus->cart.prg_size];
                }
            }
            else {
                if (addr < 0xC000) {
                    u32 base = (bank % num_banks) * 16384;
                    return bus->cart.prg_rom[(base + (addr - 0x8000)) % bus->cart.prg_size];
                } else {
                    u32 base = (num_banks - 1) * 16384;
                    return bus->cart.prg_rom[(base + (addr - 0xC000)) % bus->cart.prg_size];
                }
            }
        }
    }
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
        // OAM DMA
        u16 base = val << 8;
        for (int i = 0; i < 256; i++) {
            bus->ppu.oam[i] = bus_read(bus, base + i);
        }
    }
    else if (addr == 0x4016) {
        controller_write(&bus->controller1, val);
        controller_write(&bus->controller2, val);
    }
    else if (addr >= 0x4000 && addr <= 0x4017) {
        // APU registers
        apu_write(&bus->apu, addr, val);
    }
    else if (addr < 0x4020) {
        // other IO, ignore
    }
    // PRG RAM ($6000-$7FFF)
    else if (addr >= 0x6000 && addr < 0x8000) {
        if (bus->cart.mapper == 1) {
            bus->cart.prg_ram[addr - 0x6000] = val;
        }
    }
    // mapper registers ($8000-$FFFF)
    else if (addr >= 0x8000) {
        if (bus->cart.mapper == 1) {
            if (val & 0x80) {
                bus->cart.shift_reg = 0;
                bus->cart.shift_count = 0;
                bus->cart.mmc1_ctrl |= 0x0C;
                return;
            }
            
            bus->cart.shift_reg |= ((val & 1) << bus->cart.shift_count);
            bus->cart.shift_count++;
            
            if (bus->cart.shift_count == 5) {
                u8 data = bus->cart.shift_reg;
                
                if (addr < 0xA000) {
                    bus->cart.mmc1_ctrl = data;
                    u8 mirror = data & 0x03;
                    if (mirror == 2) {
                        bus->cart.mirroring = 1;
                        bus->ppu.mirroring = 1;
                    } else if (mirror == 3) {
                        bus->cart.mirroring = 0;
                        bus->ppu.mirroring = 0;
                    }
                }
                else if (addr < 0xC000) {
                    bus->cart.mmc1_chr0 = data;
                }
                else if (addr < 0xE000) {
                    bus->cart.mmc1_chr1 = data;
                }
                else {
                    bus->cart.mmc1_prg = data & 0x0F;
                }
                
                bool chr_4k_mode = (bus->cart.mmc1_ctrl & 0x10) != 0;
                if (chr_4k_mode) {
                    bus->ppu.chr_bank_0 = bus->cart.mmc1_chr0 * 0x1000;
                    bus->ppu.chr_bank_1 = bus->cart.mmc1_chr1 * 0x1000;
                } else {
                    bus->ppu.chr_bank_0 = (bus->cart.mmc1_chr0 & 0x1E) * 0x1000;
                    bus->ppu.chr_bank_1 = bus->ppu.chr_bank_0 + 0x1000;
                }
                
                bus->cart.shift_reg = 0;
                bus->cart.shift_count = 0;
            }
        }
    }
}