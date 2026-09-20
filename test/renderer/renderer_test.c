#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "cpu_state.h"
#include "snes/interp_bridge.h"
#include "smw_renderer.h"
#include "common_rtl.h"
uint8_t g_ram[0x20000];
static uint8_t rom[0x80000];
const uint8_t *g_rom=rom;
uint8 cpu_read8(CpuState *cpu,uint8 bank,uint16 addr) {
  (void)cpu;
  return bank==0x7e || (bank==0 && addr<0x2000) ? g_ram[addr] : rom[((bank&15)<<15)|(addr&0x7fff)];
}
void cpu_write8(CpuState *cpu,uint8 bank,uint16 addr,uint8 value) { (void)cpu; assert(bank==0x7e); g_ram[addr]=value; }
void interp_bridge_set_pre_opcode_hook(uint32_t pc,InterpPreOpcodeHook hook) { (void)pc;(void)hook; }
extern void SmwRendererGuestHook(CpuState *,uint32_t);
extern void SmwRendererDrawInfo(CpuState *);
static void word(unsigned a,unsigned value) { g_ram[a]=value;g_ram[a+1]=value>>8; }
static void geometry(void) {
  SmwVideoSettings s={true,true,0};
  const int cases[][3]={{800,600,256},{1920,1080,342},{3440,1440,458},{3840,1080,682},{10000,900,2134},{2759,777,682},{600,1000,256}};
  for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);++i) {
    SmwViewport v=SmwCalculateViewport(&s,cases[i][0],cases[i][1]);
    assert(v.width==cases[i][2]);
    int x,y,w,h;SmwDestination(v,cases[i][0],cases[i][1],&x,&y,&w,&h);
    assert(w>0 && h>0 && x>=0 && y>=0 && x+w<=cases[i][0] && y+h<=cases[i][1]);
  }
  assert(SmwCalculateViewport(&s,0,0).width==256);
  assert(SmwCalculateViewport(&s,2147483647,1).width==SMW_RENDER_MAX_WIDTH);
  s.aspect=100.0/9;assert(SmwCalculateViewport(&s,640,480).width==2134);
  s.enabled=false;assert(SmwCalculateViewport(&s,10000,900).width==256);
  SmwViewport v={2134,939,100.0/9};
  assert(SmwViewOffset(v,0,8192)==0);
  assert(SmwViewOffset(v,4096,8192)==939);
  assert(SmwViewOffset(v,7936,8192)==1878);
}
static void spawn(void) {
  CpuState cpu={0};cpu.Y=1;
  g_smw_video=(SmwVideoSettings){true,true,0};g_smw_viewport=(SmwViewport){2134,939,100.0/9};
  memset(g_ram,0,sizeof(g_ram));g_ram[0x100]=20;g_ram[0x5e]=31;
  word(0xce,0x8000);g_ram[0xd0]=2;
  unsigned data=(2<<15)+1;
  /* Enemy at x=2048, beyond every hardware OAM representation. */
  rom[data]=0;rom[data+1]=8;rom[data+2]=4;
  SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[1]==8 && g_ram[0]==0);
  /* 32px lookahead hides entry pop-in; the next column is excluded. */
  rom[data+1]=0x78;SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[1]==8);
  rom[data+1]=0x88;SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[1]==255);
  /* Original policy leaves the native loader's frontier completely alone. */
  g_smw_video.adaptive_spawns=false;g_ram[0]=0x20;g_ram[1]=1;
  SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[0]==0x20 && g_ram[1]==1);
  g_smw_video.adaptive_spawns=true;
  /* Generators/level commands still activate only at their native frontier. */
  rom[data+1]=8;rom[data+2]=0xe7;SmwRendererGuestHook(&cpu,0x02A82E);assert(g_ram[1]==255);
  /* Fireballs use their actual host coordinate and honor original lifecycle. */
  cpu.X=0;g_ram[0x171f]=0;g_ram[0x1733]=8;
  SmwRendererGuestHook(&cpu,0x02A1BE);assert(cpu._flag_Z==1);
  g_smw_video.adaptive_spawns=false;
  SmwRendererGuestHook(&cpu,0x02A1BE);assert(cpu._flag_Z==0);
  g_smw_video.adaptive_spawns=true;
  /* Native GetDrawInfo culling cannot hide an object 2048px to the right. */
  g_ram[0xe4]=0;g_ram[0x14e0]=8;cpu.X=0;
  SmwRendererDrawInfo(&cpu);assert(g_ram[0x15c4]==0 && cpu._flag_Z==1);
  g_ram[0x14e0]=9;SmwRendererDrawInfo(&cpu);assert(g_ram[0x15c4]==1);
}
static Ppu test_ppu;
static uint8_t surface[2134*224*4], native[256*224*4];
static void objects(void) {
  memset(g_ram,0,sizeof(g_ram));memset(&test_ppu,0,sizeof(test_ppu));
  g_ram[0x100]=20;g_ram[0x5e]=31;
  g_smw_video=(SmwVideoSettings){true,true,0};g_smw_viewport=(SmwViewport){2134,939,100.0/9};
  test_ppu.inidisp=15;test_ppu.bgmode=1;test_ppu.screenEnabled[0]=16;
  test_ppu.cgram[129]=31;
  for(int y=0;y<8;++y)test_ppu.vram[y]=0x00ff;
  for(int i=0;i<128;++i)test_ppu.oam[i*2]=0xf000;
  test_ppu.oam[128]=100*256;test_ppu.oam[129]=0x3000;
  word(0x300,100*256);word(0x302,0x3000);
  SmwRendererRecordSprite(0,2048,100,0,4);
  SmwRendererLatchOam();SmwRendererBeginFrame(g_ram);
  for(int y=1;y<=224;++y)SmwRendererCaptureLine(&test_ppu,y);
  SmwRendererDraw(surface,2134*4,native);
  const uint32_t *pixel=(const uint32_t *)surface;
  assert(pixel[100*2134+2048]==0xff0000);
  assert(pixel[100*2134]==0); /* Never alias x=2048 into native x=0. */
  /* Reused OAM with different attributes must not retain the far owner. */
  test_ppu.oam[129]=0x3400;test_ppu.cgram[161]=0x03e0;
  SmwRendererBeginFrame(g_ram);
  for(int y=1;y<=224;++y)SmwRendererCaptureLine(&test_ppu,y);
  SmwRendererDraw(surface,2134*4,native);
  assert(pixel[100*2134+2048]==0);
  assert(pixel[100*2134]==0x00ff00);
}
static void map16(void) {
  memset(g_ram,0,sizeof(g_ram));memset(rom,0,sizeof(rom));g_ram[0x5e]=31;
  rom[0x3da8]=0x00;rom[0x3da9]=0xc0; /* mode-0 screen table -> $00:C000 */
  rom[0x4000]=0x00;rom[0x4001]=0xc8;
  rom[0x4003]=0xb0;rom[0x4004]=0xc9;
  g_ram[0xc800]=1;g_ram[0xc9b0]=1;word(0xfc0,0x9000);
  unsigned base=(13<<15)+0x1000;
  for(unsigned i=0;i<8;++i)rom[base+i]=i+1;
  uint16_t tile=0;
  assert(SmwRendererMapTile(g_ram,0,0,0,&tile) && tile==0x0201);
  assert(SmwRendererMapTile(g_ram,0,0,8,&tile) && tile==0x0403);
  assert(SmwRendererMapTile(g_ram,0,8,0,&tile) && tile==0x0605);
  assert(SmwRendererMapTile(g_ram,0,8,8,&tile) && tile==0x0807);
  assert(SmwRendererMapTile(g_ram,0,256,0,&tile) && tile==0x0201);
  assert(!SmwRendererMapTile(g_ram,0,-1,0,&tile));
  assert(!SmwRendererMapTile(g_ram,0,0,432,&tile));
}
static void hud(void) {
  memset(g_ram,0,sizeof(g_ram));memset(&test_ppu,0,sizeof(test_ppu));
  g_ram[0x100]=20;g_ram[0x5e]=31;
  g_smw_video=(SmwVideoSettings){true,true,0};
  test_ppu.inidisp=15;test_ppu.bgmode=1;test_ppu.screenEnabled[0]=4;
  test_ppu.bgXsc[2]=0x20;
  test_ppu.cgram[1]=31;test_ppu.cgram[5]=0x03e0;test_ppu.cgram[9]=0x7c00;
  for(int y=0;y<8;++y)test_ppu.vram[8+y]=0xff;
  /* Distinct glyphs in the actual left, reserve-box and right HUD groups. */
  test_ppu.vram[0x2000+32+2]=1;
  test_ppu.vram[0x2000+32+15]=1|(1<<10);
  test_ppu.vram[0x2000+32+23]=1|(2<<10);
  for(int i=0;i<128;++i)test_ppu.oam[i*2]=0xf000;
  /* Reserve-item sprite must follow the centered BG3 box too. */
  g_ram[0xdc2]=1;test_ppu.oam[112]=0x0f78;test_ppu.oam[113]=0x3024;
  test_ppu.cgram[129]=31;
  for(int y=0;y<8;++y)test_ppu.vram[0x24*16+y]=0xff;
  const int widths[]={342,682,2134};
  for(unsigned i=0;i<sizeof(widths)/sizeof(*widths);++i) for(int camera=0;camera<=4096;camera+=4096) {
    int width=widths[i],extra=(width-256)/2;
    word(0x1a,camera);g_smw_viewport=(SmwViewport){width,extra,width/192.0};
    test_ppu.screenEnabled[0]=4|16;
    SmwRendererBeginFrame(g_ram);
    for(int y=1;y<=224;++y)SmwRendererCaptureLine(&test_ppu,y);
    SmwRendererDraw(surface,width*4,native);
    const uint32_t *pixel=(const uint32_t *)surface;
    assert(pixel[10*width+16]==0xff0000);
    assert(pixel[10*width+120+extra]==0x00ff00);
    assert(pixel[10*width+184+width-256]==0x0000ff);
    assert(pixel[10*width+120]==0);
    assert(pixel[16*width+120+extra]==0xff0000);
    assert(pixel[16*width+120]==0);
  }
}
int main(void) { geometry();spawn();objects();map16();hud();puts("geometry, spawn policy, signed OAM, Map16 and anchored HUD: passed");return 0; }
