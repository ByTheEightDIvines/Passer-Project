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
    Flee,       // last heart, running away: the injured limp-run (slower than a run)
    HurtWait,   // last heart, got away: catch breath, weapon still out
    HurtWalk,   // last heart, walking on: weapon away, limp
};

// This traveller has its own fight animations (the men fighters; their PsrPack anims are loaded).
bool has(const daNpcPasser_c* passer);
// Once per frame, after the traveller's own action code. swing: start a swing if ready.
// Returns how many swing hits land this frame (the strike should be made that many times: 0/1).
int update(daNpcPasser_c* passer, Want want, bool swing, bool turning = false);
// Weapon straight back on the belt and the game's own animation back on (bitten by the wolf: it
// runs like an unarmed traveller).
void drop(daNpcPasser_c* passer);
// Startled, drawing, sheathing or mid-swing: should hold still.
bool busy(const daNpcPasser_c* passer);
// The weapon is out of its sheath.
bool weaponOut(const daNpcPasser_c* passer);
// One of its own animations that should play at its own speed (not the walk's) is on.
bool ownSpeed(const daNpcPasser_c* passer);
// The right hand's grip frame (hand joint x fitted grip) for this body, whatever it is doing; false
// if the body has no fitted grip. Used for the cook's spoon.
bool heldMtx(const daNpcPasser_c* passer, Mtx out);
// Where the weapon model goes while in hand (false: it's in its sheath).
bool weaponMtx(const daNpcPasser_c* passer, Mtx out);

// The game adds each body's own posture tweaks (the old men's hunch, bent knees, turned arms...)
// on top of every animation. Ours are finished poses, so while one plays that tweak is faded out
// (and faded back in after). Returns false if the hook couldn't be installed.
bool install();

// Hit (any traveller): the Bokoblin's stagger, from the front or either side depending on where the
// blow came from, just under a second; it stops where it stands meanwhile, then carries on.
void hit(daNpcPasser_c* passer, const cXyz& from);
// The fall (the last blow): the Bokoblin's fly-back pose while in the air (landed=false), then its
// landing sprawl (landed=true), held on the last frame. False if the body has no such animation.
bool fall(daNpcPasser_c* passer, bool landed);
// Once a frame, after the traveller's own code: keeps a hit or the fall playing. True while one is on.
bool forcedTick(daNpcPasser_c* passer);
// The hit or fall animation has played to its end (or there is none).
bool forcedDone(const daNpcPasser_c* passer);
// Mid hit reaction.
bool hitting(const daNpcPasser_c* passer);

}  // namespace fighter_anim
