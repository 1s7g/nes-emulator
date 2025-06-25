#ifndef CARTRIDGE_H
#define CARTRIDGE_H

#include "types.h"

// iNES header format (16 bytes)
// bytes 0-3: "NES" + 0x1A
// byte 4: PRG ROM size in 16KB units
// byte 5: CHR ROM size in 8KB units (0 = uses CHR RAM)
// byte 6: flags 6 - mapper low nibble, mirroring, battery, trainer
// byte 7: flags 7 - mapper high nibble, vs/playchoice, nes 2.0
// bytes 8-15: rarely used, usually zero

typedef struct {
    u8 *prg_rom;      // program rom (cpu)
    u8 *chr_rom;      // character rom (ppu/graphics)
    u32 prg_size;     // in bytes
    u32 chr_size;     // in bytes
    
    u8 mapper;        // mapper number (theres like 200+ of these lol)
    u8 mirroring;     // 0 = horizontal, 1 = vertical
    bool has_battery; // has battery-backed save ram
    
    // might need more stuff later idk
} Cartridge;

// functions
bool cartridge_load(Cartridge *cart, const char *filename);
void cartridge_free(Cartridge *cart);
void cartridge_print_info(Cartridge *cart);

#endif