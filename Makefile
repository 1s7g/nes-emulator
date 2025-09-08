# makefile because im tired of typing the gcc command every time
# yes i know cmake exists, no i dont want to learn it right now

CC = gcc
CFLAGS = -Wall
SDL_INC = -IC:\SDL2-2.30.12\x86_64-w64-mingw32\include
SDL_LIB = -LC:\SDL2-2.30.12\x86_64-w64-mingw32\lib
SDL_FLAGS = -lmingw32 -lSDL2main -lSDL2

SRC = main.c cpu.c bus.c cartridge.c ppu.c apu.c controller.c

# just compiles everything, no fancy incremental builds or whatever
# it compiles in like 2 seconds anyway so who cares
all:
	$(CC) -o nes $(SRC) $(CFLAGS) $(SDL_INC) $(SDL_LIB) $(SDL_FLAGS)

clean:
	del nes.exe

run: all
	nes "Super Mario Bros. (World).nes"