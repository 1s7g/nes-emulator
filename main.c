#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include "bus.h"

// NES Emulator
// now with controller input!

int main(int argc, char *argv[]) {
    printf("=== NES Emulator ===\n");
    printf("version 0.4.0 (controller support)\n\n");
    
    if (argc < 2) {
        printf("Usage: nes <rom.nes>\n");
        return 1;
    }
    
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
    
    Bus bus;
    bus_init(&bus);
    cpu_set_bus(&bus);
    bus_load_cartridge(&bus, argv[1]);
    
    if (bus.cart.prg_rom == NULL) {
        printf("Failed to load ROM\n");
        return 1;
    }
    
    bus_reset(&bus);
    
    printf("\n--- controls ---\n");
    printf("Arrow keys = D-pad\n");
    printf("Z = A button\n");
    printf("X = B button\n");
    printf("Enter = Start\n");
    printf("Shift = Select\n");
    printf("Escape = Quit\n");
    printf("----------------\n\n");
    
    bool running = true;
    SDL_Event event;
    
    while (running) {
        // handle input
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            }
        }
        
        // get keyboard state
        const u8 *keys = SDL_GetKeyboardState(NULL);
        u8 buttons = 0;
        
        if (keys[SDL_SCANCODE_Z])      buttons |= BTN_A;
        if (keys[SDL_SCANCODE_X])      buttons |= BTN_B;
        if (keys[SDL_SCANCODE_RSHIFT] || keys[SDL_SCANCODE_LSHIFT]) 
                                        buttons |= BTN_SELECT;
        if (keys[SDL_SCANCODE_RETURN]) buttons |= BTN_START;
        if (keys[SDL_SCANCODE_UP])     buttons |= BTN_UP;
        if (keys[SDL_SCANCODE_DOWN])   buttons |= BTN_DOWN;
        if (keys[SDL_SCANCODE_LEFT])   buttons |= BTN_LEFT;
        if (keys[SDL_SCANCODE_RIGHT])  buttons |= BTN_RIGHT;
        
        controller_set_buttons(&bus.controller1, buttons);
        
        // run one frame
        bus.ppu.frame_ready = false;
        while (!bus.ppu.frame_ready) {
            // check for NMI
            if (bus.ppu.nmi_triggered) {
                bus.ppu.nmi_triggered = false;
                cpu_nmi(&bus.cpu);
            }
            
            cpu_step(&bus.cpu);
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
    
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    cartridge_free(&bus.cart);
    
    printf("bye!\n");
    return 0;
}