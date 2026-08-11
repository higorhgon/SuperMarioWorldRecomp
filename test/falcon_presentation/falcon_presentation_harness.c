/* Standalone malformed-blob and deterministic ARGB8888 presentation tests. */
#include "../../src/mods/falcon/falcon_presentation.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { unsigned char data[32768]; size_t n; } Blob;
static void u16(Blob *b, unsigned v) { b->data[b->n++]=(unsigned char)v; b->data[b->n++]=(unsigned char)(v>>8); }
static void u32(Blob *b, uint32_t v) { unsigned i; for(i=0;i<4;i++)b->data[b->n++]=(unsigned char)(v>>(8*i)); }
static void f32(Blob *b, float v) { uint32_t u; memcpy(&u,&v,4);u32(b,u); }
static void name(Blob *b, const char *s) { size_t n=strlen(s);memcpy(b->data+b->n,s,n);memset(b->data+b->n+n,0,32-n);b->n+=32; }
static uint64_t hash(const uint32_t *p, size_t n) { uint64_t h=1469598103934665603ULL;while(n--){h^=*p++;h*=1099511628211ULL;}return h; }
static void triangle(Blob *b, unsigned texture, float z) {
    u16(b,0);u16(b,(unsigned)texture);
    f32(b,-1);f32(b,0);f32(b,z);f32(b,0);f32(b,0);
    f32(b,1);f32(b,0);f32(b,z);f32(b,1);f32(b,0);
    f32(b,0);f32(b,4);f32(b,z);f32(b,.5f);f32(b,1);
}

static Blob make_blob(void) {
    Blob b; unsigned i,j; uint32_t colors[7]={0xff3060c8u,0xc0ff8030u,0xc0ffb030u,0xc0fff020u,0xc030c0ffu,0xffa060ffu,0xffa060ffu};
    memset(&b,0,sizeof(b)); memcpy(b.data,"FLCN64B\0",8);b.n=8;
    u32(&b,4);u32(&b,26);u32(&b,2);u32(&b,7);u32(&b,2);u32(&b,1);u32(&b,1);u32(&b,1);u32(&b,3);
    for(i=0;i<26;i++){float x=0,y=0;u32(&b,i?0:0xffffffffu);if(i==FALCON_PRESENT_JOINT_CARRY_HAND){x=1;y=2;}else if(i==FALCON_PRESENT_JOINT_PUNCH_HAND){x=3;y=4;}else if(i==FALCON_PRESENT_JOINT_KICK_EFFECT){x=2;y=1;}for(j=0;j<3;j++)f32(&b,j==0?x:(j==1?y:0));for(j=0;j<3;j++)f32(&b,0);for(j=0;j<3;j++)f32(&b,1);u32(&b,0);u32(&b,i?0:1);}
    /* Near purple is intentionally stored before far blue: without the
     * stable far-to-near sort the far face would overwrite the nearer face. */
    triangle(&b,6,-1);triangle(&b,0,1);
    for(i=0;i<7;i++){
        if(i==0 || i==6){u16(&b,1);u16(&b,1);u32(&b,4);u32(&b,colors[i]);}
        else {
            /* Make every effect frame deliberately asymmetric.  The left
             * and right deterministic hashes below then prove U mirroring,
             * rather than merely testing a symmetric solid-colour card. */
            u16(&b,2);u16(&b,2);u32(&b,16);
            u32(&b,0xff000000u | ((0x20u+i*0x10u)<<16) | 0x000011u);
            u32(&b,0xff000000u | ((0x80u+i*0x10u)<<16) | 0x0000eeu);
            u32(&b,0xff000000u | ((0x10u+i*0x10u)<<16) | 0x001100u);
            u32(&b,0xff000000u | ((0x40u+i*0x10u)<<16) | 0x00ee00u);
        }
    }
    name(&b,"Wait");f32(&b,1);u32(&b,1);u32(&b,0);u32(&b,0);
    name(&b,"Dive");f32(&b,1);u32(&b,0);u32(&b,0);u32(&b,1);
    u16(&b,0xffff);u16(&b,4);u32(&b,0);u32(&b,1);
    f32(&b,0);f32(&b,1);f32(&b,0);f32(&b,10);f32(&b,0);f32(&b,0);u32(&b,1);
    return b;
}

static int bmp(const char *path, const uint32_t *pixels, int width, int height, int pitch) {
    FILE *f=fopen(path,"wb"); unsigned char h[54]={0};int x,y,row=width*3,pad=(4-row%4)%4,size=(row+pad)*height,neg=-height;
    if(!f)return 0;
    h[0]='B';h[1]='M';h[2]=(unsigned char)(54+size);h[3]=(unsigned char)((54+size)>>8);h[4]=(unsigned char)((54+size)>>16);h[5]=(unsigned char)((54+size)>>24);h[10]=54;h[14]=40;h[18]=(unsigned char)width;h[19]=(unsigned char)(width>>8);memcpy(h+22,&neg,4);h[26]=1;h[28]=24;h[34]=(unsigned char)size;h[35]=(unsigned char)(size>>8);h[36]=(unsigned char)(size>>16);h[37]=(unsigned char)(size>>24);fwrite(h,1,54,f);
    for(y=0;y<height;y++){for(x=0;x<width;x++){uint32_t q=pixels[y*pitch+x];fputc(q&255,f);fputc((q>>8)&255,f);fputc((q>>16)&255,f);}for(x=0;x<pad;x++)fputc(0,f);}fclose(f);return 1;
}

static void shell_marker(uint32_t *pixels, int width, int height, int pitch,
                         float center_x, float center_y, int radius) {
    int x, y, left=(int)center_x-radius, top=(int)center_y-radius;
    for(y=top;y<=top+radius*2;y++)for(x=left;x<=left+radius*2;x++) {
        if(x<0||y<0||x>=width||y>=height)continue;
        if(x==left||x==left+radius*2||y==top||y==top+radius*2)
            pixels[y*pitch+x]=0xff40f0a0u;
    }
}

static int synthetic(void) {
    Blob b=make_blob(); FalconPresentation *p; uint32_t frame[64*70]; FalconPresentationTarget t; FalconPresentationPose pose; float y,z,jx,jy,jright,jleft,cx,cy,px,py,ax,ay; uint64_t actual; unsigned i,purple=0;
    for(i=0;i<64*70;i++)frame[i]=0xff102030u;
    if(falcon_presentation_load_memory(b.data,b.n-1)!=NULL){fprintf(stderr,"FAIL truncated blob accepted\n");return 1;}
    b.data[0]='X';if(falcon_presentation_load_memory(b.data,b.n)!=NULL){fprintf(stderr,"FAIL magic accepted\n");return 1;}b.data[0]='F';
    b.data[20]=0;if(falcon_presentation_load_memory(b.data,b.n)!=NULL){fprintf(stderr,"FAIL zero texture count accepted\n");return 1;}b.data[20]=7;
    memset(b.data+44+26*48+2*64+(2*12+5*24),'A',32);
    if(falcon_presentation_load_memory(b.data,b.n)!=NULL){fprintf(stderr,"FAIL unterminated animation name accepted\n");return 1;}
    b=make_blob();
    p=falcon_presentation_load_memory(b.data,b.n);if(!p){fprintf(stderr,"FAIL valid blob rejected\n");return 1;}
    if(!falcon_presentation_root_delta(p,"Dive",1,&y,&z)||y!=10.5f||z!=0){fprintf(stderr,"FAIL root sample %f %f\n",y,z);falcon_presentation_destroy(p);return 1;}
    memset(&t,0,sizeof(t));t.framebuffer=frame;t.width=64;t.height=64;t.pitch_pixels=70;t.anchor_x=32;t.anchor_y=54;t.scale=1;
    pose.state=FALCON_PRESENT_KICK_AIR;pose.frame=12;pose.facing_right=1;
    if(!falcon_presentation_joint_screen_position(p,&pose,&t,FALCON_PRESENT_JOINT_KICK_EFFECT,&jx,&jy)||jx==t.anchor_x||jy==t.anchor_y){fprintf(stderr,"FAIL animated kick joint projection\n");falcon_presentation_destroy(p);return 1;}
    jright=jx;pose.facing_right=0;
    if(!falcon_presentation_joint_screen_position(p,&pose,&t,FALCON_PRESENT_JOINT_KICK_EFFECT,&jleft,&jy)||!(jright<t.anchor_x&&jleft>t.anchor_x)){fprintf(stderr,"FAIL authored-to-screen joint mirror\n");falcon_presentation_destroy(p);return 1;}
    pose.state=FALCON_PRESENT_IDLE;pose.facing_right=1;
    if(!falcon_presentation_joint_screen_position(p,&pose,&t,FALCON_PRESENT_JOINT_CARRY_HAND,&cx,&cy)||
       !falcon_presentation_joint_screen_position(p,&pose,&t,FALCON_PRESENT_JOINT_PUNCH_HAND,&px,&py)||
       !falcon_presentation_joint_screen_position(p,&pose,&t,FALCON_PRESENT_JOINT_KICK_EFFECT,&ax,&ay)||
       (cx==px&&cy==py)||(cx==ax&&cy==ay)||(px==ax&&py==ay)){fprintf(stderr,"FAIL calibrated attachment anchors\n");falcon_presentation_destroy(p);return 1;}
    for(i=0;i<64*70;i++)frame[i]=0xff102030u;
    pose.state=FALCON_PRESENT_PUNCH;pose.frame=42;pose.facing_right=1;if(!falcon_presentation_draw(p,&pose,&t)){fprintf(stderr,"FAIL draw\n");falcon_presentation_destroy(p);return 1;}
    actual=hash(frame,64*70);printf("synthetic punch framebuffer fnv64=%016llx\n",(unsigned long long)actual);
    /* Projected-foot grounding deliberately moved the old anchor-edge sample.
     * Require the near purple face to survive the far-to-near sort anywhere
     * in the raster, while the full hash pins its exact grounded projection. */
    for(i=0;i<64*70;i++)if(frame[i]==0xffa060ffu)++purple;
    if(!purple){fprintf(stderr,"FAIL depth sort removed near face\n");falcon_presentation_destroy(p);return 1;}
    if(actual!=0xf0ba4a2017bacf94ULL){fprintf(stderr,"FAIL punch attachment pixels drift\n");falcon_presentation_destroy(p);return 1;}
    for(i=0;i<64*70;i++)frame[i]=0xff102030u;
    pose.facing_right=0;if(!falcon_presentation_draw(p,&pose,&t)){fprintf(stderr,"FAIL left punch draw\n");falcon_presentation_destroy(p);return 1;}
    actual=hash(frame,64*70);printf("synthetic left-punch framebuffer fnv64=%016llx\n",(unsigned long long)actual);
    if(actual!=0x990c19ecc7f959acULL){fprintf(stderr,"FAIL left punch orientation drift\n");falcon_presentation_destroy(p);return 1;}
    for(i=0;i<64*70;i++)frame[i]=0xff102030u;
    pose.state=FALCON_PRESENT_KICK;pose.frame=12;pose.facing_right=1;if(!falcon_presentation_draw(p,&pose,&t)){fprintf(stderr,"FAIL ground kick draw\n");falcon_presentation_destroy(p);return 1;}
    actual=hash(frame,64*70);printf("synthetic ground-kick framebuffer fnv64=%016llx\n",(unsigned long long)actual);
    if(actual!=0x3241286b6f481919ULL){fprintf(stderr,"FAIL ground-kick attachment pixels drift\n");falcon_presentation_destroy(p);return 1;}
    for(i=0;i<64*70;i++)frame[i]=0xff102030u;
    pose.facing_right=0;if(!falcon_presentation_draw(p,&pose,&t)){fprintf(stderr,"FAIL left ground kick draw\n");falcon_presentation_destroy(p);return 1;}
    actual=hash(frame,64*70);printf("synthetic left-ground-kick framebuffer fnv64=%016llx\n",(unsigned long long)actual);
    if(actual!=0x19f15de4d3a8979aULL){fprintf(stderr,"FAIL left ground-kick orientation drift\n");falcon_presentation_destroy(p);return 1;}
    for(i=0;i<64*70;i++)frame[i]=0xff102030u;
    pose.state=FALCON_PRESENT_KICK_AIR;pose.frame=12;pose.facing_right=1;if(!falcon_presentation_draw(p,&pose,&t)){fprintf(stderr,"FAIL air kick draw\n");falcon_presentation_destroy(p);return 1;}
    actual=hash(frame,64*70);printf("synthetic air-kick framebuffer fnv64=%016llx\n",(unsigned long long)actual);
    if(actual!=0x6d3e732e023907f2ULL){fprintf(stderr,"FAIL air-kick attachment pixels drift\n");falcon_presentation_destroy(p);return 1;}
    for(i=0;i<64*70;i++)frame[i]=0xff102030u;
    pose.facing_right=0;if(!falcon_presentation_draw(p,&pose,&t)){fprintf(stderr,"FAIL left air kick draw\n");falcon_presentation_destroy(p);return 1;}
    actual=hash(frame,64*70);printf("synthetic left-air-kick framebuffer fnv64=%016llx\n",(unsigned long long)actual);
    if(actual!=0xfe10527f0d890218ULL){fprintf(stderr,"FAIL left air-kick orientation drift\n");falcon_presentation_destroy(p);return 1;}
    falcon_presentation_destroy(p);return 0;
}

static int synthetic_effect_sheet(const char *path) {
    Blob b=make_blob(); FalconPresentation *p=falcon_presentation_load_memory(b.data,b.n);
    uint32_t frame[64*416]; FalconPresentationTarget t; FalconPresentationPose poses[6]={
        {FALCON_PRESENT_PUNCH,42,1},{FALCON_PRESENT_PUNCH,42,0},
        {FALCON_PRESENT_KICK,12,1},{FALCON_PRESENT_KICK,12,0},
        {FALCON_PRESENT_KICK_AIR,12,1},{FALCON_PRESENT_KICK_AIR,12,0}};
    int i;
    if(!p){fprintf(stderr,"FAIL synthetic effect blob rejected\n");return 1;}
    for(i=0;i<64*416;i++)frame[i]=0xff102030u;
    memset(&t,0,sizeof(t));t.framebuffer=frame;t.width=384;t.height=64;t.pitch_pixels=416;t.anchor_y=54;t.scale=1;
    for(i=0;i<6;i++){t.anchor_x=32+64*i;if(!falcon_presentation_draw(p,&poses[i],&t)){fprintf(stderr,"FAIL synthetic effect sheet draw\n");falcon_presentation_destroy(p);return 1;}}
    if(!bmp(path,frame,384,64,416)){fprintf(stderr,"FAIL synthetic effect sheet capture\n");falcon_presentation_destroy(p);return 1;}
    printf("synthetic effect sheet fnv64=%016llx bmp=%s\n",(unsigned long long)hash(frame,64*416),path);
    falcon_presentation_destroy(p);return 0;
}

int main(int argc, char **argv) {
    if(argc==1)return synthetic();
    if(argc==3&&!strcmp(argv[1],"--synthetic-effect-sheet"))return synthetic_effect_sheet(argv[2]);
    if(argc==4&&!strcmp(argv[1],"--blob")){FalconPresentation*p=falcon_presentation_load_file(argv[2]);uint32_t frame[128*136];FalconPresentationTarget t;FalconPresentationPose pose;int i;if(!p){fprintf(stderr,"FAIL owner blob rejected\n");return 1;}for(i=0;i<128*136;i++)frame[i]=0xff183050u;memset(&t,0,sizeof(t));t.framebuffer=frame;t.width=128;t.height=128;t.pitch_pixels=136;t.anchor_x=64;t.anchor_y=108;t.scale=2;t.yaw_degrees=88;pose.state=FALCON_PRESENT_DIVE;pose.frame=13;pose.facing_right=1;if(!falcon_presentation_draw(p,&pose,&t)||!bmp(argv[3],frame,128,128,136)){fprintf(stderr,"FAIL visual capture\n");falcon_presentation_destroy(p);return 1;}printf("owner framebuffer fnv64=%016llx bmp=%s\n",(unsigned long long)hash(frame,128*136),argv[3]);falcon_presentation_destroy(p);return 0;}
    if(argc==4&&!strcmp(argv[1],"--sheet")){FalconPresentation*p=falcon_presentation_load_file(argv[2]);uint32_t frame[192*800];FalconPresentationTarget t;FalconPresentationPose poses[6]={{FALCON_PRESENT_IDLE,0,1},{FALCON_PRESENT_RUN,6,1},{FALCON_PRESENT_PUNCH,42,1},{FALCON_PRESENT_KICK,12,1},{FALCON_PRESENT_DIVE,13,1},{FALCON_PRESENT_IDLE,0,0}};int i;if(!p){fprintf(stderr,"FAIL owner blob rejected\n");return 1;}for(i=0;i<192*800;i++)frame[i]=0xff183050u;memset(&t,0,sizeof(t));t.framebuffer=frame;t.width=768;t.height=192;t.pitch_pixels=800;t.anchor_y=166;t.scale=2.5f;t.yaw_degrees=88;for(i=0;i<6;i++){t.anchor_x=64+128*i;if(!falcon_presentation_draw(p,&poses[i],&t)){fprintf(stderr,"FAIL sheet draw\n");falcon_presentation_destroy(p);return 1;}}if(!bmp(argv[3],frame,768,192,800)){fprintf(stderr,"FAIL sheet capture\n");falcon_presentation_destroy(p);return 1;}printf("owner sheet fnv64=%016llx bmp=%s\n",(unsigned long long)hash(frame,192*800),argv[3]);falcon_presentation_destroy(p);return 0;}
    if(argc==4&&!strcmp(argv[1],"--death-sheet")){FalconPresentation*p=falcon_presentation_load_file(argv[2]);uint32_t frame[192*544];FalconPresentationTarget t;FalconPresentationPose pose;int i;unsigned f;if(!p){fprintf(stderr,"FAIL owner blob rejected\n");return 1;}for(i=0;i<192*544;i++)frame[i]=0xff183050u;memset(&t,0,sizeof(t));t.framebuffer=frame;t.width=512;t.height=192;t.pitch_pixels=544;t.scale=2.5f;t.yaw_degrees=88;t.tumble_center_y=-40;pose.state=FALCON_PRESENT_FALL;pose.facing_right=1;for(i=0;i<4;i++){f=(unsigned)i*5u;t.anchor_x=64+128*i;t.anchor_y=100+(.30f*f+.018f*f*f)*2.5f;t.tumble_radians=f*(18.0f*3.14159265358979323846f/180.0f);pose.frame=f*.5f;if(!falcon_presentation_draw(p,&pose,&t)){fprintf(stderr,"FAIL death sheet draw\n");falcon_presentation_destroy(p);return 1;}}if(!bmp(argv[3],frame,512,192,544)){fprintf(stderr,"FAIL death sheet capture\n");falcon_presentation_destroy(p);return 1;}printf("owner death sheet fnv64=%016llx bmp=%s\n",(unsigned long long)hash(frame,192*544),argv[3]);falcon_presentation_destroy(p);return 0;}
    if(argc==4&&!strcmp(argv[1],"--feedback-sheet")){FalconPresentation*p=falcon_presentation_load_file(argv[2]);uint32_t frame[192*800];FalconPresentationTarget t;FalconPresentationPose poses[6]={{FALCON_PRESENT_PUNCH,42,1},{FALCON_PRESENT_PUNCH,42,0},{FALCON_PRESENT_KICK,12,1},{FALCON_PRESENT_KICK,12,0},{FALCON_PRESENT_KICK_AIR,12,1},{FALCON_PRESENT_KICK_AIR,12,0}};int i;if(!p){fprintf(stderr,"FAIL owner blob rejected\n");return 1;}for(i=0;i<192*800;i++)frame[i]=0xff183050u;memset(&t,0,sizeof(t));t.framebuffer=frame;t.width=768;t.height=192;t.pitch_pixels=800;t.anchor_y=166;t.scale=2.5f;t.yaw_degrees=88;for(i=0;i<6;i++){t.anchor_x=64+128*i;if(!falcon_presentation_draw(p,&poses[i],&t)){fprintf(stderr,"FAIL feedback sheet draw\n");falcon_presentation_destroy(p);return 1;}}if(!bmp(argv[3],frame,768,192,800)){fprintf(stderr,"FAIL feedback sheet capture\n");falcon_presentation_destroy(p);return 1;}printf("owner feedback sheet fnv64=%016llx bmp=%s\n",(unsigned long long)hash(frame,192*800),argv[3]);falcon_presentation_destroy(p);return 0;}
    if(argc==4&&!strcmp(argv[1],"--motion-sheet")){FalconPresentation*p=falcon_presentation_load_file(argv[2]);uint32_t frame[192*1312];FalconPresentationTarget t;FalconPresentationPose poses[10]={{FALCON_PRESENT_IDLE,0,1},{FALCON_PRESENT_RUN,0,1},{FALCON_PRESENT_RUN,6,1},{FALCON_PRESENT_RUN,12,1},{FALCON_PRESENT_RUN,18,1},{FALCON_PRESENT_KICK,0,1},{FALCON_PRESENT_KICK,6,1},{FALCON_PRESENT_KICK,12,1},{FALCON_PRESENT_KICK,18,1},{FALCON_PRESENT_KICK,24,1}};int i;if(!p){fprintf(stderr,"FAIL owner blob rejected\n");return 1;}for(i=0;i<192*1312;i++)frame[i]=0xff183050u;memset(&t,0,sizeof(t));t.framebuffer=frame;t.width=1280;t.height=192;t.pitch_pixels=1312;t.anchor_y=166;t.scale=2.5f;t.yaw_degrees=88;for(i=0;i<10;i++){t.anchor_x=64+128*i;if(!falcon_presentation_draw(p,&poses[i],&t)){fprintf(stderr,"FAIL motion sheet draw\n");falcon_presentation_destroy(p);return 1;}}if(!bmp(argv[3],frame,1280,192,1312)){fprintf(stderr,"FAIL motion sheet capture\n");falcon_presentation_destroy(p);return 1;}printf("owner motion sheet fnv64=%016llx bmp=%s\n",(unsigned long long)hash(frame,192*1312),argv[3]);falcon_presentation_destroy(p);return 0;}
    if(argc==4&&!strcmp(argv[1],"--attachment-sheet")){FalconPresentation*p=falcon_presentation_load_file(argv[2]);uint32_t frame[192*1184];FalconPresentationTarget t;FalconPresentationPose poses[9]={{FALCON_PRESENT_PUNCH,42,1},{FALCON_PRESENT_PUNCH,48,1},{FALCON_PRESENT_PUNCH,54,1},{FALCON_PRESENT_KICK,12,1},{FALCON_PRESENT_KICK,18,1},{FALCON_PRESENT_KICK,24,1},{FALCON_PRESENT_KICK_AIR,12,1},{FALCON_PRESENT_KICK_AIR,18,1},{FALCON_PRESENT_KICK_AIR,24,1}};int i;if(!p){fprintf(stderr,"FAIL owner blob rejected\n");return 1;}for(i=0;i<192*1184;i++)frame[i]=0xff183050u;memset(&t,0,sizeof(t));t.framebuffer=frame;t.width=1152;t.height=192;t.pitch_pixels=1184;t.anchor_y=166;t.scale=2.5f;t.yaw_degrees=88;for(i=0;i<9;i++){t.anchor_x=64+128*i;if(!falcon_presentation_draw(p,&poses[i],&t)){fprintf(stderr,"FAIL attachment sheet draw\n");falcon_presentation_destroy(p);return 1;}}if(!bmp(argv[3],frame,1152,192,1184)){fprintf(stderr,"FAIL attachment sheet capture\n");falcon_presentation_destroy(p);return 1;}printf("owner attachment sheet fnv64=%016llx bmp=%s\n",(unsigned long long)hash(frame,192*1184),argv[3]);falcon_presentation_destroy(p);return 0;}
    if(argc==4&&!strcmp(argv[1],"--carry-sheet")){FalconPresentation*p=falcon_presentation_load_file(argv[2]);uint32_t frame[192*288];FalconPresentationTarget t;FalconPresentationPose poses[2]={{FALCON_PRESENT_IDLE,0,1},{FALCON_PRESENT_IDLE,0,0}};int i;if(!p){fprintf(stderr,"FAIL owner blob rejected\n");return 1;}for(i=0;i<192*288;i++)frame[i]=0xff183050u;memset(&t,0,sizeof(t));t.framebuffer=frame;t.width=256;t.height=192;t.pitch_pixels=288;t.anchor_y=166;t.scale=2.5f;t.yaw_degrees=88;for(i=0;i<2;i++){float x,y;t.anchor_x=64+128*i;if(!falcon_presentation_draw(p,&poses[i],&t)||!falcon_presentation_joint_screen_position(p,&poses[i],&t,FALCON_PRESENT_JOINT_CARRY_HAND,&x,&y)){fprintf(stderr,"FAIL carry sheet draw\n");falcon_presentation_destroy(p);return 1;}x+=poses[i].facing_right?-5.0f:5.0f;y-=5.0f;shell_marker(frame,256,192,288,x,y,16);}if(!bmp(argv[3],frame,256,192,288)){fprintf(stderr,"FAIL carry sheet capture\n");falcon_presentation_destroy(p);return 1;}printf("owner carry sheet fnv64=%016llx bmp=%s\n",(unsigned long long)hash(frame,192*288),argv[3]);falcon_presentation_destroy(p);return 0;}
    fprintf(stderr,"usage: %s [--synthetic-effect-sheet output.bmp | --blob|--sheet|--death-sheet|--feedback-sheet|--motion-sheet|--attachment-sheet|--carry-sheet falcon_runtime.bin output.bmp]\n",argv[0]);return 2;
}
