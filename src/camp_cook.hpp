#pragma once
#include "mods/svc/actor.h"

#include "f_op/f_op_actor.h"
#include "d/d_cc_d.h"
#include "d/d_msg_flow.h"

class daNpcPasser_c;

// The camp cook: one big guy in a chef's hat, the only one there is. He never shows up unless Link
// scoops a camp's soup twice in a day: then he appears right behind Link and yells. Every further
// try he gets up off his mat by the fire and yells louder; on the fourth he pulls out his wooden
// spoon and goes after Link, and can't be hurt, until Link leaves the area or dies. Monsters scare
// him off.
//
// This actor is his brain: it talks (so the game turns Link to face him), decides and swings. His
// body is a townsfolk actor it spawns and steers, with all of the body's own behaviour off.
#define PSR_COOK_NAME "PsrCook"

namespace cook {
constexpr int kPathId = 0x3F;   // his body's path id (marks it as his)

// Link swung a bottle at a camp pot already scooped today (the fire calls this).
void onGreedy(int campNode, const cXyz& fire, s16 campYaw);
// Where a camp's mat lies (a roomy spot clear of the tents, fire and stools) and the way he faces
// sitting on it (towards the fire).
void matSpot(const cXyz& fire, s16 campYaw, cXyz* pos, s16* yaw);
// His body (a townsfolk actor): its own behaviour is off, and it wears his look.
bool isBody(const fopAc_ac_c* actor);
// The spoon: out (in his hand, swung with the fighters' swing) or not.
bool spoonOut();
ModResult registerActor();
}  // namespace cook

// His lines, in order (registered with the travellers' talk flow), and the monster search.
uint16_t cookFlowNode(int line);
fopAc_ac_c* nearestMonster(const cXyz& at, f32 maxDistance);
