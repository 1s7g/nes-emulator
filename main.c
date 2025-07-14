#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include "bus.h"

// NES Emulator
// now with SDL! maybe i can actually see something on screen soon

int main(int argc, char *argv[]) {
    printf("=== NES Emulator ===\n");
    printf("version 0.2.0 (sdl works?)\n\n");
    
    // init SDL
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    printf("SDL initialized!\n");
    
    // NES resolution is 256x240
    // but thats tiny so lets scale it up
    int scale = 3;
    SDL_Window *window = SDL_CreateWindow(
        "NES Emulator",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        256 * scale, 240 * scale,
        SDL_WINDOW_SHOWN
    );
    
    if (!window) {
        printf("SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }
    
    SDL_Renderer *renderer = SDL_CreateRenderer(window, -1, 
        SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    
    if (!renderer) {
        printf("SDL_CreateRenderer failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    // create a texture for the NES screen (256x240 pixels)
    SDL_Texture *texture = SDL_CreateTexture(renderer,
        SDL_PIXELFORMAT_ARGB8888,
        SDL_TEXTUREACCESS_STREAMING,
        256, 240);
    
    if (!texture) {
        printf("SDL_CreateTexture failed: %s\n", SDL_GetError());
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }
    
    printf("window created! (256x240 scaled %dx)\n", scale);
    
    // for now just fill the screen with a color to prove it works
    u32 pixels[256 * 240];
    for (int i = 0; i < 256 * 240; i++) {
        // make a nice gradient or something idk
        int x = i % 256;
        int y = i / 256;
        pixels[i] = 0xFF000000 | (x << 16) | (y << 8) | ((x + y) & 0xFF);
    }
    
    SDL_UpdateTexture(texture, NULL, pixels, 256 * sizeof(u32));
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
    
    // wait for user to close window
    printf("showing test pattern, close window to exit\n");
    bool running = true;
    SDL_Event event;
    while (running) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            }
            if (event.type == SDL_KEYDOWN && event.key.keysym.sym == SDLK_ESCAPE) {
                running = false;
            }
        }
        SDL_Delay(16); // ~60fps, dont burn cpu
    }
    
    // cleanup
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    
    printf("bye!\n");
    return 0;
}