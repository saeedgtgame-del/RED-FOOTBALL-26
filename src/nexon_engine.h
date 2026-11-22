#ifndef NEXON_ENGINE_H
#define NEXON_ENGINE_H

/* NEXON ENGINE - compact PS2 game framework foundation.
 * Coordinate system: X=left/right, Y=up/down, Z=forward/back.
 * Fixed-size pools avoid heap fragmentation on PlayStation 2.
 */
#define NX_MAX_ENTITIES 96
#define NX_ENTITY_NONE (-1)

typedef struct { float x,y,z; } NX_Vec3;
typedef struct { NX_Vec3 position, rotation, scale; } NX_Transform;

typedef enum {
    NX_ENTITY_NONE_TYPE=0,
    NX_ENTITY_PLAYER,
    NX_ENTITY_NPC,
    NX_ENTITY_VEHICLE,
    NX_ENTITY_BUILDING,
    NX_ENTITY_PROP
} NX_EntityType;

typedef struct {
    int active;
    NX_EntityType type;
    NX_Transform transform;
    NX_Vec3 velocity;
    float radius;
    int flags;
} NX_Entity;

typedef struct {
    NX_Entity entities[NX_MAX_ENTITIES];
    int count;
    int player;
} NX_Scene;

typedef struct {
    NX_Vec3 position;
    NX_Vec3 target;
    float followDistance;
    float height;
    float smoothing;
} NX_Camera;

typedef struct {
    float moveX, moveZ;
    float turn;
    unsigned int pressed;
    unsigned int held;
} NX_Input;

typedef struct {
    float deltaTime;
    float elapsed;
    unsigned int frame;
} NX_Time;

void nx_init(NX_Scene *scene);
int nx_spawn(NX_Scene *scene, NX_EntityType type, NX_Vec3 position);
void nx_remove(NX_Scene *scene, int id);
void nx_set_player(NX_Scene *scene, int id);
void nx_update_player(NX_Scene *scene, const NX_Input *input, float dt);
void nx_camera_init(NX_Camera *camera, float distance, float height);
void nx_camera_follow(NX_Camera *camera, const NX_Entity *target, float dt);
void nx_time_step(NX_Time *time, float dt);

/* Reusable math and physics layer for every game built on NEXON. */
NX_Vec3 nx_vec3(float x, float y, float z);
NX_Vec3 nx_vec3_add(NX_Vec3 a, NX_Vec3 b);
NX_Vec3 nx_vec3_sub(NX_Vec3 a, NX_Vec3 b);
NX_Vec3 nx_vec3_scale(NX_Vec3 v, float s);
float nx_vec3_dot(NX_Vec3 a, NX_Vec3 b);
float nx_vec3_length(NX_Vec3 v);
void nx_physics_integrate(NX_Entity *entity, NX_Vec3 acceleration,
                          float gravity, float dt);
int nx_collision_sphere(NX_Entity *a, NX_Entity *b);
void nx_collision_resolve_spheres(NX_Entity *a, NX_Entity *b, float restitution);

#endif
