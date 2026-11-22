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
    nx_physics_integrate(p,nx_vec3(0.0f,0.0f,0.0f),0.0f,dt);
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


/* ---------- NEXON reusable math and physics ---------- */
NX_Vec3 nx_vec3(float x, float y, float z) {
    NX_Vec3 v; v.x=x; v.y=y; v.z=z; return v;
}
NX_Vec3 nx_vec3_add(NX_Vec3 a, NX_Vec3 b) {
    return nx_vec3(a.x+b.x,a.y+b.y,a.z+b.z);
}
NX_Vec3 nx_vec3_sub(NX_Vec3 a, NX_Vec3 b) {
    return nx_vec3(a.x-b.x,a.y-b.y,a.z-b.z);
}
NX_Vec3 nx_vec3_scale(NX_Vec3 v, float s) {
    return nx_vec3(v.x*s,v.y*s,v.z*s);
}
float nx_vec3_dot(NX_Vec3 a, NX_Vec3 b) {
    return a.x*b.x+a.y*b.y+a.z*b.z;
}
float nx_vec3_length(NX_Vec3 v) {
    return sqrtf(nx_vec3_dot(v,v));
}

/* Semi-implicit Euler integration: compact and suitable for PS2. */
void nx_physics_integrate(NX_Entity *e, NX_Vec3 acceleration,
                          float gravity, float dt) {
    if (!e || !e->active) return;
    dt=nx_clamp_dt(dt);
    e->velocity.x += acceleration.x*dt;
    e->velocity.y += (acceleration.y-gravity)*dt;
    e->velocity.z += acceleration.z*dt;
    e->transform.position.x += e->velocity.x*dt;
    e->transform.position.y += e->velocity.y*dt;
    e->transform.position.z += e->velocity.z*dt;
}

/* Broadly reusable sphere test; entity radii are world-space units. */
int nx_collision_sphere(NX_Entity *a, NX_Entity *b) {
    NX_Vec3 d;
    float r;
    if (!a || !b || !a->active || !b->active) return 0;
    d=nx_vec3_sub(a->transform.position,b->transform.position);
    r=a->radius+b->radius;
    return nx_vec3_dot(d,d) <= r*r;
}

/* Positional correction plus a simple normal impulse. */
void nx_collision_resolve_spheres(NX_Entity *a, NX_Entity *b, float restitution) {
    NX_Vec3 d;
    float dist, sum, penetration, relative, impulse;
    if (!nx_collision_sphere(a,b)) return;
    d=nx_vec3_sub(a->transform.position,b->transform.position);
    dist=nx_vec3_length(d);
    sum=a->radius+b->radius;
    if (dist < 0.0001f) { d=nx_vec3(1.0f,0.0f,0.0f); dist=1.0f; }
    d=nx_vec3_scale(d,1.0f/dist);
    penetration=sum-dist;
    a->transform.position=nx_vec3_add(a->transform.position,nx_vec3_scale(d,penetration*0.5f));
    b->transform.position=nx_vec3_sub(b->transform.position,nx_vec3_scale(d,penetration*0.5f));
    relative=(a->velocity.x-b->velocity.x)*d.x+
             (a->velocity.y-b->velocity.y)*d.y+
             (a->velocity.z-b->velocity.z)*d.z;
    if (relative < 0.0f) {
        if (restitution<0.0f) restitution=0.0f;
        if (restitution>1.0f) restitution=1.0f;
        impulse=-(1.0f+restitution)*relative*0.5f;
        a->velocity.x+=d.x*impulse; a->velocity.y+=d.y*impulse; a->velocity.z+=d.z*impulse;
        b->velocity.x-=d.x*impulse; b->velocity.y-=d.y*impulse; b->velocity.z-=d.z*impulse;
    }
}

void nx_time_step(NX_Time *t, float dt) {
    if (!t) return;
    t->deltaTime=nx_clamp_dt(dt);
    t->elapsed+=t->deltaTime;
    t->frame++;
}
