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
    addr &= 0x2FFF;
    addr -= 0x2000;
    
    // NES has 2KB vram mapped across 4 nametables
    // horizontal: A=B (top/bottom share), C=D
    // vertical: A=C (left/right share), B=D
    // i had this completely wrong before, was collapsing everything to 1KB
    
    if (ppu->mirroring == 0) {
        // horizontal mirroring
        // nametable 0 ($2000) and 1 ($2400) are the same
        // nametable 2 ($2800) and 3 ($2C00) are the same
        if (addr >= 0x000 && addr < 0x400) return addr;           // NT0
        if (addr >= 0x400 && addr < 0x800) return addr - 0x400;   // NT1 = NT0
        if (addr >= 0x800 && addr < 0xC00) return addr - 0x400;   // NT2
        if (addr >= 0xC00 && addr < 0x1000) return addr - 0x800;  // NT3 = NT2
    } else {
        // vertical mirroring
        // nametable 0 ($2000) and 2 ($2800) are the same
        // nametable 1 ($2400) and 3 ($2C00) are the same
        if (addr >= 0x000 && addr < 0x400) return addr;           // NT0
        if (addr >= 0x400 && addr < 0x800) return addr;           // NT1
        if (addr >= 0x800 && addr < 0xC00) return addr - 0x800;   // NT2 = NT0
        if (addr >= 0xC00 && addr < 0x1000) return addr - 0x800;  // NT3 = NT1
    }
    
    return addr & 0x7FF; // fallback, shouldnt hit this
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


// tiles are 16 bytes, first 8 = low bits, next 8 = high bits
// took me way too long to figure this out
void ppu_get_pattern_row(PPU *ppu, u8 tile_id, int row, bool table, u8 *low, u8 *high) {
    // each tile is 16 bytes (8 rows x 2 planes)
    // table 0 = $0000, table 1 = $1000
    u16 base = table ? 0x1000 : 0x0000;
    u16 addr = base + (tile_id * 16) + row;
    
    *low = ppu_read_vram(ppu, addr);
    *high = ppu_read_vram(ppu, addr + 8);
}

// bit 7 is LEFT side, not right. kept getting this backwards
u8 ppu_get_pixel_from_pattern(u8 low, u8 high, int x) {
    // x is 0-7, leftmost pixel is bit 7
    int shift = 7 - x;
    u8 lo_bit = (low >> shift) & 1;
    u8 hi_bit = (high >> shift) & 1;
    return (hi_bit << 1) | lo_bit;
}

// one ppu cycle
void ppu_step(PPU *ppu) {
    // OLD WAY - i tried rendering the whole frame at once during vblank
    // but that doesnt work for sprite zero hit detection because
    // games check it mid-frame. had to switch to per-pixel rendering
    // which is slower but actually correct
    // RIP my afternoon
    
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
        if (ppu->ctrl & PPUCTRL_NMI_ENABLE) {
            ppu->nmi_triggered = true;
        }
    }
    
    if (ppu->scanline == 261 && ppu->cycle == 1) {
        ppu->status &= ~PPUSTATUS_VBLANK;
        ppu->status &= ~PPUSTATUS_SPRITE_ZERO;
        ppu->status &= ~PPUSTATUS_OVERFLOW;
    }
    
    if (visible_scanline && visible_cycle) {
        int x = ppu->cycle - 1;
        int y = ppu->scanline;
        u32 color = nes_palette[ppu->palette[0] & 0x3F];
        u8 bg_pixel = 0;
        
        // === BACKGROUND ===
        if (ppu->mask & PPUMASK_SHOW_BG) {
            // ok so scrolling... the ppu has this whole internal address
            // register system (vram_addr / temp_addr / fine_x) that controls
            // what part of the nametable we're looking at
            // 
            // i spent like 3 hours reading loopy's scroll doc trying to
            // understand this. the vram_addr bits encode the scroll position:
            //   yyy NN YYYYY XXXXX
            //   |   |  |     |
            //   |   |  |     +-- coarse X scroll (which tile column)
            //   |   |  +-------- coarse Y scroll (which tile row)
            //   |   +----------- nametable select (2 bits)
            //   +---------------- fine Y scroll (which row WITHIN the tile)
            //
            // fine X is stored separately because of course it is
            
            // get scroll position from vram address
            int scroll_x = (ppu->vram_addr & 0x001F);         // coarse X
            int scroll_y = (ppu->vram_addr >> 5) & 0x1F;      // coarse Y  
            int fine_y_scroll = (ppu->vram_addr >> 12) & 0x07; // fine Y
            int nametable = (ppu->vram_addr >> 10) & 0x03;     // which nametable
            
            // actual pixel position considering scroll
            int actual_x = x + ppu->fine_x;
            int tile_x = scroll_x + (actual_x / 8);
            int fine_x_pixel = actual_x % 8;
            int tile_y = scroll_y;
            int fine_y = fine_y_scroll;
            
            // figure out which nametable we're in
            // if tile_x >= 32 we've scrolled into the next nametable horizontally
            u16 nt_base = 0x2000;
            int nt_select = nametable;
            if (tile_x >= 32) {
                tile_x -= 32;
                nt_select ^= 1; // flip horizontal nametable
            }
            nt_base = 0x2000 + (nt_select * 0x400);
            
            u16 nt_addr = nt_base + tile_y * 32 + tile_x;
            u8 tile_id = ppu_read_vram(ppu, nt_addr);
            
            // attribute table - this is relative to the nametable we're in
            int attr_x = tile_x / 4;
            int attr_y = tile_y / 4;
            u16 attr_base = nt_base + 0x03C0;
            u16 attr_addr = attr_base + attr_y * 8 + attr_x;
            u8 attr_byte = ppu_read_vram(ppu, attr_addr);
            
            // this shift calculation... i dont fully understand it but it works
            // something about 2x2 tile groups within 4x4 areas
            // copied from nesdev wiki honestly
            int shift = ((tile_y % 4) / 2) * 4 + ((tile_x % 4) / 2) * 2;
            u8 palette_id = (attr_byte >> shift) & 0x03;
            
            bool bg_table = (ppu->ctrl & PPUCTRL_BG_TABLE) != 0;
            u8 pattern_low, pattern_high;
            ppu_get_pattern_row(ppu, tile_id, fine_y, bg_table, &pattern_low, &pattern_high);
            
            bg_pixel = ppu_get_pixel_from_pattern(pattern_low, pattern_high, fine_x_pixel);
            
            if (bg_pixel != 0) {
                u8 palette_index = ppu->palette[palette_id * 4 + bg_pixel];
                color = nes_palette[palette_index & 0x3F];
            }
        }
        
        // TODO: sprite evaluation is probably wrong
        // only checking 8 sprites per scanline but not really enforcing it??
        // games seem to work so whatever for now
        if (ppu->mask & PPUMASK_SHOW_SPR) {
            // go through OAM in reverse so sprite 0 has highest priority
            // (last one drawn wins, and we want sprite 0 on top)
            
            for (int i = 63; i >= 0; i--) {
                u8 sprite_y    = ppu->oam[i * 4 + 0];
                u8 sprite_tile = ppu->oam[i * 4 + 1];
                u8 sprite_attr = ppu->oam[i * 4 + 2];
                u8 sprite_x    = ppu->oam[i * 4 + 3];
                
                sprite_y += 1;
                
                int sprite_height = (ppu->ctrl & PPUCTRL_SPRITE_SIZE) ? 16 : 8;
                if (y < sprite_y || y >= sprite_y + sprite_height) {
                    continue;
                }
                
                if (x < sprite_x || x >= sprite_x + 8) {
                    continue;
                }
                
                int row = y - sprite_y;
                
                if (sprite_attr & 0x80) {
                    row = (sprite_height - 1) - row;
                }
                
                bool spr_table = (ppu->ctrl & PPUCTRL_SPRITE_TABLE) != 0;
                u8 spr_low, spr_high;
                
                if (sprite_height == 16) {
                    spr_table = (sprite_tile & 0x01) != 0;
                    sprite_tile &= 0xFE;
                    if (row >= 8) {
                        sprite_tile++;
                        row -= 8;
                    }
                }
                
                ppu_get_pattern_row(ppu, sprite_tile, row, spr_table, &spr_low, &spr_high);
                
                int col = x - sprite_x;
                
                if (sprite_attr & 0x40) {
                    col = 7 - col;
                }
                
                u8 spr_pixel = ppu_get_pixel_from_pattern(spr_low, spr_high, col);
                
                if (spr_pixel == 0) {
                    continue;
                }
                
                // sprite zero hit - took FOREVER to get this right
                // x != 255 is some edge case i found on forums
                if (i == 0 && bg_pixel != 0 && x != 255) {
                    ppu->status |= PPUSTATUS_SPRITE_ZERO;
                }
                
                bool behind_bg = (sprite_attr & 0x20) != 0;
                
                if (behind_bg && bg_pixel != 0) {
                    continue;
                }
                
                u8 spr_palette = (sprite_attr & 0x03) + 4;
                u8 palette_index = ppu->palette[spr_palette * 4 + spr_pixel];
                color = nes_palette[palette_index & 0x3F];
            }
        }
        
        ppu->framebuffer[y * 256 + x] = color;
    }
    
    // scroll updates during rendering
    // the ppu increments parts of vram_addr as it renders
    // this is the part that took me forever to get right
    if (rendering_enabled) {
        // at cycle 256, increment Y scroll
        if (visible_scanline && ppu->cycle == 256) {
            // increment fine Y
            if ((ppu->vram_addr & 0x7000) != 0x7000) {
                ppu->vram_addr += 0x1000;
            } else {
                ppu->vram_addr &= ~0x7000; // reset fine Y
                int coarse_y = (ppu->vram_addr & 0x03E0) >> 5;
                if (coarse_y == 29) {
                    coarse_y = 0;
                    ppu->vram_addr ^= 0x0800; // flip vertical nametable
                } else if (coarse_y == 31) {
                    coarse_y = 0;
                    // dont flip nametable, weird edge case
                } else {
                    coarse_y++;
                }
                ppu->vram_addr = (ppu->vram_addr & ~0x03E0) | (coarse_y << 5);
            }
        }
        
        // at cycle 257, copy horizontal bits from temp to vram addr
        if (visible_scanline && ppu->cycle == 257) {
            ppu->vram_addr = (ppu->vram_addr & ~0x041F) | (ppu->temp_addr & 0x041F);
        }
        
        // during pre-render scanline (261), copy vertical bits
        if (ppu->scanline == 261 && ppu->cycle >= 280 && ppu->cycle <= 304) {
            ppu->vram_addr = (ppu->vram_addr & ~0x7BE0) | (ppu->temp_addr & 0x7BE0);
        }
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