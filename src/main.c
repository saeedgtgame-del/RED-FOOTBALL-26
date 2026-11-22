#include <kernel.h>
#include <stdlib.h>
#include <malloc.h>
#include <math.h>
#include <string.h>
#include <tamtypes.h>
#include <math3d.h>
#include <packet.h>
#include <dma_tags.h>
#include <gif_tags.h>
#include <gs_psm.h>
#include <dma.h>
#include <graph.h>
#include <draw.h>
#include <draw3d.h>
#include <libpad.h>
#include "mesh_data.c"
#include "nexon_engine.h"

#define SW 640
#define SH 512
#define TEAM_N 11
#define FIELD_X 52.0f
#define FIELD_Z 82.0f
#define BALL_R 1.15f
#define PI 3.14159265f

static framebuffer_t frame;
static zbuffer_t zbuf;
static unsigned char padBuf[256] __attribute__((aligned(64)));
static struct padButtonStatus pad;
static int padOK=0, started=0, paused=0, lastStart=0, lastPass=0, lastShot=0, lastSwitch=0;
static int scoreA=0, scoreB=0, selected=9, matchSeconds=300, gameOver=0;
static float matchClock=0.0f;
static NX_Scene scene;
static NX_Time clockNX;
static int playerIds[2][TEAM_N], ballId;
static float ballX=0, ballZ=0, ballVX=0, ballVZ=0, ballY=1.2f, ballVY=0;
static float camX=0,camY=112,camZ=166;
static VECTOR cameraPos={0,112,166,1}, cameraRot={0.58f,0,0,1};
static const float homeX[TEAM_N]={0,-34,-12,12,34,-25,-8,8,25,-12,12};
static const float homeZA[TEAM_N]={-73,-48,-48,-48,-48,-20,-20,-20,-20,7,7};
static const float homeZB[TEAM_N]={73,48,48,48,48,20,20,20,20,-7,-7};

static void initGS(void){
 frame.width=SW;frame.height=SH;frame.mask=0;frame.psm=GS_PSM_32;
 frame.address=graph_vram_allocate(SW,SH,GS_PSM_32,GRAPH_ALIGN_PAGE);
 zbuf.enable=DRAW_ENABLE;zbuf.mask=0;zbuf.method=ZTEST_METHOD_GREATER_EQUAL;zbuf.zsm=GS_ZBUF_32;
 zbuf.address=graph_vram_allocate(SW,SH,GS_ZBUF_32,GRAPH_ALIGN_PAGE);
 graph_initialize(frame.address,SW,SH,GS_PSM_32,0,0);
 packet_t *p=packet_init(32,PACKET_NORMAL);qword_t *q=p->data;
 q=draw_setup_environment(q,0,&frame,&zbuf);q=draw_primitive_xyoffset(q,0,2048-SW/2,2048-SH/2);
 q=draw_finish(q);dma_channel_send_normal(DMA_CHANNEL_GIF,p->data,q-p->data,0,0);dma_wait_fast();packet_free(p);
}
static qword_t *rect(qword_t *q,int x,int y,int w,int h,int r,int g,int b){
 rect_t a;a.v0.x=x;a.v0.y=y;a.v0.z=1;a.v1.x=x+w;a.v1.y=y+h;a.v1.z=1;
 a.color.r=r;a.color.g=g;a.color.b=b;a.color.a=128;a.color.q=1.0f;return draw_rect_filled(q,0,&a);
}
static const unsigned char font5x7[39][7]={
{14,17,19,21,25,17,14},{4,12,4,4,4,4,14},{14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},{31,16,16,30,1,1,30},{6,8,16,30,17,17,14},{31,1,2,4,8,8,8},{14,17,17,14,17,17,14},{14,17,17,15,1,2,12},
{14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},{30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},{14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},{7,2,2,2,18,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},{17,27,21,21,17,17,17},{17,25,21,19,17,17,17},{14,17,17,17,17,17,14},{30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},{15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},{17,17,17,17,17,10,4},{17,17,17,21,21,21,10},{17,17,10,4,10,17,17},{17,17,10,4,4,4,4},{31,1,2,4,8,16,31},{0,0,0,31,0,0,0},{1,2,2,4,8,8,16},{0,0,0,0,0,0,0}};
static int glyph(char c){if(c>='0'&&c<='9')return c-'0';if(c>='A'&&c<='Z')return 10+c-'A';if(c=='-')return 36;if(c=='/')return 37;return 38;}
static qword_t *text(qword_t *q,int x,int y,int s,const char *v,int r,int g,int b){int i,a,k;for(i=0;v[i];i++){int n=glyph(v[i]);for(a=0;a<7;a++)for(k=0;k<5;k++)if(font5x7[n][a]&(1<<(4-k)))q=rect(q,x+i*6*s+k*s,y+a*s,s,s,r,g,b);}return q;}

static qword_t *cube(qword_t *q,MATRIX vs,VECTOR pos,VECTOR rot,float sx,float sy,float sz,int cr,int cg,int cb,VECTOR *tmp,xyz_t *xyz,color_t *rgbaq,prim_t *prim,color_t *base){
 MATRIX lw,wv,ls;VECTOR scaled[24];color_f_t cf[24];int i;
 for(i=0;i<vertex_count;i++){scaled[i][0]=vertices[i][0]*sx*.1f;scaled[i][1]=vertices[i][1]*sy*.1f;scaled[i][2]=vertices[i][2]*sz*.1f;scaled[i][3]=vertices[i][3];cf[i].r=cr/255.0f;cf[i].g=cg/255.0f;cf[i].b=cb/255.0f;cf[i].a=1.0f;}
 create_local_world(lw,pos,rot);create_world_view(wv,cameraPos,cameraRot);create_local_screen(ls,lw,wv,vs);
 calculate_vertices(tmp,vertex_count,scaled,ls);draw_convert_xyz(xyz,2048,2048,32,vertex_count,(vertex_f_t*)tmp);draw_convert_rgbq(rgbaq,vertex_count,(vertex_f_t*)tmp,cf,0x80);
 q=draw_prim_start(q,0,prim,base);for(i=0;i<points_count;i++){q->dw[0]=rgbaq[points[i]].rgbaq;q->dw[1]=xyz[points[i]].xyz;q++;}return draw_prim_end(q,2,DRAW_RGBAQ_REGLIST);
}
static float clampf(float v,float lo,float hi){return v<lo?lo:(v>hi?hi:v);}
static float dist2(float x,float z,float a,float b){float dx=x-a,dz=z-b;return dx*dx+dz*dz;}
static void resetPositions(void){
 int t,i;for(t=0;t<2;t++)for(i=0;i<TEAM_N;i++){NX_Entity *e=&scene.entities[playerIds[t][i]];e->transform.position.x=homeX[i];e->transform.position.z=t?homeZB[i]:homeZA[i];e->velocity.x=0;e->velocity.z=0;}
 ballX=0;ballZ=0;ballY=1.2f;ballVX=ballVZ=ballVY=0;selected=9;
 {NX_Entity *b=&scene.entities[ballId];b->transform.position.x=0;b->transform.position.y=ballY;b->transform.position.z=0;b->velocity=nx_vec3(0,0,0);}
}
static void initGame(void){
 int t,i;NX_Vec3 p;nx_init(&scene);memset(&clockNX,0,sizeof(clockNX));
 for(t=0;t<2;t++)for(i=0;i<TEAM_N;i++){p.x=homeX[i];p.y=0;p.z=t?homeZB[i]:homeZA[i];playerIds[t][i]=nx_spawn(&scene,t?NX_ENTITY_NPC:NX_ENTITY_PLAYER,p);scene.entities[playerIds[t][i]].radius=2.0f;}
 nx_set_player(&scene,playerIds[0][9]);p.x=0;p.y=1;p.z=0;ballId=nx_spawn(&scene,NX_ENTITY_PROP,p);scene.entities[ballId].radius=1.15f;resetPositions();
}
static void initPad(void){if(padInit(0)==1&&padPortOpen(0,0,padBuf)){padSetMainMode(0,0,PAD_MMODE_DUALSHOCK,PAD_MMODE_LOCK);padOK=1;}}
static void updatePad(void){
 NX_Input in;NX_Entity *p;float ax=0,ay=0;int start,pass,shot,sw;
 memset(&in,0,sizeof(in));if(padOK&&padGetState(0,0)==PAD_STATE_STABLE&&padRead(0,0,&pad)){
  ax=((float)pad.ljoy_h-128.0f)/127.0f;ay=((float)pad.ljoy_v-128.0f)/127.0f;
  if(fabsf(ax)<.16f)ax=0;if(fabsf(ay)<.16f)ay=0;
  if(!(pad.btns&PAD_LEFT))ax=-1;if(!(pad.btns&PAD_RIGHT))ax=1;if(!(pad.btns&PAD_UP))ay=-1;if(!(pad.btns&PAD_DOWN))ay=1;
  in.moveX=ax;in.moveZ=-ay;if(!(pad.btns&PAD_CROSS))in.held=1;
  start=!(pad.btns&PAD_START);pass=!(pad.btns&PAD_SQUARE);shot=!(pad.btns&PAD_CIRCLE);sw=!(pad.btns&PAD_TRIANGLE);
  if(start&&!lastStart){if(gameOver){scoreA=0;scoreB=0;matchClock=0;gameOver=0;resetPositions();started=1;paused=0;}else if(!started){started=1;paused=0;}else paused=!paused;}lastStart=start;
  if(pass&&!lastPass&&started&&!paused){NX_Entity *e=&scene.entities[playerIds[0][selected]];ballVX=(e->transform.position.x-ballX)*.22f;ballVZ=(e->transform.position.z-ballZ)*.22f;ballVX+=0;ballVZ+=(e->transform.position.z<0? -16.0f:16.0f); }lastPass=pass;
  if(shot&&!lastShot&&started&&!paused){NX_Entity *e=&scene.entities[playerIds[0][selected]];float dx=ballX-e->transform.position.x,dz=ballZ-e->transform.position.z;float len=sqrtf(dx*dx+dz*dz);if(len<8){ballVX=(dx/(len+0.01f))*7.0f;ballVZ=24.0f;ballVY=4.5f;}}lastShot=shot;
  if(sw&&!lastSwitch&&started&&!paused){int i,best=0;float bd=1e9f;for(i=1;i<TEAM_N;i++){NX_Entity *e=&scene.entities[playerIds[0][i]];float d=dist2(e->transform.position.x,e->transform.position.z,ballX,ballZ);if(d<bd){bd=d;best=i;}}selected=best;nx_set_player(&scene,playerIds[0][selected]);}lastSwitch=sw;
 }else{padOK=0;start=pass=shot=sw=0;}
 if(!started||paused||gameOver)return;
 p=&scene.entities[playerIds[0][selected]];
 in.turn=0;nx_update_player(&scene,&in,1.0f/60.0f);
 p->transform.position.x=clampf(p->transform.position.x,-FIELD_X+3,FIELD_X-3);
 p->transform.position.z=clampf(p->transform.position.z,-FIELD_Z+3,FIELD_Z-3);
}
static void updateAI(float dt){
 int t,i,nearest;float best,d,dx,dz,len,tx,tz,blend;
 for(t=0;t<2;t++){
  nearest=0;best=1e9f;
  for(i=1;i<TEAM_N;i++){NX_Entity *e=&scene.entities[playerIds[t][i]];d=dist2(e->transform.position.x,e->transform.position.z,ballX,ballZ);if(d<best){best=d;nearest=i;}}
  for(i=0;i<TEAM_N;i++){
   NX_Entity *e=&scene.entities[playerIds[t][i]];
   if(t==0&&i==selected)continue;
   tx=homeX[i]+clampf(ballX*.22f,-10,10);tz=(t?homeZB[i]:homeZA[i])+clampf(ballZ*.20f,-15,15);
   if(i==nearest|| (t==1&&best<2200)){tx=ballX;tz=ballZ;}
   dx=tx-e->transform.position.x;dz=tz-e->transform.position.z;len=sqrtf(dx*dx+dz*dz);
   if(len>1.0f){dx/=len;dz/=len;}else{dx=dz=0;}
   blend=clampf(dt*5.0f,0,1);e->velocity.x+=(dx*17.0f-e->velocity.x)*blend;e->velocity.z+=(dz*17.0f-e->velocity.z)*blend;
   e->transform.position.x+=e->velocity.x*dt;e->transform.position.z+=e->velocity.z*dt;
   e->transform.position.x=clampf(e->transform.position.x,-FIELD_X+2,FIELD_X-2);e->transform.position.z=clampf(e->transform.position.z,-FIELD_Z+2,FIELD_Z-2);
  }
 }
}
static void updatePlayerCollisions(void){
 int t,i,j;
 /* NEXON sphere contacts keep teammates from occupying the same space. */
 for(t=0;t<2;t++)for(i=0;i<TEAM_N;i++)for(j=i+1;j<TEAM_N;j++)
  nx_collision_resolve_spheres(&scene.entities[playerIds[t][i]],&scene.entities[playerIds[t][j]],0.12f);
}
static void updateBall(float dt){
 int t,i;float dx,dz,d,len;NX_Entity *e,*b=&scene.entities[ballId];
 /* The upgraded NEXON integrator owns the ball's world-space motion and gravity. */
 b->transform.position.x=ballX;b->transform.position.y=ballY;b->transform.position.z=ballZ;
 b->velocity=nx_vec3(ballVX,ballVY,ballVZ);
 nx_physics_integrate(b,nx_vec3(0,0,0),18.0f,dt);
 ballX=b->transform.position.x;ballY=b->transform.position.y;ballZ=b->transform.position.z;
 for(t=0;t<2;t++)for(i=0;i<TEAM_N;i++){
  e=&scene.entities[playerIds[t][i]];
  if(nx_collision_sphere(b,e)){
   dx=ballX-e->transform.position.x;dz=ballZ-e->transform.position.z;
   d=dx*dx+dz*dz;len=sqrtf(d)+.01f;
   ballVX+=(dx/len)*(t==0&&i==selected?7.0f:4.0f);
   ballVZ+=(dz/len)*(t==0&&i==selected?7.0f:4.0f);
   if(t==0&&i==selected){ballVX+=e->velocity.x*.4f;ballVZ+=e->velocity.z*.4f;}
  }
 }
 if(ballY<BALL_R){ballY=BALL_R;if(ballVY<0)ballVY=-ballVY*.48f;}
 ballVX*=.986f;ballVZ*=.986f;ballVY*=.96f;
 if(ballX< -FIELD_X+1||ballX>FIELD_X-1){ballX=clampf(ballX,-FIELD_X+1,FIELD_X-1);ballVX=-ballVX*.72f;}
 if(ballZ< -FIELD_Z-3||ballZ>FIELD_Z+3){
  if(fabsf(ballX)<9.0f){if(ballZ<0)scoreB++;else scoreA++;resetPositions();}
  else{ballZ=clampf(ballZ,-FIELD_Z,FIELD_Z);ballVZ=-ballVZ*.7f;}
 }
 b->transform.position.x=ballX;b->transform.position.y=ballY;b->transform.position.z=ballZ;
 b->velocity=nx_vec3(ballVX,ballVY,ballVZ);
}
static void updateGame(void){
 float dt=1.0f/60.0f;if(!started||paused||gameOver)return;
 nx_time_step(&clockNX,dt);matchClock+=dt;
 updateAI(dt);updatePlayerCollisions();updateBall(dt);
 if(matchClock>=matchSeconds){gameOver=1;started=0;}
}
static void render(void){
 static packet_t *packets[2]={0,0};static int ctx=0;static VECTOR *tmp=0;static xyz_t *xyz=0;static color_t *rgbaq=0;static prim_t prim;static color_t base;static MATRIX vs;
 qword_t *q,*tag;int i,t;VECTOR p,r;char sa[2],sb[2];
 if(!packets[0]){packets[0]=packet_init(32768,PACKET_NORMAL);packets[1]=packet_init(32768,PACKET_NORMAL);tmp=memalign(128,sizeof(VECTOR)*vertex_count);xyz=memalign(128,sizeof(xyz_t)*vertex_count);rgbaq=memalign(128,sizeof(color_t)*vertex_count);prim.type=PRIM_TRIANGLE;prim.shading=PRIM_SHADE_GOURAUD;prim.mapping=DRAW_DISABLE;prim.fogging=DRAW_DISABLE;prim.blending=DRAW_DISABLE;prim.antialiasing=DRAW_ENABLE;prim.mapping_type=PRIM_MAP_ST;prim.colorfix=PRIM_UNFIXED;base.r=128;base.g=128;base.b=128;base.a=128;base.q=1.0f;create_view_screen(vs,graph_aspect_ratio(),-3,3,-2.4f,2.4f,1,1200);}
 cameraPos[0]=camX;cameraPos[1]=camY;cameraPos[2]=camZ;cameraRot[0]=0.58f;cameraRot[1]=0;cameraRot[2]=0;
 tag=packets[ctx]->data;q=tag+1;q=draw_disable_tests(q,0,&zbuf);q=draw_clear(q,0,2048-SW/2,2048-SH/2,SW,SH,20,75,45);q=draw_enable_tests(q,0,&zbuf);
 p[0]=0;p[1]=-1.4f;p[2]=0;p[3]=1;r[0]=r[1]=r[2]=r[3]=0;q=cube(q,vs,p,r,FIELD_X*2,2,FIELD_Z*2,34,125,65,tmp,xyz,rgbaq,&prim,&base);
 /* White touchlines, halfway line, centre circle and penalty areas. */
 for(i=0;i<2;i++){float z=i?FIELD_Z:-FIELD_Z;p[0]=0;p[1]=-.2f;p[2]=z;p[3]=1;q=cube(q,vs,p,r,FIELD_X*2,.25f,.35f,235,235,225,tmp,xyz,rgbaq,&prim,&base);p[0]=i?FIELD_X:-FIELD_X;p[2]=0;q=cube(q,vs,p,r,.35f,.25f,FIELD_Z*2,235,235,225,tmp,xyz,rgbaq,&prim,&base);}
 p[0]=0;p[2]=0;q=cube(q,vs,p,r,FIELD_X*2,.25f,.35f,235,235,225,tmp,xyz,rgbaq,&prim,&base);
 for(i=0;i<32;i++){float a=(float)i*2*PI/32;p[0]=cosf(a)*12;p[2]=sinf(a)*12;q=cube(q,vs,p,r,.35f,.22f,.35f,235,235,225,tmp,xyz,rgbaq,&prim,&base);}
 for(t=0;t<2;t++){float z=t?FIELD_Z-16:-FIELD_Z+16;p[0]=0;p[2]=z;q=cube(q,vs,p,r,22,.2f,.3f,235,235,225,tmp,xyz,rgbaq,&prim,&base);p[0]=-22;q=cube(q,vs,p,r,.3f,.2f,16,235,235,225,tmp,xyz,rgbaq,&prim,&base);p[0]=22;q=cube(q,vs,p,r,.3f,.2f,16,235,235,225,tmp,xyz,rgbaq,&prim,&base);
  /* Goals and nets */
  p[0]=-9;p[2]=t?FIELD_Z+1:-FIELD_Z-1;p[1]=3;q=cube(q,vs,p,r,.5f,6,.5f,245,245,235,tmp,xyz,rgbaq,&prim,&base);p[0]=9;q=cube(q,vs,p,r,.5f,6,.5f,245,245,235,tmp,xyz,rgbaq,&prim,&base);p[0]=0;p[1]=6;q=cube(q,vs,p,r,9,.5f,.5f,245,245,235,tmp,xyz,rgbaq,&prim,&base);
 }
 /* Stadium stands and crowd bands around the pitch. */
 for(i=0;i<28;i++){float x=-52+i*3.85f;p[0]=x;p[1]=5;p[2]=-91;r[1]=0;q=cube(q,vs,p,r,3.2f,10,7,65,72,85,tmp,xyz,rgbaq,&prim,&base);p[2]=91;q=cube(q,vs,p,r,3.2f,10,7,65,72,85,tmp,xyz,rgbaq,&prim,&base);}
 for(i=0;i<22;i++){float z=-78+i*7.4f;p[0]=-58;p[1]=5;p[2]=z;q=cube(q,vs,p,r,7,10,3.2f,65,72,85,tmp,xyz,rgbaq,&prim,&base);p[0]=58;q=cube(q,vs,p,r,7,10,3.2f,65,72,85,tmp,xyz,rgbaq,&prim,&base);}
 /* Players: layered torso, head and legs, with team colors. */
 for(t=0;t<2;t++)for(i=0;i<TEAM_N;i++){NX_Entity *e=&scene.entities[playerIds[t][i]];float x=e->transform.position.x,z=e->transform.position.z;int red=t?35:205,green=t?85:35,blue=t?210:48;
  if(i==0){red=t?20:165;green=t?35:35;blue=t?95:35;}
  p[0]=x;p[1]=3.5f;p[2]=z;p[3]=1;q=cube(q,vs,p,r,2.7f,5.5f,2.0f,red,green,blue,tmp,xyz,rgbaq,&prim,&base);
  p[1]=7.5f;q=cube(q,vs,p,r,1.8f,1.8f,1.8f,190,150,118,tmp,xyz,rgbaq,&prim,&base);
  p[1]=.7f;p[0]=x-.8f;q=cube(q,vs,p,r,.75f,2.3f,.8f,25,30,38,tmp,xyz,rgbaq,&prim,&base);p[0]=x+.8f;q=cube(q,vs,p,r,.75f,2.3f,.8f,25,30,38,tmp,xyz,rgbaq,&prim,&base);
  if(t==0&&i==selected){p[0]=x;p[1]=10;p[2]=z;q=cube(q,vs,p,r,3.5f,.3f,3.5f,255,215,40,tmp,xyz,rgbaq,&prim,&base);}
 }
 /* Ball with a dark panel pattern approximation. */
 p[0]=ballX;p[1]=ballY;p[2]=ballZ;p[3]=1;q=cube(q,vs,p,r,2.2f,2.2f,2.2f,245,245,235,tmp,xyz,rgbaq,&prim,&base);
 q=draw_disable_tests(q,0,&zbuf);
 q=rect(q,0,0,SW,48,7,12,20);q=rect(q,0,SH-42,SW,42,7,12,20);
 q=text(q,18,14,2,"RED FOOTBALL 26",245,245,245);
 q=rect(q,270,8,100,31,30,35,45);q=text(q,283,17,2,"RED",235,70,70);q=text(q,330,17,2,"BLUE",90,160,245);
 sa[0]=(char)('0'+(scoreA%10));sa[1]=0;sb[0]=(char)('0'+(scoreB%10));sb[1]=0;q=text(q,385,15,3,sa,255,255,255);q=text(q,414,15,3,"-",255,220,80);q=text(q,440,15,3,sb,255,255,255);
 {int sec=(int)(matchSeconds-matchClock);if(sec<0)sec=0;char tm[8];tm[0]='0'+(sec/60)/10;tm[1]='0'+(sec/60)%10;tm[2]=':';tm[3]='0'+(sec%60)/10;tm[4]='0'+(sec%60)%10;tm[5]=0;q=text(q,520,17,2,tm,245,245,245);}
 q=text(q,16,SH-28,1,"LEFT STICK MOVE  SQUARE PASS  CIRCLE SHOOT  TRIANGLE SWITCH",205,215,225);
 if(!started&&!gameOver){q=rect(q,SW/2-175,SH/2-65,350,130,8,12,20);q=text(q,SW/2-128,SH/2-35,3,"RED FOOTBALL",245,245,245);q=text(q,SW/2-112,SH/2+7,2,"PRESS START TO PLAY",255,205,75);}
 if(paused){q=rect(q,SW/2-100,SH/2-35,200,70,8,12,20);q=text(q,SW/2-48,SH/2-8,3,"PAUSE",255,215,80);}
 if(gameOver){q=rect(q,SW/2-140,SH/2-45,280,90,8,12,20);q=text(q,SW/2-94,SH/2-18,2,"FULL TIME",255,220,80);}
 q=draw_finish(q);DMATAG_END(tag,(q-tag)-1,0,0,0);dma_wait_fast();dma_channel_send_chain(DMA_CHANNEL_GIF,tag,q-tag,0,0);ctx^=1;draw_wait_finish();graph_wait_vsync();
}
int main(void){
 dma_channel_initialize(DMA_CHANNEL_GIF,NULL,0);dma_channel_fast_waits(DMA_CHANNEL_GIF);initGS();initGame();initPad();
 for(;;){updatePad();updateGame();render();}
 return 0;
}
