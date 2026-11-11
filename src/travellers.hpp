#pragma once
// Dynamic Travelers: the travel network and the travellers that walk it.
//
// Every traveller always exists as a "virtual" traveller moving along the network on the game
// clock (roads by day, the nearest camp at dusk, camp at night, back out at dawn; Castle Town
// gates and loading screens are walked through). When one is in the loaded stage and room, near
// Link, a real passer actor is created on its spot and walks the same road with a path this mod
// supplies; when it's far away again the actor is removed and it carries on virtually.

#include "d/actor/d_a_npc_passer.h"
#include "d/d_path.h"

namespace travellers {

constexpr int kSlots = 24;               // travellers (and real actors at most)
constexpr int kWalkPathBase = 0x40;       // path IDs 0x40..0x57: each slot's walking path
constexpr int kFleePathBase = 0x60;       // path IDs 0x60..0x77: each slot's flee path

void initialize();
void update();                            // once per frame (mod_update)

// The passer actor belongs to this mod's travellers.
bool isTraveller(const fopAc_ac_c* actor);
// The path a traveller's actor asked for (dPath_GetRoomPath hook), or nullptr.
dPath* pathFor(int pathId);
// The actor reached the end of the path it was given. Returns true when handled (skip vanilla,
// which would delete it on the spot).
bool onPathEnd(daNpcPasser_c* passer);
// Vanilla flee needs an escape marker actor; travellers run back along their own road instead.
// Returns true when handled.
bool startFlee(daNpcPasser_c* passer);
// Back to walking toward its destination from wherever it is now (after cowering or talking).
void resumeWalk(daNpcPasser_c* passer);
// The actor is about to be removed because it died (Wind Waker launch finished).
void onDeath(daNpcPasser_c* passer);
// Nearest live traveller actor to a point, within maxDist (xz), or nullptr.
daNpcPasser_c* nearestActor(const cXyz& pos, f32 maxDist);
// Is this one of the Hyrule Field stages the network covers?
bool inNetworkStage();

}  // namespace travellers
