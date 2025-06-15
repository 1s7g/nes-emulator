#include <stdio.h>
#include <stdlib.h>

// NES Emulator
// started this project because i thought it would be "fun"
// we'll see about that lmao

// TODO: literally everything

int main(int argc, char *argv[]) {
    printf("=== NES Emulator ===\n");
    printf("version 0.0.0.0.0.1 (aka nothing works yet)\n\n");
    
    if (argc < 2) {
        printf("Usage: nes <rom_file>\n");
        printf("...but like, nothing works yet so dont bother\n");
        return 1;
    }

    char *rom_path = argv[1];
    printf("ROM file: %s\n", rom_path);
    printf("cool, now i just need to figure out how to actually load this thing\n");

    // ok so the plan is:
    // 1. load the rom
    // 2. emulate the cpu (6502)
    // 3. emulate the ppu (graphics)
    // 4. emulate the apu (sound)
    // 5. somehow make it all work together
    // 6. ???
    // 7. profit
    
    // lets try to at least open the file i guess
    FILE *f = fopen(rom_path, "rb");
    if (f == NULL) {
        printf("ERROR: couldnt open file '%s'\n", rom_path);
        printf("did you spell it right?? lol\n");
        return 1;
    }

    // just read the first few bytes to see if its a real NES rom
    // NES roms start with "NES" followed by 0x1A (the iNES header)
    unsigned char header[4];
    fread(header, 1, 4, f);
    
    if (header[0] == 'N' && header[1] == 'E' && header[2] == 'S' && header[3] == 0x1A) {
        printf("yoooo it's a valid NES rom!!\n");
    } else {
        printf("thats not a NES rom bro. header bytes: %02X %02X %02X %02X\n", 
               header[0], header[1], header[2], header[3]);
        fclose(f);
        return 1;
    }

    fclose(f);
    
    printf("\nok thats all i can do for now. more coming soon (tm)\n");
    
    return 0;
}