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
        // ppu registers are mirrored every 8 bytes
        // i think? yeah $2000-$2007 repeated up to $3FFF
        return ppu_read_register(&bus->ppu, addr);
    }
    else if (addr == 0x4016) {
        return controller_read(&bus->controller1);
    }
    else if (addr == 0x4017) {
        return controller_read(&bus->controller2);
    }
    else if (addr < 0x4020) {
        // APU and IO registers
        // TODO: actually implement APU reads
        return 0;
    }
    // PRG RAM ($6000-$7FFF)
    else if (addr >= 0x6000 && addr < 0x8000) {
        if (bus->cart.mapper == 1) {
            return bus->cart.prg_ram[addr - 0x6000];
        }
        return 0; // no prg ram for mapper 0
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
            // this took a while to figure out, theres like 4 different modes
            u8 prg_mode = (bus->cart.mmc1_ctrl >> 2) & 0x03;
            u8 bank = bus->cart.mmc1_prg & 0x0F;
            u32 num_banks = bus->cart.prg_size / 16384;
            
            if (num_banks == 0) num_banks = 1; // shouldnt happen but just in case
            
            if (prg_mode <= 1) {
                // 32KB mode - switch both banks together, ignore low bit
                u32 base = ((bank & 0xFE) % num_banks) * 16384;
                return bus->cart.prg_rom[(base + (addr - 0x8000)) % bus->cart.prg_size];
            }
            else if (prg_mode == 2) {
                // first bank fixed to bank 0, switch bank at $C000
                if (addr < 0xC000) {
                    return bus->cart.prg_rom[addr - 0x8000];
                } else {
                    u32 base = (bank % num_banks) * 16384;
                    return bus->cart.prg_rom[(base + (addr - 0xC000)) % bus->cart.prg_size];
                }
            }
            else {
                // mode 3: switch bank at $8000, last bank fixed at $C000
                // this is the most common mode, zelda uses this
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
        // OAM DMA - copies 256 bytes from CPU memory to PPU OAM
        u16 base = val << 8;
        for (int i = 0; i < 256; i++) {
            bus->ppu.oam[i] = bus_read(bus, base + i);
        }
        // TODO: this should take 513 or 514 cpu cycles
        // skipping this for now, games seem fine without it
    }
    else if (addr == 0x4016) {
        controller_write(&bus->controller1, val);
        controller_write(&bus->controller2, val);
    }
    else if (addr < 0x4020) {
        // APU writes, not doing sound yet
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
            // MMC1 shift register - you write ONE BIT AT A TIME
            // what kind of psychopath designed this
            // bit 7 = reset, otherwise bit 0 goes into shift register
            // after 5 writes the value gets applied to a register
            
            if (val & 0x80) {
                // reset shift register
                bus->cart.shift_reg = 0;
                bus->cart.shift_count = 0;
                bus->cart.mmc1_ctrl |= 0x0C; // reset to prg mode 3
                return;
            }
            
            // shift in bit 0
            bus->cart.shift_reg |= ((val & 1) << bus->cart.shift_count);
            bus->cart.shift_count++;
            
            if (bus->cart.shift_count == 5) {
                // ok 5 bits collected, now figure out which register
                // based on the address of THIS write (the 5th one)
                u8 data = bus->cart.shift_reg;
                
                if (addr < 0xA000) {
                    // $8000-$9FFF = control register
                    bus->cart.mmc1_ctrl = data;
                    
                    // update mirroring
                    u8 mirror = data & 0x03;
                    if (mirror == 2) {
                        bus->cart.mirroring = 1; // vertical
                        bus->ppu.mirroring = 1;
                    } else if (mirror == 3) {
                        bus->cart.mirroring = 0; // horizontal
                        bus->ppu.mirroring = 0;
                    }
                    // TODO: mirror modes 0 and 1 are single-screen
                    // not sure how to handle that with our current mirroring setup
                }
                else if (addr < 0xC000) {
                    // $A000-$BFFF = CHR bank 0
                    bus->cart.mmc1_chr0 = data;
                }
                else if (addr < 0xE000) {
                    // $C000-$DFFF = CHR bank 1
                    bus->cart.mmc1_chr1 = data;
                }
                else {
                    // $E000-$FFFF = PRG bank
                    bus->cart.mmc1_prg = data & 0x0F;
                }
                
                // update chr bank offsets in ppu
                // theres two modes: 8KB (swap both together) or 4KB (swap independently)
                bool chr_4k_mode = (bus->cart.mmc1_ctrl & 0x10) != 0;
                if (chr_4k_mode) {
                    bus->ppu.chr_bank_0 = bus->cart.mmc1_chr0 * 0x1000;
                    bus->ppu.chr_bank_1 = bus->cart.mmc1_chr1 * 0x1000;
                } else {
                    // 8KB mode, low bit ignored
                    bus->ppu.chr_bank_0 = (bus->cart.mmc1_chr0 & 0x1E) * 0x1000;
                    bus->ppu.chr_bank_1 = bus->ppu.chr_bank_0 + 0x1000;
                }
                
                // reset shift register for next write sequence
                bus->cart.shift_reg = 0;
                bus->cart.shift_count = 0;
            }
        }
        // mapper 0 ignores writes to $8000+
    }
}