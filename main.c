#include <stdio.h>
#include <stdlib.h>
#include <SDL2/SDL.h>
#include "bus.h"

// NES Emulator
// finally adding sound, this is gonna be fun

// audio callback for SDL
void audio_callback(void *userdata, u8 *stream, int len) {
    APU *apu = (APU*)userdata;
    float *out = (float*)stream;
    int samples = len / sizeof(float);
    
    // this is probably not the best way to do this
    // should use a proper ring buffer but this works for now
    for (int i = 0; i < samples; i++) {
        if (apu->sample_index > 0) {
            out[i] = apu->sample_buffer[0];
            // shift everything down, yeah its slow but whatever
            for (int j = 0; j < apu->sample_index - 1; j++) {
                apu->sample_buffer[j] = apu->sample_buffer[j + 1];
            }
            apu->sample_index--;
        } else {
            out[i] = 0;
        }
    }
}

int main(int argc, char *argv[]) {
    printf("=== NES Emulator ===\n");
    printf("version 0.5.0 (sound support!)\n\n");
    
    if (argc < 2) {
        printf("Usage: nes <rom.nes>\n");
        return 1;
    }
    
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
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
    
    // setup audio
    // took me a while to figure out SDL audio, its kinda weird
    SDL_AudioSpec want, have;
    SDL_memset(&want, 0, sizeof(want));
    want.freq = 44100;
    want.format = AUDIO_F32SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = audio_callback;
    want.userdata = &bus.apu;
    
    SDL_AudioDeviceID audio_dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (audio_dev == 0) {
        printf("[AUDIO] failed to open audio: %s\n", SDL_GetError());
        printf("[AUDIO] continuing without sound\n");
    } else {
        printf("[AUDIO] opened audio device, freq=%d\n", have.freq);
        SDL_PauseAudioDevice(audio_dev, 0);  // start playing
    }
    
    printf("\n--- controls ---\n");
    printf("Arrow keys / WASD = D-pad\n");
    printf("Z = A button\n");
    printf("X = B button\n");
    printf("Enter = Start\n");
    printf("Shift = Select\n");
    printf("Escape = Quit\n");
    printf("----------------\n\n");
    
    // fps counter stuff
    // stolen from stackoverflow lol
    u32 fps_timer = SDL_GetTicks();
    int fps_frames = 0;
    int fps_current = 0;
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
        
        // update controller
        const u8 *keys = SDL_GetKeyboardState(NULL);
        u8 buttons = 0;
        if (keys[SDL_SCANCODE_Z])      buttons |= BTN_A;
        if (keys[SDL_SCANCODE_X])      buttons |= BTN_B;
        if (keys[SDL_SCANCODE_RSHIFT] || keys[SDL_SCANCODE_LSHIFT])
                                        buttons |= BTN_SELECT;
        if (keys[SDL_SCANCODE_RETURN]) buttons |= BTN_START;
        if (keys[SDL_SCANCODE_UP]    || keys[SDL_SCANCODE_W]) buttons |= BTN_UP;
        if (keys[SDL_SCANCODE_DOWN]  || keys[SDL_SCANCODE_S]) buttons |= BTN_DOWN;
        if (keys[SDL_SCANCODE_LEFT]  || keys[SDL_SCANCODE_A]) buttons |= BTN_LEFT;
        if (keys[SDL_SCANCODE_RIGHT] || keys[SDL_SCANCODE_D]) buttons |= BTN_RIGHT;
        controller_set_buttons(&bus.controller1, buttons);
        
        // run one frame
        bus.ppu.frame_ready = false;
        while (!bus.ppu.frame_ready) {
            if (bus.ppu.nmi_triggered) {
                bus.ppu.nmi_triggered = false;
                cpu_nmi(&bus.cpu);
            }
            
            cpu_step(&bus.cpu);
            
            // ppu runs 3x faster than cpu
            ppu_step(&bus.ppu);
            ppu_step(&bus.ppu);
            ppu_step(&bus.ppu);
            
            // apu runs at cpu speed
            apu_step(&bus.apu);
        }
        
        // display
        SDL_UpdateTexture(texture, NULL, bus.ppu.framebuffer, 256 * sizeof(u32));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        
        // fps counter
        fps_frames++;
        u32 now = SDL_GetTicks();
        if (now - fps_timer >= 1000) {
            fps_current = fps_frames;
            fps_frames = 0;
            fps_timer = now;
            
            char title[64];
            sprintf(title, "NES Emulator - %d fps", fps_current);
            SDL_SetWindowTitle(window, title);
        }
    }
    
    // cleanup
    if (audio_dev != 0) {
        SDL_CloseAudioDevice(audio_dev);
    }
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    cartridge_free(&bus.cart);
    
    printf("bye!\n");
    return 0;
}