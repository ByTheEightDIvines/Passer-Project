#pragma once
// The field around Beedle's tent, seen from inside his shop (R_PSR00) through the doorway and windows:
// four pre-rendered boxes (morning, day, evening, night) from PsrSky.arc, drawn unlit round the shop,
// the one for the time of day. Placed by shop_stage when Link walks in; it goes with the stage.
#include "mods/svc/actor.h"
#include "f_op/f_op_actor.h"
#include "SSystem/SComponent/c_phase.h"

#define PSR_SKY_NAME "PsrSky"

class psrSky_c : public fopAc_ac_c {
public:
    request_of_phase_process_class mPhase;
    J3DModel* mpBox[4];          // morning, day, evening, night
    virtual ~psrSky_c();
    cPhs_Step create();
    int CreateHeap();
    int Execute();
    int Draw();
    static s16 sProcName;
    static ActorHandle sActorHandle;
    static const ActorProfileDesc sProfile;
};

namespace psr_sky {
ModResult registerActor();       // once, from mod_initialize
int boxForHour(int hour);        // 0 morning (5-9), 1 day (9-16), 2 evening (16-19), 3 night (19-5)
}
