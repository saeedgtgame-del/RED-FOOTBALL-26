#include "nexon_engine.h"
#include <string.h>
#include <math.h>

static float nx_clamp_dt(float dt) {
    if (dt < 0.0f) return 0.0f;
    if (dt > 0.05f) return 0.05f;
    return dt;
}

void nx_init(NX_Scene *scene) {
    int i;
    memset(scene, 0, sizeof(*scene));
    scene->player = NX_ENTITY_NONE;
    for (i=0;i<NX_MAX_ENTITIES;i++) scene->entities[i].active=0;
}

int nx_spawn(NX_Scene *scene, NX_EntityType type, NX_Vec3 p) {
    int i;
    for (i=0;i<NX_MAX_ENTITIES;i++) {
        NX_Entity *e=&scene->entities[i];
        if (!e->active) {
            memset(e,0,sizeof(*e));
            e->active=1; e->type=type;
            e->transform.position=p;
            e->transform.scale.x=1.0f;
            e->transform.scale.y=1.0f;
            e->transform.scale.z=1.0f;
            e->radius=1.0f;
            scene->count++;
            return i;
        }
    }
    return NX_ENTITY_NONE;
}

void nx_remove(NX_Scene *scene, int id) {
    if (id<0 || id>=NX_MAX_ENTITIES) return;
    if (scene->entities[id].active) {
        scene->entities[id].active=0;
        if (scene->count>0) scene->count--;
        if (scene->player==id) scene->player=NX_ENTITY_NONE;
    }
}

void nx_set_player(NX_Scene *scene, int id) {
    if (id>=0 && id<NX_MAX_ENTITIES && scene->entities[id].active)
        scene->player=id;
}

void nx_update_player(NX_Scene *scene, const NX_Input *in, float dt) {
    NX_Entity *p;
    float speed,dx,dz,len;
    dt=nx_clamp_dt(dt);
    if (!in || scene->player<0 || scene->player>=NX_MAX_ENTITIES) return;
    p=&scene->entities[scene->player];
    if (!p->active) return;
    speed=(in->held & 1u) ? 32.0f : 20.0f;
    dx=in->moveX; dz=in->moveZ;
    len=dx*dx+dz*dz;
    if (len>1.0f) {
        float inv=1.0f/sqrtf(len);
        dx*=inv; dz*=inv;
    }
    p->transform.rotation.y += in->turn*2.2f*dt;
    /* Movement follows the character's facing direction. */
    {
        float sn=sinf(p->transform.rotation.y), cs=cosf(p->transform.rotation.y);
        float forward=-dz, right=dx;
        float targetX=(right*cs+forward*sn)*speed;
        float targetZ=(right*sn-forward*cs)*speed;
        float response=(len>0.001f)?12.0f:18.0f;
        float blend=dt*response;
        if(blend>1.0f) blend=1.0f;
        /* Acceleration and friction smooth movement instead of instant velocity changes. */
        p->velocity.x+=(targetX-p->velocity.x)*blend;
        p->velocity.z+=(targetZ-p->velocity.z)*blend;
    }
    p->transform.position.x += p->velocity.x*dt;
    p->transform.position.z += p->velocity.z*dt;
}

void nx_camera_init(NX_Camera *c, float distance, float height) {
    memset(c,0,sizeof(*c));
    c->followDistance=distance;
    c->height=height;
    c->smoothing=7.0f;
}

void nx_camera_follow(NX_Camera *c, const NX_Entity *target, float dt) {
    float a;
    NX_Vec3 desired;
    if (!c || !target || !target->active) return;
    dt=nx_clamp_dt(dt);
    a=c->smoothing*dt;
    if (a>1.0f) a=1.0f;
    desired.x=target->transform.position.x;
    desired.y=target->transform.position.y+c->height;
    desired.z=target->transform.position.z+c->followDistance;
    c->position.x+=(desired.x-c->position.x)*a;
    c->position.y+=(desired.y-c->position.y)*a;
    c->position.z+=(desired.z-c->position.z)*a;
    c->target=target->transform.position;
}

void nx_time_step(NX_Time *t, float dt) {
    if (!t) return;
    t->deltaTime=nx_clamp_dt(dt);
    t->elapsed+=t->deltaTime;
    t->frame++;
}
