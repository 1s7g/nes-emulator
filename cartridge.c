#include "cartridge.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool cartridge_load(Cartridge *cart, const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        printf("[CART] ERROR: couldnt open file '%s'\n", filename);
        return false;
    }
    
    // read header
    u8 header[16];
    if (fread(header, 1, 16, f) != 16) {
        printf("[CART] ERROR: couldnt read header\n");
        fclose(f);
        return false;
    }
    
    // check magic bytes
    if (header[0] != 'N' || header[1] != 'E' || header[2] != 'S' || header[3] != 0x1A) {
        printf("[CART] ERROR: not a valid iNES file\n");
        printf("[CART] got bytes: %02X %02X %02X %02X\n", 
               header[0], header[1], header[2], header[3]);
        fclose(f);
        return false;
    }
    
    // pull out the important stuff from the header
    // bytes 4 and 5 are rom sizes
    u8 prg_banks = header[4];  // number of 16KB prg banks
    u8 chr_banks = header[5];  // number of 8KB chr banks (0 means chr ram)
    u8 flags6 = header[6];
    u8 flags7 = header[7];
    // bytes 8-15 are usually zero, ignoring them
    // NES 2.0 format uses them but whatever, not dealing with that now
    
    cart->prg_size = prg_banks * 16384;
    cart->chr_size = chr_banks * 8192;
    
    // mapper number is split across high nibble of flags6 and flags7
    // why they did it this way i have no idea
    cart->mapper = (flags7 & 0xF0) | (flags6 >> 4);
    cart->mirroring = flags6 & 0x01;  // bit 0: 0=horizontal 1=vertical
    cart->has_battery = (flags6 & 0x02) != 0;  // bit 1: battery backed ram
    
    // bit 2 of flags6 = trainer present (512 bytes before prg rom)
    // almost no games use this
    bool has_trainer = (flags6 & 0x04) != 0;
    
    // skip trainer if present (512 bytes, rarely used)
    if (has_trainer) {
        printf("[CART] skipping 512 byte trainer\n");
        fseek(f, 512, SEEK_CUR);
    }
    
    // allocate and read PRG ROM
    cart->prg_rom = (u8*)malloc(cart->prg_size);
    if (!cart->prg_rom) {
        printf("[CART] ERROR: failed to allocate PRG ROM\n");
        fclose(f);
        return false;
    }
    
    if (fread(cart->prg_rom, 1, cart->prg_size, f) != cart->prg_size) {
        printf("[CART] ERROR: failed to read PRG ROM\n");
        free(cart->prg_rom);
        fclose(f);
        return false;
    }
    
    // allocate and read CHR ROM (if present)
    if (cart->chr_size > 0) {
        cart->chr_rom = (u8*)malloc(cart->chr_size);
        if (!cart->chr_rom) {
            printf("[CART] ERROR: failed to allocate CHR ROM\n");
            free(cart->prg_rom);
            fclose(f);
            return false;
        }
        
        if (fread(cart->chr_rom, 1, cart->chr_size, f) != cart->chr_size) {
            printf("[CART] ERROR: failed to read CHR ROM\n");
            free(cart->prg_rom);
            free(cart->chr_rom);
            fclose(f);
            return false;
        }
    } else {
        // no CHR ROM means the game uses CHR RAM
        // allocate 8KB of RAM for it
        cart->chr_size = 8192;
        cart->chr_rom = (u8*)calloc(cart->chr_size, 1);
        printf("[CART] no CHR ROM, using 8KB CHR RAM\n");
    }
    
    fclose(f);
    
    printf("[CART] loaded '%s' successfully\n", filename);
    return true;
}

void cartridge_free(Cartridge *cart) {
    if (cart->prg_rom) {
        free(cart->prg_rom);
        cart->prg_rom = NULL;
    }
    if (cart->chr_rom) {
        free(cart->chr_rom);
        cart->chr_rom = NULL;
    }
}

void cartridge_print_info(Cartridge *cart) {
    printf("=== Cartridge Info ===\n");
    printf("PRG ROM: %d KB (%d bytes)\n", cart->prg_size / 1024, cart->prg_size);
    printf("CHR ROM: %d KB (%d bytes)\n", cart->chr_size / 1024, cart->chr_size);
    printf("Mapper: %d\n", cart->mapper);
    printf("Mirroring: %s\n", cart->mirroring ? "Vertical" : "Horizontal");
    printf("Battery: %s\n", cart->has_battery ? "Yes" : "No");
    
    // warn about unsupported mappers
    if (cart->mapper != 0 && cart->mapper != 1) {
        printf("WARNING: mapper %d not supported!!\n", cart->mapper);
        printf("only mappers 0 (NROM) and 1 (MMC1) work right now\n");
    }
    printf("======================\n");
}