/* Host renderer. Mode-1 tile decoding/composition follows the sibling
 * Super Metroid custom renderer; level addressing and object ownership are SMW.
 * Per-line immutable snapshots retain IRQ/HDMA changes without replaying logic. */
#include "smw_renderer.h"
#include "common_rtl.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

typedef struct RasterLine {
  uint8_t regs[PPU_SAVESTATE_REGS_SIZE];
  uint16_t palette[256], oam[256], vram[0x8000];
  uint8_t high[32];
} RasterLine;
typedef struct OamOwner { int x; uint16_t position, attr; bool valid; } OamOwner;
static RasterLine lines[224];
static uint8_t frame_ram[0x20000];
static OamOwner pending[128], latched[128];
typedef struct SpriteOwner { int x,y; bool valid; } SpriteOwner;
static SpriteOwner sprite_owners[64];
static unsigned captured;
static int native_x;
static Ppu raster;
static uint16_t objects[SMW_RENDER_MAX_WIDTH];
static unsigned read16(const uint8_t *r, unsigned a) { return r[a] | (r[a+1] << 8); }
static unsigned rom16(unsigned bank, unsigned a) {
  if (!g_rom || a < 0x8000 || a > 0xfffe || bank > 15) return 0;
  return read16(g_rom, (bank << 15) | (a & 0x7fff));
}
void SmwRendererRecordOam(unsigned slot, int x, uint16_t pos, uint16_t attr) {
  if (slot < 128) pending[slot] = (OamOwner){x, pos, attr, true};
}
void SmwRendererRecordSprite(unsigned slot, int x, int y, unsigned first, unsigned end) {
  if (slot < 12 && first < end && end <= 256)
    for (unsigned index=first;index<end;index+=4)
      sprite_owners[index/4] = (SpriteOwner){x,y,true};
}
void SmwRendererLatchOam(void) {
  /* SMW's small generic graphics paths do not call FinishOAMWrite. Their
   * GetDrawInfo call identifies the sprite's reserved OAM allocation. Bind
   * the completed pieces before NMI, with signed deltas to that draw origin.
   * Each piece retains its own draw origin: multi-pass sprites such as Yoshi
   * draw the head first, then shift the same sprite's origin for the body.
   * Store the exact final image so later OAM reuse cannot inherit an owner. */
  for (unsigned piece=0;piece<64;++piece) {
    const SpriteOwner *owner=&sprite_owners[piece];
    if (!owner->valid) continue;
    unsigned slot=64+piece, index=piece*4;
    if (pending[slot].valid) continue;
    unsigned pos=read16(g_ram,0x300+index), attr=read16(g_ram,0x302+index);
    if ((pos>>8)==240) continue;
    int dx=(int8_t)((pos&255)-(owner->x&255));
    int dy=(int8_t)((pos>>8)-(owner->y&255));
    if (abs(dx)>64 || abs(dy)>64) continue;
    pending[slot]=(OamOwner){owner->x+dx,(uint16_t)pos,(uint16_t)attr,true};
  }
  memcpy(latched, pending, sizeof(latched));
  memset(pending, 0, sizeof(pending));
  memset(sprite_owners,0,sizeof(sprite_owners));
}
void SmwRendererBeginFrame(const uint8_t *ram) {
  captured = 0;
  if (!g_smw_video.enabled) return;
  memcpy(frame_ram, ram, sizeof(frame_ram));
  native_x = g_smw_viewport.extra;
  if (ram[0x100] == 0x14 && !(ram[0x5b] & 1))
    native_x = SmwViewOffset(g_smw_viewport, read16(ram, 0x1a), (ram[0x5e]+1)*256);
}
void SmwRendererCaptureLine(const Ppu *p, int line) {
  if (!g_smw_video.enabled || line < 1 || line > 224) return;
  RasterLine *l = &lines[line-1];
  memcpy(l->regs, p, sizeof(l->regs));
  memcpy(l->palette, p->cgram, sizeof(l->palette));
  memcpy(l->oam, p->oam, sizeof(l->oam));
  memcpy(l->high, p->highOam, sizeof(l->high));
  memcpy(l->vram, p->vram, sizeof(l->vram));
  ++captured;
}

bool SmwRendererMapTile(const uint8_t *ram, unsigned layer, int x, int y, uint16_t *tile) {
  unsigned mode = ram[0x1925];
  if (layer > 1 || mode >= 32 || x < 0 || y < 0) return false;
  bool vertical = (ram[0x5b] & (1u << layer)) != 0;
  unsigned screen, index;
  if (vertical) {
    if (x >= 512 || y >= 0x1c00) return false;
    screen = (unsigned)y >> 8;
    index = ((x >> 8) * 256) + ((y & 255) >> 4) * 16 + ((x & 255) >> 4);
  } else {
    if (x >= ((int)ram[0x5e] + 1) * 256 || y >= 432) return false;
    screen = (unsigned)x >> 8;
    index = (y >> 4) * 16 + ((x & 255) >> 4);
  }
  unsigned table = rom16(0, (layer ? 0xbde8 : 0xbda8) + mode * 2);
  if (!table) return false;
  unsigned low = rom16(0, table + screen * 3);
  /* Vanilla tables address parallel $7E/$7F Map16 planes. */
  if (low < 0xc800 || low + index > 0xffff) return false;
  unsigned block = ram[low+index] | (ram[0x10000+low+index] << 8);
  if (block >= 512) return false;
  unsigned address = read16(ram, 0xfbe + block * 2);
  address += (x & 8 ? 4 : 0) + (y & 8 ? 2 : 0);
  if (address < 0x8000 || address > 0xfffe) return false;
  *tile = (uint16_t)rom16(ram[0x1931] >= 0x10 ? 5 : 13, address);
  return true;
}
static unsigned tile_pixel(const uint16_t *vram, unsigned address, int x, int y, int bpp) {
  unsigned a = (address + y) & 0x7fff;
  unsigned shift = 7 - x;
  unsigned bits = vram[a] >> shift;
  unsigned pixel = (bits & 1) | ((bits >> 7) & 2);
  if (bpp == 4) {
    bits = vram[(a + 8) & 0x7fff] >> shift;
    pixel |= ((bits & 1) << 2) | ((bits >> 5) & 8);
  }
  return pixel;
}

static bool window_condition(unsigned mode, bool inside) {
  return mode == 3 || (mode == 1 && !inside) || (mode == 2 && inside);
}

static uint32_t colour(const Ppu *p, const uint16_t *palette, const uint8_t *brightness, uint16_t main,
                       uint16_t sub, bool inside) {
  unsigned rgb = palette[main & 255], layer = (main >> 8) & 15;
  bool clipped = window_condition(p->cgwsel >> 6, inside);
  bool math = !window_condition((p->cgwsel >> 4) & 3, inside) &&
              ((p->cgadsub & 63) & (1u << layer));
  unsigned other = p->fixedColor;
  bool half = math && (p->cgadsub & 64) && !clipped;
  if (math && (p->cgwsel & 2)) {
    if ((sub & 255) != 0) other = palette[sub & 255];
    else half = false;
  }
  uint32_t result = 0;
  for (int component = 0; component < 3; ++component) {
    int c = clipped ? 0 : (rgb >> (component * 5)) & 31;
    if (math) {
      int second = (other >> (component * 5)) & 31;
      c += p->cgadsub & 128 ? -second : second;
      if (c < 0) c = 0;
      if (half) c /= 2;
      if (c > 31) c = 31;
    }
    c = brightness[c];
    result |= (uint32_t)c << (16 - component * 8);
  }
  return result;
}

static bool window(const Ppu *p, int layer, int x) {
  unsigned flags = (p->windowsel >> (layer*4)) & 15;
  int left = -native_x, right = g_smw_viewport.width-native_x-1;
  int l1 = p->window1left ? p->window1left : left;
  int r1 = p->window1right == 255 ? right : p->window1right;
  int l2 = p->window2left ? p->window2left : left;
  int r2 = p->window2right == 255 ? right : p->window2right;
  bool a = (x >= l1 && x <= r1) != ((flags&1)!=0);
  bool b = (x >= l2 && x <= r2) != ((flags&4)!=0);
  if (!(flags&2)) return (flags&8) && b;
  if (!(flags&8)) return a;
  switch ((p->wbgobjlog >> (layer*2)) & 3) {
    case 0: return a || b; case 1: return a && b;
    case 2: return a != b; default: return a == b;
  }
}
static uint16_t background(const Ppu *p, const RasterLine *l, unsigned layer, int x, int y) {
  static const unsigned low[] = {8,7,1}, high[] = {12,11,3};
  /* Preserve the native groups: lives/bonus at left, reserve box centered,
   * TIME/coins/score at right. Sample their original tiles without stretching
   * glyphs or depending on the gameplay camera's position at a level edge. */
  if (layer == 2 && y <= 40) {
    int sx = x + native_x, width = g_smw_viewport.width;
    if (sx < 112) x = sx;
    else if (sx >= width - 112) x = sx - (width - 256);
    else if (sx >= g_smw_viewport.extra + 112 && sx < g_smw_viewport.extra + 144)
      x = sx - g_smw_viewport.extra;
    else return 0;
  }
  if (layer == 2 && (y <= 40 || frame_ram[0x1426]) && (x < 0 || x >= 256)) return 0;
  int size = PPU_bigTiles(p, layer) ? 16 : 8;
  int bpp = layer == 2 ? 2 : 4;
  if (p->mosaic & (1u << layer)) {
    int m = (p->mosaic >> 4) + 1;
    x -= ((x % m) + m) % m;
    y -= y % m;
  }
  int px = (x + p->hScroll[layer]) & 1023;
  int py = (y + p->vScroll[layer]) & 1023;
  unsigned sc = p->bgXsc[layer], tx = px/size, ty = py/size;
  unsigned addr = (sc & 0xfc)*256 + (tx&31) + (ty&31)*32;
  if ((sc&1) && (tx&32)) addr += 1024;
  if ((sc&2) && (ty&32)) addr += sc&1 ? 2048 : 1024;
  uint16_t tile = l->vram[addr&0x7fff];
  bool world_layer = layer == 0 || (layer == 1 && frame_ram[0x1925] < 32 &&
                       (0x800081feu & (1u << frame_ram[0x1925])));
  if ((x < 0 || x >= 256) && world_layer && size == 8) {
    int camx = read16(frame_ram,0x1a+layer*4), camy = read16(frame_ram,0x1c+layer*4);
    int dx = ((p->hScroll[layer]-camx+512)&1023)-512;
    int dy = ((p->vScroll[layer]-camy+512)&1023)-512;
    int wx = camx+x+dx, wy = camy+y+dy;
    if (!SmwRendererMapTile(frame_ram,layer,wx,wy,&tile)) return 0;
    if (layer == 1 && frame_ram[0x1931] == 3) tile |= 0x1000;
    px = wx; py = wy;
  }
  int cx = px&(size-1), cy = py&(size-1);
  if (tile&0x4000) cx = size-1-cx;
  if (tile&0x8000) cy = size-1-cy;
  unsigned number = ((tile&1023) + cx/8 + (cy/8)*16)&1023;
  unsigned base = ((p->bgTileAdr>>(layer*4))&15)*4096;
  unsigned pixel = tile_pixel(l->vram,base+number*(bpp*4),cx&7,cy&7,bpp);
  if (!pixel) return 0;
  unsigned priority = tile&0x2000 ? high[layer] : low[layer];
  if (layer == 2 && (tile&0x2000) && (p->bgmode&8)) priority = 15;
  return (priority<<12)|(layer<<8)|(((tile>>10)&7)*(1u<<bpp))|pixel;
}
static void sprites(const Ppu *p, const RasterLine *l, int y) {
  static const int sizes[8][2]={{8,16},{8,32},{8,64},{16,32},{16,64},{32,64},{16,32},{16,32}};
  memset(objects,0,g_smw_viewport.width*sizeof(*objects));
  for (int slot=127;slot>=0;--slot) {
    unsigned pos=l->oam[slot*2], attr=l->oam[slot*2+1];
    unsigned hi=l->high[slot/4]>>((slot%4)*2);
    int size=sizes[p->obsel>>5][(hi>>1)&1];
    int row=(y-(pos>>8))&255;
    if (row>=size) continue;
    int x=(pos&255)|((hi&1)<<8);
    if(x>=256) x-=512;
    const OamOwner *owner=&latched[slot];
    if(owner->valid && owner->position==pos && owner->attr==attr) x=owner->x;
    else if(x+size<=0 || x>=256) continue;
    /* $00:9Dxx DrawReserveItem owns OAM 56 (or 0 in the boss variant).
     * Verify its native position and current tile before moving it. */
    static const uint8_t item_tiles[4]={0x24,0x26,0x48,0x0e};
    unsigned item=frame_ram[0xdc2];
    if ((slot==56 || slot==0) && pos==0x0f78 && item>=1 && item<=4 &&
        (attr&255)==item_tiles[item-1]) x += g_smw_viewport.extra-native_x;
    if(attr&0x8000) row=size-1-row;
    unsigned base=(p->obsel&7)*8192;
    if(attr&256) base+=(((p->obsel>>3)&3)+1)*4096;
    unsigned palette=128+((attr>>9)&7)*16;
    unsigned priority=((attr>>12)&3)*4+2;
    unsigned layer=attr&0x800?4:6;
    for(int col=0;col<size;++col) {
      int dest=x+col+native_x;
      if(dest<0 || dest>=g_smw_viewport.width) continue;
      int cx=attr&0x4000?size-1-col:col;
      unsigned tile=(((((attr&255)>>4)+row/8)&15)<<4)|(((attr&15)+cx/8)&15);
      unsigned pixel=tile_pixel(l->vram,base+tile*16,cx&7,row&7,4);
      if(pixel) objects[dest]=(priority<<12)|(layer<<8)|palette|pixel;
    }
  }
}
void SmwRendererDraw(uint8_t *pixels,size_t pitch,const uint8_t *stock) {
  int width=g_smw_viewport.width;
  bool level=frame_ram[0x100]==0x14;
  for(int y=0;y<224;++y) {
    uint32_t *dst=(uint32_t *)(pixels+y*pitch);
    const RasterLine *l=&lines[y];
    memcpy(&raster,l->regs,sizeof(l->regs));
    if(width==256 || !level || captured!=224 || (raster.bgmode&7)!=1) {
      memset(dst,0,width*4);
      memcpy(dst+native_x,stock+y*256*4,256*4);
      continue;
    }
    if(raster.inidisp&128) { memset(dst,0,width*4); continue; }
    uint8_t brightness[32];
    for(int i=0;i<32;++i) brightness[i]=((i<<3)|(i>>2))*(raster.inidisp&15)/15;
    sprites(&raster,l,y);
    for(int sx=0;sx<width;++sx) {
      int x=sx-native_x;
      uint16_t bg[3], screens[2]={0x500,0x500};
      for(int layer=0;layer<3;++layer) bg[layer]=background(&raster,l,layer,x,y+1);
      for(int sub=0;sub<2;++sub) {
        for(int layer=0;layer<3;++layer) {
          if(!(raster.screenEnabled[sub]&(1u<<layer))) continue;
          if((raster.screenWindowed[sub]&(1u<<layer)) && window(&raster,layer,x)) continue;
          if(bg[layer]>screens[sub]) screens[sub]=bg[layer];
        }
        if((raster.screenEnabled[sub]&16) &&
           (!(raster.screenWindowed[sub]&16) || !window(&raster,4,x)) &&
           objects[sx]>screens[sub]) screens[sub]=objects[sx];
      }
      dst[sx]=colour(&raster,l->palette,brightness,screens[0],screens[1],window(&raster,5,x));
    }
  }
}

/* A far object's wrapped native OAM can alias into the stock image. Only
 * differences inside the exact captured, re-positioned piece are expected. */
static bool alias_footprint(int x,int y) {
  static const int sizes[8][2]={{8,16},{8,32},{8,64},{16,32},{16,64},{32,64},{16,32},{16,32}};
  const RasterLine *l=&lines[y];
  for(unsigned slot=0;slot<128;++slot) {
    const OamOwner *o=&latched[slot];
    unsigned pos=l->oam[slot*2],attr=l->oam[slot*2+1];
    if(!o->valid || o->position!=pos || o->attr!=attr) continue;
    unsigned hi=l->high[slot/4]>>((slot%4)*2);
    int old=(pos&255)|((hi&1)<<8);
    if(old>=256)old-=512;
    if(old==o->x)continue;
    int size=sizes[l->regs[offsetof(Ppu,obsel)]>>5][(hi>>1)&1];
    if(((y-(pos>>8))&255)>=(unsigned)size)continue;
    if((x>=old && x<old+size) || (x>=o->x && x<o->x+size))return true;
  }
  return false;
}
/* Opt-in capture and native-center oracle. Does not pause or modify the guest. */
void SmwRendererDiagnostics(const uint8_t *stock, const uint8_t *image, size_t pitch) {
  static unsigned frame;
  static FILE *trace;
  const char *directory=getenv("SMW_RENDER_DIAGNOSTICS");
  ++frame;
  if(!directory || !*directory) return;
  size_t size=(size_t)g_smw_viewport.width*224*4;
  unsigned differing=0, unexplained=0, active=0, far=0;
  /* Wide HUD anchoring is verified separately; its relocation is intentional. */
  for(int y=(g_smw_viewport.width==256?0:40);y<224;++y) for(int x=0;x<256;++x) {
    uint32_t a=((const uint32_t *)(image+y*pitch))[native_x+x];
    uint32_t b=((const uint32_t *)stock)[y*256+x];
    if((a&0xffffff)!=(b&0xffffff)) {
      ++differing;
      if(!alias_footprint(x,y))++unexplained;
    }
  }
  int camera=read16(frame_ram,0x1a);
  for(int i=0;i<12;++i) if(frame_ram[0x14c8+i]) {
    ++active;
    int x=(frame_ram[0xe4+i]|(frame_ram[0x14e0+i]<<8))-camera;
    if(x<-64 || x>=320) ++far;
  }
  char path[1024];
  if(!trace) {
    snprintf(path,sizeof(path),"%s/frames.csv",directory);
    trace=fopen(path,"w");
    if(trace) fprintf(trace,"frame,mode,camera,width,active,far,native_differences,unexplained_differences\n");
  }
  if(trace) { fprintf(trace,"%u,%u,%d,%d,%u,%u,%u,%u\n",frame,frame_ram[0x100],camera,g_smw_viewport.width,active,far,differing,unexplained); fflush(trace); }
  const char *requested=getenv("SMW_RENDER_CAPTURE_FRAME");
  const char *interval=getenv("SMW_RENDER_CAPTURE_EVERY");
  unsigned every=interval && atoi(interval)>0?(unsigned)atoi(interval):300;
  if(requested ? frame!=(unsigned)atoi(requested) : frame%every!=0) return;
  snprintf(path,sizeof(path),"%s/frame-%06u.bmp",directory,frame);
  FILE *f=fopen(path,"wb");
  if(!f) return;
  uint8_t header[54]={0x42,0x4d};
  uint32_t values[]={54+(uint32_t)size,54,40,(uint32_t)g_smw_viewport.width,224,0x200001};
  const unsigned offsets[]={2,10,14,18,22,26};
  for(unsigned i=0;i<6;++i) for(unsigned j=0;j<4;++j) header[offsets[i]+j]=(uint8_t)(values[i]>>(8*j));
  fwrite(header,1,54,f);
  for(int y=223;y>=0;--y) fwrite(image+(size_t)y*pitch,4,g_smw_viewport.width,f);
  fclose(f);
  if(requested) {
    snprintf(path,sizeof(path),"%s/frame.swr",directory);
    f=fopen(path,"wb");
    if(f) {
      fwrite(frame_ram,1,sizeof(frame_ram),f);
      fwrite(lines,1,sizeof(lines),f);
      fwrite(latched,1,sizeof(latched),f);
      fwrite(stock,1,256*224*4,f);
      fclose(f);
    }
  }
}
