#include "ppu.h"
#include <stdio.h>
#include <string.h>

// forward declarations for helper functions
void ppu_get_pattern_row(PPU *ppu, u8 tile_id, int row, bool table, u8 *low, u8 *high);
u8 ppu_get_pixel_from_pattern(u8 low, u8 high, int x);

// NES color palette - these are the actual RGB colors the NES can display
// theres like a million different "correct" palettes, this is just one of them
static const u32 nes_palette[64] = {
    0xFF666666, 0xFF002A88, 0xFF1412A7, 0xFF3B00A4,
    0xFF5C007E, 0xFF6E0040, 0xFF6C0600, 0xFF561D00,
    0xFF333500, 0xFF0B4800, 0xFF005200, 0xFF004F08,
    0xFF00404D, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFADADAD, 0xFF155FD9, 0xFF4240FF, 0xFF7527FE,
    0xFFA01ACC, 0xFFB71E7B, 0xFFB53120, 0xFF994E00,
    0xFF6B6D00, 0xFF388700, 0xFF0C9300, 0xFF008F32,
    0xFF007C8D, 0xFF000000, 0xFF000000, 0xFF000000,
    0xFFFFFEFF, 0xFF64B0FF, 0xFF9290FF, 0xFFC676FF,
    0xFFF36AFF, 0xFFFE6ECC, 0xFFFE8170, 0xFFEA9E22,
    0xFFBCBE00, 0xFF88D800, 0xFF5CE430, 0xFF45E082,
    0xFF48CDDE, 0xFF4F4F4F, 0xFF000000, 0xFF000000,
    0xFFFFFEFF, 0xFFC0DFFF, 0xFFD3D2FF, 0xFFE8C8FF,
    0xFFFBC2FF, 0xFFFEC4EA, 0xFFFECCC5, 0xFFF7D8A5,
    0xFFE4E594, 0xFFCFEF96, 0xFFBDF4AB, 0xFFB3F3CC,
    0xFFB5EBF2, 0xFFB8B8B8, 0xFF000000, 0xFF000000,
};


void ppu_init(PPU *ppu) {
    memset(ppu, 0, sizeof(PPU));
    printf("[PPU] initialized\n");
}

void ppu_reset(PPU *ppu) {
    ppu->ctrl = 0;
    ppu->mask = 0;
    ppu->status = 0;
    ppu->latch = 0;
    ppu->vram_addr = 0;
    ppu->temp_addr = 0;
    ppu->fine_x = 0;
    ppu->data_buffer = 0;
    ppu->scanline = 0;
    ppu->cycle = 0;
    ppu->frame = 0;
    ppu->frame_ready = false;
    
    // fill framebuffer with a color so we know its working
    for (int i = 0; i < 256 * 240; i++) {
        ppu->framebuffer[i] = nes_palette[0x0F]; // black
    }
    
    printf("[PPU] reset\n");
}


// helper: get the actual vram address considering mirroring
u16 ppu_mirror_vram(PPU *ppu, u16 addr) {
    addr &= 0x2FFF;  // wrap to $2000-$2FFF range
    addr -= 0x2000;  // make it 0-based
    
    // NES has 2KB of VRAM but 4 nametables (4KB addressable)
    // mirroring determines how the 2KB maps to the 4 nametable slots
    // horizontal mirroring: $2000=$2400, $2800=$2C00
    // vertical mirroring: $2000=$2800, $2400=$2C00
    
    if (ppu->mirroring == 0) {
        // horizontal
        if (addr >= 0x800) {
            addr -= 0x800;
        }
        if (addr >= 0x400 && addr < 0x800) {
            addr -= 0x400;
        }
    } else {
        // vertical
        if (addr >= 0x800) {
            addr -= 0x800;
        }
    }
    
    return addr & 0x7FF;  // clamp to 2KB
}


u8 ppu_read_vram(PPU *ppu, u16 addr) {
    addr &= 0x3FFF;  // ppu address space is 14 bits
    
    if (addr < 0x2000) {
        // chr rom/ram (pattern tables)
        if (ppu->chr_rom && addr < ppu->chr_size) {
            return ppu->chr_rom[addr];
        }
        return 0;
    } 
    else if (addr < 0x3F00) {
        // nametables
        return ppu->vram[ppu_mirror_vram(ppu, addr)];
    }
    else {
        // palettes
        u16 palette_addr = addr & 0x1F;
        // mirrors of background color
        if (palette_addr == 0x10 || palette_addr == 0x14 || 
            palette_addr == 0x18 || palette_addr == 0x1C) {
            palette_addr &= 0x0F;
        }
        return ppu->palette[palette_addr];
    }
}

void ppu_write_vram(PPU *ppu, u16 addr, u8 val) {
    addr &= 0x3FFF;
    
    if (addr < 0x2000) {
        // chr rom - usually not writable but some carts have chr ram
        if (ppu->chr_rom && addr < ppu->chr_size) {
            ppu->chr_rom[addr] = val;  // will only work if its actually ram
        }
    }
    else if (addr < 0x3F00) {
        // nametables
        ppu->vram[ppu_mirror_vram(ppu, addr)] = val;
    }
    else {
        // palettes
        u16 palette_addr = addr & 0x1F;
        if (palette_addr == 0x10 || palette_addr == 0x14 || 
            palette_addr == 0x18 || palette_addr == 0x1C) {
            palette_addr &= 0x0F;
        }
        ppu->palette[palette_addr] = val;
    }
}


// cpu reads ppu registers ($2000-$2007)
u8 ppu_read_register(PPU *ppu, u16 addr) {
    u8 result = 0;
    
    switch (addr & 0x07) {
        case 0: // $2000 PPUCTRL - not readable
            break;
            
        case 1: // $2001 PPUMASK - not readable
            break;
            
        case 2: // $2002 PPUSTATUS
            result = (ppu->status & 0xE0);  // only top 3 bits are real
            // reading clears vblank flag
            ppu->status &= ~PPUSTATUS_VBLANK;
            // also resets the latch
            ppu->latch = 0;
            break;
            
        case 3: // $2003 OAMADDR - not readable
            break;
            
        case 4: // $2004 OAMDATA
            result = ppu->oam[ppu->oam_addr];
            break;
            
        case 5: // $2005 PPUSCROLL - not readable
            break;
            
        case 6: // $2006 PPUADDR - not readable
            break;
            
        case 7: // $2007 PPUDATA
            // reading from vram is delayed by one read (buffered)
            // except for palette which is returned immediately
            if (ppu->vram_addr < 0x3F00) {
                result = ppu->data_buffer;
                ppu->data_buffer = ppu_read_vram(ppu, ppu->vram_addr);
            } else {
                // palette read is not buffered
                result = ppu_read_vram(ppu, ppu->vram_addr);
                // but the buffer gets filled with the "underlying" nametable data
                ppu->data_buffer = ppu_read_vram(ppu, ppu->vram_addr - 0x1000);
            }
            // increment vram address
            ppu->vram_addr += (ppu->ctrl & PPUCTRL_INCREMENT) ? 32 : 1;
            break;
    }
    
    return result;
}

// cpu writes to ppu registers ($2000-$2007)
void ppu_write_register(PPU *ppu, u16 addr, u8 val) {
    switch (addr & 0x07) {
        case 0: // $2000 PPUCTRL
            ppu->ctrl = val;
            // update nametable bits in temp address
            ppu->temp_addr = (ppu->temp_addr & 0xF3FF) | ((val & 0x03) << 10);
            break;
            
        case 1: // $2001 PPUMASK
            ppu->mask = val;
            break;
            
        case 2: // $2002 PPUSTATUS - not writable
            break;
            
        case 3: // $2003 OAMADDR
            ppu->oam_addr = val;
            break;
            
        case 4: // $2004 OAMDATA
            ppu->oam[ppu->oam_addr] = val;
            ppu->oam_addr++;
            break;
            
        case 5: // $2005 PPUSCROLL
            if (ppu->latch == 0) {
                // first write - X scroll
                ppu->fine_x = val & 0x07;
                ppu->temp_addr = (ppu->temp_addr & 0xFFE0) | (val >> 3);
                ppu->latch = 1;
            } else {
                // second write - Y scroll
                ppu->temp_addr = (ppu->temp_addr & 0x8C1F) | 
                                 ((val & 0x07) << 12) |
                                 ((val & 0xF8) << 2);
                ppu->latch = 0;
            }
            break;
            
        case 6: // $2006 PPUADDR
            if (ppu->latch == 0) {
                // first write - high byte
                ppu->temp_addr = (ppu->temp_addr & 0x00FF) | ((val & 0x3F) << 8);
                ppu->latch = 1;
            } else {
                // second write - low byte
                ppu->temp_addr = (ppu->temp_addr & 0xFF00) | val;
                ppu->vram_addr = ppu->temp_addr;
                ppu->latch = 0;
            }
            break;
            
        case 7: // $2007 PPUDATA
            ppu_write_vram(ppu, ppu->vram_addr, val);
            ppu->vram_addr += (ppu->ctrl & PPUCTRL_INCREMENT) ? 32 : 1;
            break;
    }
}


// fetch a tile from pattern table
void ppu_get_pattern_row(PPU *ppu, u8 tile_id, int row, bool table, u8 *low, u8 *high) {
    // each tile is 16 bytes (8 rows x 2 planes)
    // table 0 = $0000, table 1 = $1000
    u16 base = table ? 0x1000 : 0x0000;
    u16 addr = base + (tile_id * 16) + row;
    
    *low = ppu_read_vram(ppu, addr);
    *high = ppu_read_vram(ppu, addr + 8);
}

// get pixel color (0-3) from pattern data
u8 ppu_get_pixel_from_pattern(u8 low, u8 high, int x) {
    // x is 0-7, leftmost pixel is bit 7
    int shift = 7 - x;
    u8 lo_bit = (low >> shift) & 1;
    u8 hi_bit = (high >> shift) & 1;
    return (hi_bit << 1) | lo_bit;
}

// one ppu cycle
void ppu_step(PPU *ppu) {
    // visible scanlines: 0-239
    // post-render: 240
    // vblank: 241-260
    // pre-render: 261
    
    bool rendering_enabled = (ppu->mask & (PPUMASK_SHOW_BG | PPUMASK_SHOW_SPR)) != 0;
    bool visible_scanline = ppu->scanline < 240;
    bool visible_cycle = ppu->cycle >= 1 && ppu->cycle <= 256;
    
    // vblank flag
    if (ppu->scanline == 241 && ppu->cycle == 1) {
        ppu->status |= PPUSTATUS_VBLANK;
        ppu->frame_ready = true;
        // trigger NMI if enabled
        if (ppu->ctrl & PPUCTRL_NMI_ENABLE) {
            ppu->nmi_triggered = true;
        }
    }
    
    if (ppu->scanline == 261 && ppu->cycle == 1) {
        ppu->status &= ~PPUSTATUS_VBLANK;
        ppu->status &= ~PPUSTATUS_SPRITE_ZERO;
        ppu->status &= ~PPUSTATUS_OVERFLOW;
    }
    
    // render a pixel
    if (visible_scanline && visible_cycle) {
        int x = ppu->cycle - 1;
        int y = ppu->scanline;
        u32 color = nes_palette[ppu->palette[0] & 0x3F]; // default bg
        
        // background rendering
        if (ppu->mask & PPUMASK_SHOW_BG) {
            // figure out which nametable tile we're in
            // each tile is 8x8 pixels
            // nametable is 32x30 tiles
            
            int tile_x = x / 8;
            int tile_y = y / 8;
            int fine_x = x % 8;
            int fine_y = y % 8;
            
            // get nametable base (simplified, ignoring scroll for now)
            // nametable 0 is at $2000
            u16 nt_addr = 0x2000 + tile_y * 32 + tile_x;
            u8 tile_id = ppu_read_vram(ppu, nt_addr);
            
            // get attribute byte
            // attributes are at end of nametable, each byte covers 4x4 tiles
            int attr_x = tile_x / 4;
            int attr_y = tile_y / 4;
            u16 attr_addr = 0x23C0 + attr_y * 8 + attr_x;
            u8 attr_byte = ppu_read_vram(ppu, attr_addr);
            
            // extract the 2-bit palette for this 2x2 tile area
            int shift = ((tile_y % 4) / 2) * 4 + ((tile_x % 4) / 2) * 2;
            u8 palette_id = (attr_byte >> shift) & 0x03;
            
            // get pattern data
            bool bg_table = (ppu->ctrl & PPUCTRL_BG_TABLE) != 0;
            u8 pattern_low, pattern_high;
            ppu_get_pattern_row(ppu, tile_id, fine_y, bg_table, &pattern_low, &pattern_high);
            
            // get pixel color (0-3)
            u8 pixel = ppu_get_pixel_from_pattern(pattern_low, pattern_high, fine_x);
            
            if (pixel != 0) {
                // non-transparent, look up actual color
                u8 palette_index = ppu->palette[palette_id * 4 + pixel];
                color = nes_palette[palette_index & 0x3F];
            }
        }
        
        ppu->framebuffer[y * 256 + x] = color;
    }
    
    // advance
    ppu->cycle++;
    if (ppu->cycle > 340) {
        ppu->cycle = 0;
        ppu->scanline++;
        if (ppu->scanline > 261) {
            ppu->scanline = 0;
            ppu->frame++;
        }
    }
}