#ifndef PPU_H
#define PPU_H

#include "types.h"

// PPU registers (memory mapped at $2000-$2007)
// $2000 - PPUCTRL
// $2001 - PPUMASK  
// $2002 - PPUSTATUS
// $2003 - OAMADDR
// $2004 - OAMDATA
// $2005 - PPUSCROLL
// $2006 - PPUADDR
// $2007 - PPUDATA

// PPUCTRL bits
#define PPUCTRL_NAMETABLE_X     0x01
#define PPUCTRL_NAMETABLE_Y     0x02
#define PPUCTRL_INCREMENT       0x04  // 0=add 1, 1=add 32
#define PPUCTRL_SPRITE_TABLE    0x08
#define PPUCTRL_BG_TABLE        0x10
#define PPUCTRL_SPRITE_SIZE     0x20  // 0=8x8, 1=8x16
#define PPUCTRL_MASTER_SLAVE    0x40  // unused on NES
#define PPUCTRL_NMI_ENABLE      0x80

// PPUMASK bits
#define PPUMASK_GREYSCALE       0x01
#define PPUMASK_SHOW_BG_LEFT    0x02
#define PPUMASK_SHOW_SPR_LEFT   0x04
#define PPUMASK_SHOW_BG         0x08
#define PPUMASK_SHOW_SPR        0x10
#define PPUMASK_EMPHASIZE_R     0x20
#define PPUMASK_EMPHASIZE_G     0x40
#define PPUMASK_EMPHASIZE_B     0x80

// PPUSTATUS bits
#define PPUSTATUS_OVERFLOW      0x20
#define PPUSTATUS_SPRITE_ZERO   0x40
#define PPUSTATUS_VBLANK        0x80

typedef struct {
    // registers
    u8 ctrl;        // $2000
    u8 mask;        // $2001
    u8 status;      // $2002
    u8 oam_addr;    // $2003
    
    // internal registers/latches
    // the ppu has this weird latch thing for writes to $2005/$2006
    u8 latch;         // first/second write toggle
    u8 fine_x;        // fine x scroll (3 bits)
    u16 vram_addr;    // current vram address (15 bits)
    u16 temp_addr;    // temporary vram address (15 bits)
    u8 data_buffer;   // ppudata read buffer
    
    // memory
    u8 vram[2048];           // 2KB video ram (nametables)
    u8 palette[32];          // palette ram
    u8 oam[256];             // object attribute memory (sprites)
    
    // rendering state
    int scanline;            // current scanline (0-261)
    int cycle;               // current cycle within scanline (0-340)
    u64 frame;               // frame counter
    
    // output
    u32 framebuffer[256 * 240];  // pixel output
    bool frame_ready;            // set when a frame is done
    bool nmi_triggered;          // NMI needs to fire
    
    // we need chr rom access, will be set by bus
    u8 *chr_rom;
    u32 chr_size;
    u8 mirroring;  // 0=horizontal, 1=vertical
    
} PPU;

void ppu_init(PPU *ppu);
void ppu_reset(PPU *ppu);
void ppu_step(PPU *ppu);  // run one ppu cycle

// register access (called by bus)
u8 ppu_read_register(PPU *ppu, u16 addr);
void ppu_write_register(PPU *ppu, u16 addr, u8 val);

#endif