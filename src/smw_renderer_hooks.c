#include "smw_renderer.h"
#include "cpu_state.h"
#include "snes/interp_bridge.h"

static unsigned r8(CpuState *c,unsigned a) { return cpu_read8(c,0x7e,(uint16_t)a); }
static unsigned r16(CpuState *c,unsigned a) { return r8(c,a)|(r8(c,a+1)<<8); }
static bool active(CpuState *c) {
  return g_smw_video.enabled && g_smw_viewport.width>256 && r8(c,0x100)==0x14;
}
static int left_margin(CpuState *c) {
  return r8(c,0x5b)&1 ? g_smw_viewport.extra :
      SmwViewOffset(g_smw_viewport,r16(c,0x1a),(r8(c,0x5e)+1)*256);
}

void SmwRendererDrawInfo(CpuState *c) {
  if(!active(c)) return;
  unsigned slot=c->X&0xffff;
  if(slot>=12) return;
  int x=(int16_t)((r8(c,0xe4+slot)|(r8(c,0x14e0+slot)<<8))-r16(c,0x1a));
  int left=left_margin(c);
  bool draw=x>=-left-64 && x<g_smw_viewport.width-left;
  if(draw) {
    unsigned first=r8(c,0x15ea+slot), end=256;
    for(unsigned i=0;i<12;++i) {
      unsigned next=r8(c,0x15ea+i);
      if(next>first && next<end) end=next;
    }
    int y=(int16_t)((r8(c,0xd8+slot)|(r8(c,0x14d4+slot)<<8))-r16(c,0x1c));
    SmwRendererRecordSprite(slot,x,y,first,end);
  }
  c->_flag_Z=draw?1:0;
  c->_flag_C=draw?0:1;
  cpu_write8(c,0x7e,0x15c4+slot,draw?0:1);
}

void SmwRendererGuestHook(CpuState *c,uint32_t pc) {
  static int fireball_x;
  if(!active(c)) return;
  pc &= 0x7fffff;
  if(pc==0x02A1BE) {
    unsigned slot=c->X&255;
    if(slot>=10) return;
    fireball_x=(int16_t)((r8(c,0x171f+slot)|(r8(c,0x1733+slot)<<8))-r16(c,0x1a));
    int left=g_smw_video.adaptive_spawns?left_margin(c):0;
    int right=g_smw_video.adaptive_spawns?g_smw_viewport.width-left:256;
    c->_flag_Z=fireball_x>=-left && fireball_x<right;
    return;
  }
  if(pc==0x02A204) {
    unsigned index=c->Y&255;
    SmwRendererRecordOam(index/4,fireball_x,r16(c,0x200+index),r16(c,0x202+index));
    return;
  }
  if(pc==0x01B844) {
    unsigned index=c->Y&255;
    int x=(int16_t)(r16(c,c->D+4)-r16(c,0x1a));
    SmwRendererRecordOam(64+index/4,x,r16(c,0x300+index),r16(c,0x302+index));
    return;
  }
  if(!g_smw_video.adaptive_spawns || !g_smw_viewport.extra) return;
  if(pc==0x02A82E && !(r8(c,0x5b)&1)) {
    /* Select every visible record on the native loader's ordinary sweep.
     * Keep native allocation, initialization, load flags and respawn policy.
     * Excluded records compare below $FF and continue instead of ending the
     * list early. No camera shifting, replayed game frames or forged OAM. */
    unsigned pointer=r16(c,c->D+0xce)|(r8(c,c->D+0xd0)<<16);
    unsigned address=pointer+c->Y;
    unsigned a=cpu_read8(c,address>>16,address&65535);
    unsigned b=cpu_read8(c,(address+1)>>16,(address+1)&65535);
    unsigned id=cpu_read8(c,(address+2)>>16,(address+2)&65535);
    if(a==255) return;
    int x=(((a&2)<<3)|(b&15))*256+(b&0xf0);
    int camera=r16(c,0x1a), left=left_margin(c);
    bool visible=x>=camera-left-32 && x<camera+g_smw_viewport.width-left+32;
    /* Scroll commands/generators remain native: starting a level's end
     * command on a 100:9 monitor would change progression, not enemies. */
    if(id>=0xcb) {
      unsigned direction=r8(c,0x55);
      int edge=(camera+(direction==0?-48:direction==2?288:0))&~15;
      visible=x==edge;
    }
    cpu_write8(c,0x7e,c->D,(uint8_t)(x&0xf0));
    cpu_write8(c,0x7e,c->D+1,visible?(uint8_t)(x>>8):255);
  } else if(pc==0x01AC7C || pc==0x02D076 || pc==0x03B8A8) {
    if(r8(c,0x5b)&1) return;
    unsigned slot=c->X&0xffff, side=c->Y&7;
    if(slot>=12) return;
    unsigned table=pc==0x01AC7C?0xac11:pc==0x02D076?0xd007:0xb83f;
    int bound=(int16_t)(cpu_read8(c,c->DB,table+side)|
                          (cpu_read8(c,c->DB,table+side+8)<<8));
    int delta=(int16_t)(bound+r16(c,0x1a)-(r8(c,0xe4+slot)|(r8(c,0x14e0+slot)<<8)));
    int left=left_margin(c);
    int extra=(side&1?left:g_smw_viewport.width-256-left)+32;
    int threshold=bound<=-64?extra:bound+64+extra;
    bool erase=side&1?delta>=threshold:delta<-extra;
    cpu_write8(c,0x7e,c->D,erase?128:0);
  }
}
void SmwRendererInstallHooks(void) {
  const uint32_t pcs[]={0x02A82E,0x01B844,0x01AC7C,0x02D076,0x03B8A8,0x02A1BE,0x02A204};
  for(unsigned i=0;i<sizeof(pcs)/sizeof(*pcs);++i)
    interp_bridge_set_pre_opcode_hook(pcs[i],SmwRendererGuestHook);
}
