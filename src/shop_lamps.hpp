#pragma once
#include "mods/svc/actor.h"

#include "f_op/f_op_actor.h"
#include "SSystem/SComponent/c_phase.h"
#include "d/d_kankyo.h"

// The three lanterns hanging from the ridge of Beedle's shop (R_PSR00). They used to be baked into the
// shop's model, and a baked copy can't keep the lantern's see-through glass (the game draws it as a
// separate blended material), so the flame inside was hidden behind solid glass. Now the game's own
// lantern (Obj_kntr.arc, ob_kantera.bmd: the one the travellers carry) is drawn here with its own
// materials, and at night each gets Link's lantern fire and a warm point light, from this actor's own
// update. Spawned by shop_stage while Link is in the shop; the stage change takes it away.
#define PSR_SHOP_LAMPS_NAME "PsrLamp"

class psrShopLamps_c : public fopAc_ac_c {
public:
    static constexpr int kNum = 3;
    request_of_phase_process_class mPhase;
    J3DModel* mpLamp[kNum];
    u32 mFlame[kNum];
    LIGHT_INFLUENCE mGlow[kNum];
    bool mLit;

    virtual ~psrShopLamps_c();
    cPhs_Step create();
    int CreateHeap();
    int Delete();
    int Execute();
    int Draw();
    void lightsOut();
    static int createHeapCallBack(fopAc_ac_c*);

    static s16 sProcName;
    static ActorHandle sActorHandle;
    static const ActorProfileDesc sProfile;
};

namespace psr_shop_lamps {
// Is the lanterns' actor in the stage (or on its way)?
bool present();
// Puts the lanterns up in the shop: the shop's own frame (its centre on the floor and its facing).
void spawn(const cXyz& centre, s16 yaw, int room);
// Forget a spawn that never arrived (call when Link is not in the shop).
void reset();
ModResult registerActor();
}  // namespace psr_shop_lamps
