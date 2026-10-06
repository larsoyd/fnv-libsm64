#include "decomp/game/interaction.h"
#include "decomp/game/mario.h"
#include "decomp/engine/math_util.h"
#include "decomp/include/sm64.h"
#include "decomp/shim.h"

// the interaction bits a blow carries, kept file local by the library
#define INT_PUNCH (1 << 1)
#define INT_KICK (1 << 2)
#define INT_TRIP (1 << 3)
#define INT_FAST_ATTACK_OR_SHELL (1 << 5)

// a wall mario punched throws him back, an actor's box is a body he hit and does not
void check_kick_or_punch_wall(struct MarioState *m) {
    if (!(m->flags & (MARIO_PUNCHING | MARIO_KICKING | MARIO_TRIPPING))) return;
    Vec3f detector;
    detector[0] = m->pos[0] + 50.0f * sins(m->faceAngle[1]);
    detector[2] = m->pos[2] + 50.0f * coss(m->faceAngle[1]);
    detector[1] = m->pos[1];
    struct SM64SurfaceCollisionData *wall = resolve_and_return_wall_collisions(detector, 80.0f, 5.0f);
    if (wall == NULL || wall->transform != NULL) return;
    if (m->action != ACT_MOVE_PUNCHING || m->forwardVel >= 0.0f) {
        if (m->action == ACT_PUNCHING) m->action = ACT_MOVE_PUNCHING;
        mario_set_forward_vel(m, -48.0f);
        play_sound(SOUND_ACTION_HIT_2, m->marioObj->header.gfx.cameraToObject);
        m->particleFlags |= PARTICLE_TRIANGLE;
    } else if (m->action & ACT_FLAG_AIR) {
        mario_set_forward_vel(m, -16.0f);
        play_sound(SOUND_ACTION_HIT_2, m->marioObj->header.gfx.cameraToObject);
        m->particleFlags |= PARTICLE_TRIANGLE;
    }
}

// a blow that lands on a body is heard but does not throw mario back
void fake_bounce_back_from_attack(struct MarioState *m, u32 interaction) {
    if (interaction & (INT_PUNCH | INT_KICK | INT_TRIP | INT_FAST_ATTACK_OR_SHELL))
        play_sound(SOUND_ACTION_HIT_2, m->marioObj->header.gfx.cameraToObject);
}
