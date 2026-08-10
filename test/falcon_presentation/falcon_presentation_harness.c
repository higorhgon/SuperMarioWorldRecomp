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
    Blob b; unsigned i,j; uint32_t colors[6]={0xff3060c8u,0xc0ff8030u,0xc0ffb030u,0xc0fff020u,0xc030c0ffu,0xffa060ffu};
    memset(&b,0,sizeof(b)); memcpy(b.data,"FLCN64B\0",8);b.n=8;
    u32(&b,4);u32(&b,26);u32(&b,2);u32(&b,6);u32(&b,2);u32(&b,1);u32(&b,1);u32(&b,1);u32(&b,3);
    for(i=0;i<26;i++){u32(&b,i?0:0xffffffffu);for(j=0;j<3;j++)f32(&b,0);for(j=0;j<3;j++)f32(&b,0);for(j=0;j<3;j++)f32(&b,1);u32(&b,0);u32(&b,i?0:1);}
    /* Near purple is intentionally stored before far blue: without the
     * stable far-to-near sort the far face would overwrite the nearer face. */
    triangle(&b,5,-1);triangle(&b,0,1);
    for(i=0;i<6;i++){u16(&b,1);u16(&b,1);u32(&b,4);u32(&b,colors[i]);}
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

static int synthetic(void) {
    Blob b=make_blob(); FalconPresentation *p; uint32_t frame[64*70]; FalconPresentationTarget t; FalconPresentationPose pose; float y,z; uint64_t actual; unsigned i;
    for(i=0;i<64*70;i++)frame[i]=0xff102030u;
    if(falcon_presentation_load_memory(b.data,b.n-1)!=NULL){fprintf(stderr,"FAIL truncated blob accepted\n");return 1;}
    b.data[0]='X';if(falcon_presentation_load_memory(b.data,b.n)!=NULL){fprintf(stderr,"FAIL magic accepted\n");return 1;}b.data[0]='F';
    b.data[20]=0;if(falcon_presentation_load_memory(b.data,b.n)!=NULL){fprintf(stderr,"FAIL zero texture count accepted\n");return 1;}b.data[20]=6;
    memset(b.data+44+26*48+2*64+6*12,'A',32);
    if(falcon_presentation_load_memory(b.data,b.n)!=NULL){fprintf(stderr,"FAIL unterminated animation name accepted\n");return 1;}
    b=make_blob();
    p=falcon_presentation_load_memory(b.data,b.n);if(!p){fprintf(stderr,"FAIL valid blob rejected\n");return 1;}
    if(!falcon_presentation_root_delta(p,"Dive",1,&y,&z)||y!=10.5f||z!=0){fprintf(stderr,"FAIL root sample %f %f\n",y,z);falcon_presentation_destroy(p);return 1;}
    memset(&t,0,sizeof(t));t.framebuffer=frame;t.width=64;t.height=64;t.pitch_pixels=70;t.anchor_x=32;t.anchor_y=54;t.scale=1;
    pose.state=FALCON_PRESENT_PUNCH;pose.frame=42;pose.facing_right=1;if(!falcon_presentation_draw(p,&pose,&t)){fprintf(stderr,"FAIL draw\n");falcon_presentation_destroy(p);return 1;}
    actual=hash(frame,64*70);printf("synthetic framebuffer fnv64=%016llx\n",(unsigned long long)actual);
    if(frame[54*70+32]!=0xffa060ffu){fprintf(stderr,"FAIL depth sort foreground=%08x\n",frame[54*70+32]);falcon_presentation_destroy(p);return 1;}
    if(actual!=0x700fe30a06e7965bULL){fprintf(stderr,"FAIL framebuffer hash drift\n");falcon_presentation_destroy(p);return 1;}
    falcon_presentation_destroy(p);return 0;
}

int main(int argc, char **argv) {
    if(argc==1)return synthetic();
    if(argc==4&&!strcmp(argv[1],"--blob")){FalconPresentation*p=falcon_presentation_load_file(argv[2]);uint32_t frame[128*136];FalconPresentationTarget t;FalconPresentationPose pose;int i;if(!p){fprintf(stderr,"FAIL owner blob rejected\n");return 1;}for(i=0;i<128*136;i++)frame[i]=0xff183050u;memset(&t,0,sizeof(t));t.framebuffer=frame;t.width=128;t.height=128;t.pitch_pixels=136;t.anchor_x=64;t.anchor_y=108;t.scale=2;pose.state=FALCON_PRESENT_DIVE;pose.frame=13;pose.facing_right=1;if(!falcon_presentation_draw(p,&pose,&t)||!bmp(argv[3],frame,128,128,136)){fprintf(stderr,"FAIL visual capture\n");falcon_presentation_destroy(p);return 1;}printf("owner framebuffer fnv64=%016llx bmp=%s\n",(unsigned long long)hash(frame,128*136),argv[3]);falcon_presentation_destroy(p);return 0;}
    if(argc==4&&!strcmp(argv[1],"--sheet")){FalconPresentation*p=falcon_presentation_load_file(argv[2]);uint32_t frame[192*800];FalconPresentationTarget t;FalconPresentationPose poses[6]={{FALCON_PRESENT_IDLE,0,1},{FALCON_PRESENT_RUN,6,1},{FALCON_PRESENT_PUNCH,42,1},{FALCON_PRESENT_KICK,12,1},{FALCON_PRESENT_DIVE,13,1},{FALCON_PRESENT_IDLE,0,0}};int i;if(!p){fprintf(stderr,"FAIL owner blob rejected\n");return 1;}for(i=0;i<192*800;i++)frame[i]=0xff183050u;memset(&t,0,sizeof(t));t.framebuffer=frame;t.width=768;t.height=192;t.pitch_pixels=800;t.anchor_y=166;t.scale=2.5f;for(i=0;i<6;i++){t.anchor_x=64+128*i;if(!falcon_presentation_draw(p,&poses[i],&t)){fprintf(stderr,"FAIL sheet draw\n");falcon_presentation_destroy(p);return 1;}}if(!bmp(argv[3],frame,768,192,800)){fprintf(stderr,"FAIL sheet capture\n");falcon_presentation_destroy(p);return 1;}printf("owner sheet fnv64=%016llx bmp=%s\n",(unsigned long long)hash(frame,192*800),argv[3]);falcon_presentation_destroy(p);return 0;}
    fprintf(stderr,"usage: %s [--blob|--sheet falcon_runtime.bin output.bmp]\n",argv[0]);return 2;
}
