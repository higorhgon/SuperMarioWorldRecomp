/* Strict v4 owner-cache loader and small pitch-aware software mesh rasterizer. */
#include "falcon_presentation.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FALCON_MAGIC "FLCN64B"
#define FALCON_VERSION 4u
#define FALCON_JOINTS 26u
#define FALCON_ROOT 0xffffu
#define MAX_BLOB_BYTES (64u * 1024u * 1024u)
#define MAX_TRIANGLES 10000u
#define MAX_TEXTURES 256u
#define MAX_ANIMATIONS 128u
#define MAX_TRACKS 100000u
#define MAX_SEGMENTS 1000000u

typedef struct { int parent; float t[3], r[3], s[3]; uint32_t first, count; } Joint;
typedef struct { float p[3], uv[2]; } Vertex;
typedef struct { uint16_t joint, texture; Vertex v[3]; } Triangle;
typedef struct { uint16_t w, h; uint32_t *pixels; } Texture;
typedef struct { char name[32]; float duration; uint32_t loop, first, count; } Animation;
typedef struct { uint16_t joint, kind; uint32_t first, count; } Track;
typedef struct { float frame, duration, base, target, rate0, rate1; uint32_t kind; } Segment;
typedef struct { float m[16]; } Mat4;
typedef struct { const uint8_t *p; size_t n, at; int ok; } Reader;

struct FalconPresentation {
    Joint *joints; Triangle *triangles; Texture *textures; Animation *anims;
    Track *tracks; Segment *segments;
    uint32_t triangles_n, textures_n, anims_n, tracks_n, segments_n;
    uint32_t punch_first, punch_count;
    float bind_min[3], bind_max[3];
};

static uint16_t u16(Reader *r) { uint16_t v; if (!r->ok || r->at > r->n || r->n-r->at < 2) { r->ok=0; return 0; } v=(uint16_t)r->p[r->at]|((uint16_t)r->p[r->at+1]<<8); r->at+=2; return v; }
static uint32_t u32(Reader *r) { uint32_t v; if (!r->ok || r->at > r->n || r->n-r->at < 4) { r->ok=0; return 0; } v=(uint32_t)r->p[r->at]|((uint32_t)r->p[r->at+1]<<8)|((uint32_t)r->p[r->at+2]<<16)|((uint32_t)r->p[r->at+3]<<24); r->at+=4; return v; }
static float f32(Reader *r) { uint32_t bits=u32(r); float v=0; memcpy(&v,&bits,4); if (!isfinite(v)) r->ok=0; return v; }
static int bytes(Reader *r, void *out, size_t n) { if (!r->ok || r->at > r->n || n > r->n-r->at) { r->ok=0; return 0; } memcpy(out,r->p+r->at,n); r->at+=n; return 1; }
static int range_ok(uint32_t first, uint32_t count, uint32_t total) { return first <= total && count <= total-first; }

static void release(FalconPresentation *p) {
    uint32_t i;
    if (!p) return;
    if (p->textures)
        for (i=0;i<p->textures_n;i++) free(p->textures[i].pixels);
    free(p->joints); free(p->triangles); free(p->textures); free(p->anims);
    free(p->tracks); free(p->segments); free(p);
}
void falcon_presentation_destroy(FalconPresentation *p) { release(p); }

static Mat4 identity(void) { Mat4 a; int i; memset(&a,0,sizeof(a)); for(i=0;i<4;i++) a.m[i*4+i]=1; return a; }
static Mat4 mul(Mat4 a, Mat4 b) { Mat4 o; int i,j,k; for(i=0;i<4;i++) for(j=0;j<4;j++){ float s=0; for(k=0;k<4;k++) s+=a.m[i*4+k]*b.m[k*4+j]; o.m[i*4+j]=s; } return o; }
static Mat4 local(const float t[3], const float r[3], const float s[3]) {
    Mat4 tr=identity(), rx=identity(), ry=identity(), rz=identity(), sc=identity();
    float cx=cosf(r[0]),sx=sinf(r[0]),cy=cosf(r[1]),sy=sinf(r[1]),cz=cosf(r[2]),sz=sinf(r[2]);
    tr.m[3]=t[0]; tr.m[7]=t[1]; tr.m[11]=t[2];
    rx.m[5]=cx;rx.m[6]=-sx;rx.m[9]=sx;rx.m[10]=cx;
    ry.m[0]=cy;ry.m[2]=sy;ry.m[8]=-sy;ry.m[10]=cy;
    rz.m[0]=cz;rz.m[1]=-sz;rz.m[4]=sz;rz.m[5]=cz;
    sc.m[0]=s[0];sc.m[5]=s[1];sc.m[10]=s[2];
    return mul(tr,mul(rz,mul(ry,mul(rx,sc))));
}
static void point(Mat4 m, const float in[3], float out[3]) { out[0]=m.m[0]*in[0]+m.m[1]*in[1]+m.m[2]*in[2]+m.m[3]; out[1]=m.m[4]*in[0]+m.m[5]*in[1]+m.m[6]*in[2]+m.m[7]; out[2]=m.m[8]*in[0]+m.m[9]*in[1]+m.m[10]*in[2]+m.m[11]; }

static void matrices(const FalconPresentation *p, float t[FALCON_JOINTS][3], float r[FALCON_JOINTS][3], float s[FALCON_JOINTS][3], Mat4 out[FALCON_JOINTS]) {
    uint32_t i; for(i=0;i<FALCON_JOINTS;i++) { Mat4 m=local(t[i],r[i],s[i]); out[i]=p->joints[i].parent<0 ? m : mul(out[p->joints[i].parent],m); }
}
static void bounds(const FalconPresentation *p, Mat4 world[FALCON_JOINTS], float lo[3], float hi[3]) {
    uint32_t i,j; for(j=0;j<3;j++){lo[j]=FLT_MAX;hi[j]=-FLT_MAX;} for(i=0;i<p->triangles_n;i++) for(j=0;j<3;j++){float q[3];int k;point(world[p->triangles[i].joint],p->triangles[i].v[j].p,q);for(k=0;k<3;k++){if(q[k]<lo[k])lo[k]=q[k];if(q[k]>hi[k])hi[k]=q[k];}}
}

static int name_ok(const char name[32]) { unsigned i; for(i=0;i<32;i++) { if (!name[i]) return i>0; if ((unsigned char)name[i]<32 || (unsigned char)name[i]>126) return 0; } return 0; }
static int parse(const void *data, size_t size, FalconPresentation **out) {
    Reader r; FalconPresentation *p; uint8_t magic[8]; uint32_t version,i,j;
    if (!data || size < 44 || size > MAX_BLOB_BYTES) return 0;
    r.p=(const uint8_t*)data;r.n=size;r.at=0;r.ok=1; if(!bytes(&r,magic,8) || memcmp(magic,FALCON_MAGIC,7) || magic[7]) return 0;
    version=u32(&r); if(version!=FALCON_VERSION) return 0;
    p=(FalconPresentation*)calloc(1,sizeof(*p)); if(!p) return 0;
    { uint32_t joints=u32(&r); p->triangles_n=u32(&r);p->textures_n=u32(&r);p->anims_n=u32(&r);p->tracks_n=u32(&r);p->segments_n=u32(&r);p->punch_first=u32(&r);p->punch_count=u32(&r);
      if(joints!=FALCON_JOINTS || !p->triangles_n || p->triangles_n>MAX_TRIANGLES || !p->textures_n || p->textures_n>MAX_TEXTURES || !p->anims_n || p->anims_n>MAX_ANIMATIONS || p->tracks_n>MAX_TRACKS || p->segments_n>MAX_SEGMENTS || p->punch_count!=3 || !range_ok(p->punch_first,3,p->textures_n) || !range_ok(p->punch_first+3,2,p->textures_n)) goto bad; }
    p->joints=(Joint*)calloc(FALCON_JOINTS,sizeof(*p->joints));p->triangles=(Triangle*)calloc(p->triangles_n,sizeof(*p->triangles));p->textures=(Texture*)calloc(p->textures_n,sizeof(*p->textures));p->anims=(Animation*)calloc(p->anims_n,sizeof(*p->anims));p->tracks=(Track*)calloc(p->tracks_n?p->tracks_n:1,sizeof(*p->tracks));p->segments=(Segment*)calloc(p->segments_n?p->segments_n:1,sizeof(*p->segments));
    if(!p->joints||!p->triangles||!p->textures||!p->anims||!p->tracks||!p->segments) goto bad;
    for(i=0;i<FALCON_JOINTS;i++){Joint*q=&p->joints[i];q->parent=(int32_t)u32(&r);for(j=0;j<3;j++)q->t[j]=f32(&r);for(j=0;j<3;j++)q->r[j]=f32(&r);for(j=0;j<3;j++)q->s[j]=f32(&r);q->first=u32(&r);q->count=u32(&r);if(q->parent < -1 || q->parent >= (int)i || !range_ok(q->first,q->count,p->triangles_n))r.ok=0;}
    for(i=0;i<p->triangles_n;i++){Triangle*q=&p->triangles[i];q->joint=u16(&r);q->texture=u16(&r);for(j=0;j<3;j++){unsigned k;for(k=0;k<3;k++)q->v[j].p[k]=f32(&r);for(k=0;k<2;k++)q->v[j].uv[k]=f32(&r);}if(q->joint>=FALCON_JOINTS || (q->texture!=FALCON_ROOT && q->texture>=p->textures_n))r.ok=0;}
    for(i=0;i<p->textures_n;i++){Texture*q=&p->textures[i];uint32_t n;uint64_t want; q->w=u16(&r);q->h=u16(&r);n=u32(&r);want=(uint64_t)q->w*q->h*4u; if(!q->w||!q->h||q->w>4096||q->h>4096||want!=n||n>MAX_BLOB_BYTES||n>r.n-r.at)goto bad;q->pixels=(uint32_t*)malloc(n);if(!q->pixels||!bytes(&r,q->pixels,n))goto bad;}
    for(i=0;i<p->anims_n;i++){Animation*q=&p->anims[i];bytes(&r,q->name,32);if(!memchr(q->name,0,sizeof(q->name)))r.ok=0;q->name[31]=0;q->duration=f32(&r);q->loop=u32(&r);q->first=u32(&r);q->count=u32(&r);if(!name_ok(q->name)||q->duration<0||q->loop>1||!range_ok(q->first,q->count,p->tracks_n))r.ok=0;for(j=0;j<i;j++)if(!strcmp(q->name,p->anims[j].name))r.ok=0;}
    for(i=0;i<p->tracks_n;i++){Track*q=&p->tracks[i];q->joint=u16(&r);q->kind=u16(&r);q->first=u32(&r);q->count=u32(&r);if((q->joint>=FALCON_JOINTS&&q->joint!=FALCON_ROOT)||q->kind>8||!q->count||!range_ok(q->first,q->count,p->segments_n))r.ok=0;}
    for(i=0;i<p->segments_n;i++){Segment*q=&p->segments[i];q->frame=f32(&r);q->duration=f32(&r);q->base=f32(&r);q->target=f32(&r);q->rate0=f32(&r);q->rate1=f32(&r);q->kind=u32(&r);if(q->frame<0||q->duration<0||q->kind>3)r.ok=0;}
    for(i=0;i<p->tracks_n;i++)for(j=1;j<p->tracks[i].count;j++)if(p->segments[p->tracks[i].first+j].frame<p->segments[p->tracks[i].first+j-1].frame)r.ok=0;
    if(!r.ok||r.at!=r.n)goto bad;
    { float t[FALCON_JOINTS][3],rr[FALCON_JOINTS][3],s[FALCON_JOINTS][3];Mat4 w[FALCON_JOINTS];for(i=0;i<FALCON_JOINTS;i++){memcpy(t[i],p->joints[i].t,12);memcpy(rr[i],p->joints[i].r,12);memcpy(s[i],p->joints[i].s,12);}matrices(p,t,rr,s,w);bounds(p,w,p->bind_min,p->bind_max);if(p->bind_max[1]-p->bind_min[1]<1.0f)goto bad;}
    *out=p;return 1;
bad: release(p);return 0;
}

FalconPresentation *falcon_presentation_load_memory(const void *data,size_t size){FalconPresentation*p=0;return parse(data,size,&p)?p:0;}
FalconPresentation *falcon_presentation_load_file(const char *path){FILE*f;long n;uint8_t*b;FalconPresentation*p;if(!path||(f=fopen(path,"rb"))==0)return 0;if(fseek(f,0,SEEK_END)|| (n=ftell(f))<=0 || (size_t)n>MAX_BLOB_BYTES || fseek(f,0,SEEK_SET)){fclose(f);return 0;}b=(uint8_t*)malloc((size_t)n);if(!b||fread(b,1,(size_t)n,f)!=(size_t)n){free(b);fclose(f);return 0;}fclose(f);p=falcon_presentation_load_memory(b,(size_t)n);free(b);return p;}

const char *falcon_presentation_animation(FalconPresentationState state){switch(state){case FALCON_PRESENT_WALK:return "Walk2";case FALCON_PRESENT_RUN:return "Run";case FALCON_PRESENT_JUMP:return "JumpF";case FALCON_PRESENT_FALL:return "Fall";case FALCON_PRESENT_PUNCH:return "FalconPunchGround";case FALCON_PRESENT_KICK:return "DownSpecial";case FALCON_PRESENT_DIVE:return "FalconDive";case FALCON_PRESENT_DIVE_CATCH:return "CatchingEnemyWhileDiving";case FALCON_PRESENT_DIVE_THROW:return "FalconDiveEnd1";case FALCON_PRESENT_JAB:return "Jab1";case FALCON_PRESENT_FTILT:return "AttackS3";case FALCON_PRESENT_NAIR:return "AttackAirN";case FALCON_PRESENT_FAIR:return "AttackAirF";case FALCON_PRESENT_BAIR:return "AttackAirB";case FALCON_PRESENT_DAIR:return "AttackAirD";default:return "Wait";}}
static const Animation *find(const FalconPresentation*p,const char*n){uint32_t i;for(i=0;i<p->anims_n;i++)if(!strcmp(p->anims[i].name,n))return&p->anims[i];return 0;}
static float sample(const FalconPresentation*p,const Track*t,float frame){const Segment*s=p->segments+t->first;uint32_t i;if(frame<=s[0].frame)return s[0].base;for(i=0;i<t->count;i++){const Segment*q=&s[i];float e,a;if(frame<q->frame)return i?s[i-1].target:q->base;if(q->duration<=0)continue;e=frame-q->frame;if(e<=q->duration){a=e/q->duration;if(q->kind==1)return q->base+(q->target-q->base)*a;if(q->kind==2){float a2=a*a,a3=a2*a;return(2*a3-3*a2+1)*q->base+(a3-2*a2+a)*q->duration*q->rate0+(-2*a3+3*a2)*q->target+(a3-a2)*q->duration*q->rate1;}return q->kind==3&&e>=q->duration?q->target:q->base;}}return s[t->count-1].target;}
int falcon_presentation_root_delta(const FalconPresentation*p,const char*name,float frame,float*dy,float*dz){const Animation*a;float old=frame>0?frame-1:frame,y=0,py=0,z=0,pz=0;uint32_t i;int hy=0,hz=0;if(dy)*dy=0;if(dz)*dz=0;if(!p||!name||!(a=find(p,name)))return 0;if(frame>a->duration)frame=a->duration;if(old>a->duration)old=a->duration;for(i=0;i<a->count;i++){const Track*t=&p->tracks[a->first+i];if(t->joint!=FALCON_ROOT)continue;if(t->kind==4){y=sample(p,t,frame);py=sample(p,t,old);hy=1;}if(t->kind==5){z=sample(p,t,frame);pz=sample(p,t,old);hz=1;}}if(!hy&&!hz)return 0;if(dy)*dy=(y-py)*1.05f;if(dz)*dz=(z-pz)*1.05f;return 1;}

static uint32_t over(uint32_t d,uint32_t s){unsigned a=s>>24,ia=255-a;unsigned r=(((s>>16)&255)*a+((d>>16)&255)*ia+127)/255,g=(((s>>8)&255)*a+((d>>8)&255)*ia+127)/255,b=((s&255)*a+(d&255)*ia+127)/255;return 0xff000000u|(r<<16)|(g<<8)|b;}
typedef struct {float x,y,z,u,v;} PV;
typedef struct { PV v[3]; const Texture *texture; float depth; } DrawTriangle;
static float edge(PV a,PV b,float x,float y){return(x-a.x)*(b.y-a.y)-(y-a.y)*(b.x-a.x);}
static void tri(const FalconPresentationTarget*t,const Texture*tex,PV a,PV b,PV c){float area=edge(a,b,c.x,c.y);int x0,x1,y0,y1,x,y;if(fabsf(area)<.0001f)return;x0=(int)floorf(fminf(a.x,fminf(b.x,c.x)));x1=(int)ceilf(fmaxf(a.x,fmaxf(b.x,c.x)));y0=(int)floorf(fminf(a.y,fminf(b.y,c.y)));y1=(int)ceilf(fmaxf(a.y,fmaxf(b.y,c.y)));if(x0<0)x0=0;if(y0<0)y0=0;if(x1>=t->width)x1=t->width-1;if(y1>=t->height)y1=t->height-1;for(y=y0;y<=y1;y++)for(x=x0;x<=x1;x++){float w0=edge(b,c,x+.5f,y+.5f)/area,w1=edge(c,a,x+.5f,y+.5f)/area,w2=1-w0-w1,u,v;int tx,ty;if(w0<0||w1<0||w2<0)continue;u=w0*a.u+w1*b.u+w2*c.u;v=w0*a.v+w1*b.v+w2*c.v;tx=(int)floorf(u);ty=(int)floorf(v);if(tx<0)tx=0;if(ty<0)ty=0;if(tx>=tex->w)tx=tex->w-1;if(ty>=tex->h)ty=tex->h-1;{uint32_t q=tex->pixels[ty*tex->w+tx];if(q>>24)t->framebuffer[y*t->pitch_pixels+x]=over(t->framebuffer[y*t->pitch_pixels+x],q);}}}
static void card(const FalconPresentationTarget*t,const Texture*tex,float cx,float cy,float w,float h){PV a={cx-w*.5f,cy-h,0,0,0},b={cx+w*.5f,cy-h,0,(float)tex->w-.01f,0},c={cx+w*.5f,cy,0,(float)tex->w-.01f,(float)tex->h-.01f},d={cx-w*.5f,cy,0,0,(float)tex->h-.01f};tri(t,tex,a,b,c);tri(t,tex,a,c,d);}
static void particle(const FalconPresentationTarget *t, uint32_t color,
                     float x, float y, float width, float height)
{
    Texture q;
    q.w = q.h = 1;
    q.pixels = &color;
    card(t, &q, x, y, width, height);
}

/* A compact host recreation of the NES renderer's f0 ImpactWave, f13
 * DustDashSmall + two-frame spark cadence, and Catch/Throw white impacts.
 * It intentionally uses generated ARGB colors rather than unrelated owner
 * particle assets. */
static void dive_particles(const FalconPresentationPose *pose,
                           const FalconPresentationTarget *t, float s, float dir)
{
    unsigned frame = pose->frame > 0.0f ? (unsigned)pose->frame : 0u;
    float ground = t->anchor_y;
    float middle = ground - 16.0f * s;
    if (pose->state == FALCON_PRESENT_DIVE) {
        if (frame < 6u)
            particle(t, (0xa0u - frame * 16u) << 24 | 0x00ffffffu,
                     t->anchor_x, ground - 1.0f*s,
                     (5.0f + frame) * s, 0.8f*s);
        if (frame >= 13u && frame < 45u) {
            unsigned phase = frame - 13u;
            if (phase < 6u)
                particle(t, (0xb8u - phase * 16u) << 24 | 0x00c07838u,
                         t->anchor_x - dir * (3.0f + phase) * s,
                         ground - 1.0f*s, (2.0f + .25f*phase)*s, .9f*s);
            if (phase < 20u) {
                float step = (float)(phase / 2u);
                float side = (phase & 2u) ? -1.0f : 1.0f;
                particle(t, 0xd8ffd848u,
                         t->anchor_x + dir*(4.0f + fmodf(step,3.0f))*s,
                         middle + side*(3.0f + fmodf(step,4.0f))*s,
                         (1.4f + .2f*fmodf(step,3.0f))*s,
                         (1.4f + .2f*fmodf(step,3.0f))*s);
                particle(t, 0xc0ffffffu,
                         t->anchor_x - dir*(2.0f + fmodf(step,2.0f))*s,
                         middle - side*5.0f*s, .9f*s, .9f*s);
            }
        }
        if (frame >= 45u && frame < 49u)
            particle(t, (0xd0u - (frame-45u)*32u) << 24 | 0x00ffffffu,
                     t->anchor_x + dir*5.0f*s, middle, (2.0f-(frame-45u)*.25f)*s,
                     (2.0f-(frame-45u)*.25f)*s);
    } else if (pose->state == FALCON_PRESENT_DIVE_CATCH && frame < 6u) {
        particle(t, (0xe0u-frame*20u) << 24 | 0x00ffd848u,
                 t->anchor_x+dir*5.0f*s,middle,(2.0f+.15f*frame)*s,(2.0f+.15f*frame)*s);
        particle(t, 0xb0ffffffu,t->anchor_x+dir*8.0f*s,middle-1.5f*s,.8f*s,.8f*s);
    } else if (pose->state == FALCON_PRESENT_DIVE_THROW && frame < 10u) {
        particle(t, (0xe8u-frame*18u) << 24 | 0x00ffffffu,
                 t->anchor_x+dir*6.0f*s,middle,(2.0f+.15f*frame)*s,(2.0f+.15f*frame)*s);
        particle(t, 0xb0ffd848u,t->anchor_x-dir*2.5f*s,middle-4.0f*s,.9f*s,.9f*s);
    }
}

static void effect(const FalconPresentation*p,const FalconPresentationPose*pose,const FalconPresentationTarget*t){float s=t->scale>0?t->scale:1,dir=pose->facing_right?1:-1;if(pose->state==FALCON_PRESENT_PUNCH&&pose->frame>=42&&pose->frame<55){const Texture*q=&p->textures[p->punch_first+((unsigned)pose->frame-42)%3];card(t,q,t->anchor_x+dir*18*s,t->anchor_y-19*s,24*s,24*s);}else if(pose->state==FALCON_PRESENT_KICK&&pose->frame>=12&&pose->frame<32){const Texture*q=&p->textures[p->punch_first+3+((unsigned)pose->frame-12)%2];card(t,q,t->anchor_x+dir*17*s,t->anchor_y-12*s,30*s,18*s);}else dive_particles(pose,t,s,dir);}
int falcon_presentation_draw(const FalconPresentation*p,const FalconPresentationPose*pose,const FalconPresentationTarget*t){
    float tr[FALCON_JOINTS][3],ro[FALCON_JOINTS][3],sc[FALCON_JOINTS][3],lo[3],hi[3],height,scale,dir;
    Mat4 w[FALCON_JOINTS]; const Animation*a; DrawTriangle *draws; uint32_t i;
    uint32_t gray=0xff3060c8u; Texture fallback;
    if(!p||!pose||!t||!t->framebuffer||t->width<=0||t->height<=0||t->pitch_pixels<t->width)return 0;
    a=find(p,falcon_presentation_animation(pose->state));if(!a)a=find(p,"Wait");if(!a)return 0;
    for(i=0;i<FALCON_JOINTS;i++){memcpy(tr[i],p->joints[i].t,12);memcpy(ro[i],p->joints[i].r,12);memcpy(sc[i],p->joints[i].s,12);}
    {float frame=pose->frame;if(a->loop&&a->duration>0)frame=fmodf(frame,a->duration);else if(frame>a->duration)frame=a->duration;for(i=0;i<a->count;i++){const Track*q=&p->tracks[a->first+i];float v=sample(p,q,frame);if(q->joint==FALCON_ROOT)continue;if(q->kind<3)ro[q->joint][q->kind]=v;else if(q->kind<6)tr[q->joint][q->kind-3]=v;else sc[q->joint][q->kind-6]=v;}}
    matrices(p,tr,ro,sc,w);bounds(p,w,lo,hi);height=p->bind_max[1]-p->bind_min[1];scale=(t->scale>0?t->scale:1)*32.0f/height;dir=pose->facing_right?1:-1;
    draws=(DrawTriangle*)malloc((size_t)p->triangles_n*sizeof(*draws)); if(!draws)return 0;
    fallback.w=fallback.h=1;fallback.pixels=&gray;
    for(i=0;i<p->triangles_n;i++){
        const Triangle*q=&p->triangles[i]; DrawTriangle *d=&draws[i]; uint32_t j;
        d->texture=q->texture==FALCON_ROOT?&fallback:&p->textures[q->texture]; d->depth=0;
        for(j=0;j<3;j++){float z,x,y,pt[3];point(w[q->joint],q->v[j].p,pt);x=(pt[0]-(lo[0]+hi[0])*.5f)*dir;y=pt[1]-lo[1];z=(pt[2]-(lo[2]+hi[2])*.5f)*dir;d->v[j].x=t->anchor_x+x*scale+z*scale*.18f;d->v[j].y=t->anchor_y-y*scale-z*scale*.08f;d->v[j].z=z;d->v[j].u=q->v[j].uv[0];d->v[j].v=q->v[j].uv[1];d->depth+=z;}
        d->depth/=3.0f;
    }
    /* Painter's order: positive camera-space z is farther away.  Insertion
     * sort is deliberately stable, retaining blob order for equal-depth faces. */
    for(i=1;i<p->triangles_n;i++){DrawTriangle key=draws[i];uint32_t j=i;while(j>0&&draws[j-1].depth<key.depth){draws[j]=draws[j-1];j--;}draws[j]=key;}
    for(i=0;i<p->triangles_n;i++)tri(t,draws[i].texture,draws[i].v[0],draws[i].v[1],draws[i].v[2]);
    free(draws); effect(p,pose,t);return 1;
}
