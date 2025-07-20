#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include "bus.h"

// NES Emulator
// we have a ppu now! sorta

int main(int argc, char *argv[]) {
    printf("=== NES Emulator ===\n");
    printf("version 0.3.0 (ppu exists now)\n\n");
    
    if (argc < 2) {
        printf("Usage: nes <rom.nes>\n");
        return 1;
    }
    
    // init SDL
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    
    int scale = 3;
    SDL_Window *window = SDL_CreateWindow(
        "NES Emulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        256 * scale, 240 * scale,
        SDL_WINDOW_SHOWN
    );
    
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1,
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    
    SDL_Texture *texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        256, 240);
    
    // init emulator
    Bus bus;
    bus_init(&bus);
    cpu_set_bus(&bus);
    bus_load_cartridge(&bus, argv[1]);
    
    if (bus.cart.prg_rom == NULL) {
        printf("Failed to load ROM\n");
        return 1;
    }
    
    bus_reset(&bus);
    
    // main loop
    printf("\n--- running ---\n");
    bool running = true;
    SDL_Event event;
    
    while (running) {
        // handle events
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            }
        }
        
        // run one frame
        // NES runs at ~60fps, each frame is about 29780 cpu cycles
        // ppu runs 3x faster than cpu
        bus.ppu.frame_ready = false;
        while (!bus.ppu.frame_ready) {
            cpu_step(&bus.cpu);
            // ppu runs 3 cycles per cpu cycle
            ppu_step(&bus.ppu);
            ppu_step(&bus.ppu);
            ppu_step(&bus.ppu);
        }
        
        // display
        SDL_UpdateTexture(texture, NULL, bus.ppu.framebuffer, 256 * sizeof(u32));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
    }
    
    // cleanup
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    cartridge_free(&bus.cart);
    
    printf("bye!\n");
    return 0;
}