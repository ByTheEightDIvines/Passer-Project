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
    Shoot,      // the bowman, enemy at range: bow in hand, draw and aim; loose when asked (swing)
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

// --- the bowman (passer 8): the Bulblin's draw / aim / shot, refitted to his body ---
// He has the bow animations (and is passer 8).
bool bowman(const daNpcPasser_c* passer);
// A guard takes a blow from in front on his shield (most of the time): the block plays and it does no harm.
bool block(daNpcPasser_c* passer, const cXyz& from);
// What the bow is doing for drawing it: in his left hand (else on his back), an arrow in his right
// hand, and whether the string is on his fingers (drawn back).
struct BowView {
    bool inHand = false;
    bool arrow = false;
    bool nocked = false;
};
BowView bowView(const daNpcPasser_c* passer);
// Drawn and held long enough to loose (update() with Want::Shoot and swing then shoots: it returns 1
// on the frame the arrow leaves the string).
bool aimed(const daNpcPasser_c* passer);

// The game adds each body's own posture tweaks (the old men's hunch, bent knees, turned arms...)
// on top of every animation. Ours are finished poses, so while one plays that tweak is faded out
// (and faded back in after). Returns false if the hook couldn't be installed.
bool install();

// Hit (any traveller): the Bokoblin's stagger, from the front or either side depending on where the
// blow came from, just under a second; it stops where it stands meanwhile, then carries on.
void hit(daNpcPasser_c* passer, const cXyz& from);
// Sitting down: a flinch where they sit, then back to sitting.
void sitFlinch(daNpcPasser_c* passer);
// A guard holds his shield up (the block pose) for now.
void raiseShield(daNpcPasser_c* passer);
// The fall (the last blow): the Bokoblin's fly-back pose while in the air (landed=false), then its
// landing sprawl (landed=true), held on the last frame. False if the body has no such animation.
bool fall(daNpcPasser_c* passer, bool landed);
// Once a frame, after the traveller's own code: keeps a hit or the fall playing. True while one is on.
bool forcedTick(daNpcPasser_c* passer);
// The hit or fall animation has played to its end (or there is none).
bool forcedDone(const daNpcPasser_c* passer);
// Mid hit reaction.
bool hitting(const daNpcPasser_c* passer);


// --- layering (any traveller) ---
// Once a frame, after everything else has picked its animation: on the move with an animation that
// isn't a walk or run (drawing, swinging, a pose left over from standing), the legs take the
// traveller's own walk or run instead, so it never slides along in a standing pose. carryLight: the
// left arm holds a torch or lantern up (whatever the rest of the body is doing).
void layerTick(daNpcPasser_c* passer, bool carryLight);
// Seated with food or drink: 1 eat (held by one end), 2 drink (bottle), 3 sip (goblet), 4 nibble (in the
// palm), 5 quaff (tankard); 0 stops.
// The body's own loop plays on the left arm while it sits.
void setMeal(daNpcPasser_c* passer, int kind);
// What it has in hand right now (0 nothing; the arm is at least partly up with it).
int meal(const daNpcPasser_c* passer);
// The torch pick-up (per body): start it; the frame it is on (-1: not playing); finished.
constexpr float kPickupGrab = 13.0f;   // the hand closes on the torch
bool startPickup(daNpcPasser_c* passer);
float pickupFrame(const daNpcPasser_c* passer);
bool pickupDone(const daNpcPasser_c* passer);
// The old women's freeze cast (per body): start it; its frame (-1: not playing); finished; and how
// far into the thrust it is (0..1, the staff tips forward with it).
constexpr float kCastFire = 16.0f;     // the spell leaves the lantern
bool startCast(daNpcPasser_c* passer);
float castFrame(const daNpcPasser_c* passer);
bool castDone(const daNpcPasser_c* passer);
float castReach(const daNpcPasser_c* passer);
// The left hand's matrix (for the torch or lantern), false if there is no model yet.
bool leftHandMtx(const daNpcPasser_c* passer, Mtx out);

}  // namespace fighter_anim
