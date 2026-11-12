#pragma once
#include "mods/svc/actor.h"

#include "f_op/f_op_actor.h"
#include "SSystem/SComponent/c_phase.h"

// A traveller spawner: an invisible actor placed in each room the road network passes through.
// It is added to the room's own actor list, so the game creates it when the room loads and removes
// it when the room unloads, like Nintendo's own passer spawners (daPasserMng). It is the only thing
// that creates traveller NPCs: when its room loads it places everyone the roster has in that room
// ("scatter", like the vanilla spawner's mid-day scatter), and after that it brings in travellers as
// they walk into the room from somewhere that isn't loaded (a gate, a loading line, an unloaded room).
#define PSR_SPAWNER_NAME "PsrSpwn"

class psrSpawner_c : public fopAc_ac_c {
public:
    bool mScattered;   // the room-load scatter has been done
    int mTick;

    cPhs_Step create();
    int Execute();

    static s16 sProcName;
    static ActorHandle sActorHandle;
    static const ActorProfileDesc sProfile;
};

namespace psr_spawner {
// Registers the actor and adds one spawner to every network room. Call once from mod_initialize.
ModResult registerAndPlace();
}  // namespace psr_spawner
