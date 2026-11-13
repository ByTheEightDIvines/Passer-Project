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

constexpr int kSlots = 32;               // travellers (and real actors at most)
constexpr int kWalkPathBase = 0x40;       // path IDs 0x40..0x5F: each slot's walking path
constexpr int kFleePathBase = 0x60;       // path IDs 0x60..0x7F: each slot's flee path

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
bool startFlee(daNpcPasser_c* passer, const cXyz& threat);
// Back to walking toward its destination from wherever it is now (after cowering or talking).
void resumeWalk(daNpcPasser_c* passer);
// The actor is about to be removed because it died (Wind Waker launch finished).
void onDeath(daNpcPasser_c* passer);
// Hearts, in quarter hearts (4 per heart, like Link's). -1 if the actor isn't a traveller.
int life(const fopAc_ac_c* passer);
int maxLife(const fopAc_ac_c* passer);
void setLife(const fopAc_ac_c* passer, int quarters);
// Fighters stand their ground and only run once they are down to their last heart.
bool isFighter(const fopAc_ac_c* passer);
// Roster slot of a traveller actor (0..31), or -1.
int slotIndex(const fopAc_ac_c* actor);
// Diagnostics: a traveller was hit; an enemy started or ended a hunt for one.
void noteHit(const fopAc_ac_c* passer, int damage, int lifeLeft);
// Settled at a camp for the night (seated): camp centre, its seat and the way it faces.
bool campInfo(const fopAc_ac_c* passer, cXyz* center, cXyz* seat, s16* seatYaw);
// The seats around a camp's fire (campNode = the camp's road node), for the fire to draw its stumps.
int campSeatCount(int campNode);
bool campSeatSpot(int campNode, int seat, cXyz* pos, s16* yaw);
bool campSeatInUse(int campNode, int seat);
int bigSeat();                                // the big guy's reserved seat number at every camp
bool inBigSeat(const fopAc_ac_c* passer);      // this traveller is the big guy, in his seat   // someone is sitting on it right now (it draws its own)
// A second scoop of a camp's soup: the nearest camper, or a big guy right behind Link, comes to yell.
// Returns the roster slot (-1: nobody available). He stands until the yelling is over, then sits.
int summonGreedyGuard(int campNode, const cXyz& link, s16 linkYaw, s8 room);
fopAc_ac_c* slotActor(int slot);
bool walkingToSeat(const fopAc_ac_c* passer);
void doneWalkingToSeat(const fopAc_ac_c* passer);
// Put its resting (sitting) animation back next frame.
void refreshIdle(const fopAc_ac_c* passer);
void noteCamp(const fopAc_ac_c* passer, const char* what, const fopAc_ac_c* enemy);
void noteStand(const fopAc_ac_c* passer, const fopAc_ac_c* enemy);
void noteHunt(const fopAc_ac_c* enemy, const fopAc_ac_c* prey, bool start);
// Nearest live traveller actor to a point, within maxDist (xz), or nullptr.
daNpcPasser_c* nearestActor(const cXyz& pos, f32 maxDist);
// Is this one of the Hyrule Field stages the network covers?
bool inNetworkStage();
// Diagnostics: an enemy's targeting answer was swapped from Link to a traveller.
void noteEnemyRedirect();
// Diagnostics: what a traveller is doing (logged if the game deletes it).
void noteAct(fopAc_ac_c* passer, const char* what);

// --- room spawners (spawner_actor.cpp) ---
struct SpawnerSpot { const char* stage; int room; f32 x, y, z; };
// i-th room the network passes through, with a point in it; false past the last one.
bool spawnerSpot(int i, SpawnerSpot* out);
// A room's spawner, once per tick. firstTick is true until the room-load scatter has been done;
// returns true once it has (it waits through events and twilight).
bool spawnerTick(fopAc_ac_c* spawner, bool firstTick);

// The path lookup only answers our own walkers: true while one of them is setting its path up.
bool pathLookupOpen();
void setPathLookupOpen(bool open);

}  // namespace travellers
