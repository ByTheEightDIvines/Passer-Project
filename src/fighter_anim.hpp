#pragma once
// Fighters' own animations (see fighter_anim.cpp): startled, hip draw, squaring up, swinging,
// running hurt with the weapon out, limping, sheathing.
#include "global.h"

class daNpcPasser_c;

namespace fighter_anim {

// What the traveller's situation asks of the weapon this frame.
enum class Want : unsigned char {
    Calm,       // nothing to fight: put the weapon away (or leave it away)
    Keep,       // don't touch anything this frame (vanilla is playing its own startle)
    Ready,      // an enemy close by: draw and square up, swing when asked
    Chase,      // going after an enemy: run with the weapon out
    Flee,       // last heart, running away: run with the weapon out
    HurtWait,   // last heart, got away: catch breath, weapon still out
    HurtWalk,   // last heart, walking on: weapon away, limp
};

// This traveller has its own fight animations (the men fighters; their PsrPack anims are loaded).
bool has(const daNpcPasser_c* passer);
// Once per frame, after the traveller's own action code. swing: start a swing if ready.
// Returns how many swing hits land this frame (the strike should be made that many times: 0/1).
int update(daNpcPasser_c* passer, Want want, bool swing);
// Startled, drawing, sheathing or mid-swing: should hold still.
bool busy(const daNpcPasser_c* passer);
// The weapon is out of its sheath.
bool weaponOut(const daNpcPasser_c* passer);
// One of its own animations that should play at its own speed (not the walk's) is on.
bool ownSpeed(const daNpcPasser_c* passer);
// Where the weapon model goes while in hand (false: it's in its sheath).
bool weaponMtx(const daNpcPasser_c* passer, Mtx out);

}  // namespace fighter_anim
