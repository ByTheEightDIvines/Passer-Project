#pragma once
#include "mods/svc/actor.h"

#include "f_op/f_op_actor.h"
#include "SSystem/SComponent/c_phase.h"
#include "d/d_kankyo.h"

class mDoExt_McaMorfSO;
class daTag_KMsg_c;

// Beedle in his tent (R_PSR00): sitting cross-legged on the mat on his counter with his Wind Waker
// shop animations, and today's six wares on the cushioned tables (the game's own shop displays,
// daShopItem_c). Link talks to him (his welcome) or to a ware on its cushion (Beedle's offer for it,
// the field shop's purchase: the usual price - 5, "THANK YOU!"); a bought ware leaves its cushion for
// the day. "GOODBYE!" when the talk is over. Spawned by shop_stage while Link is in the shop.
#define PSR_TENT_BEEDLE_NAME "PsrBdSt"

class psrTentBeedle_c : public fopAc_ac_c {
public:
    static constexpr int kStands = 6;
    request_of_phase_process_class mPhase;
    mDoExt_McaMorfSO* mpMorf = nullptr;
    Mtx mMtx;
    int mWare[kStands];          // the ware on each cushion (-1: none / sold)
    fpc_ProcID mItem[kStands];   // its display
    int mAnim = -1;              // what he's doing (see tent_beedle.cpp)
    int mPraise = 0;

    virtual ~psrTentBeedle_c();
    cPhs_Step create();
    int CreateHeap();
    int Delete();
    int Execute();
    int Draw();
    static int createHeapCallBack(fopAc_ac_c*);

    static s16 sProcName;
    static ActorHandle sActorHandle;
    static const ActorProfileDesc sProfile;
};

namespace psr_tent_beedle {
bool present();
void spawn(const cXyz& shopCentre, s16 shopYaw, int room);
void reset();
// each frame (mod_update): the talk marker over him or the ware Link is nearest
void talkTick();
// the KMsg hook, for our marker (true: it was ours, the passers' talk leaves it alone)
bool tagPre(daTag_KMsg_c* tag);
bool tagPost(daTag_KMsg_c* tag);
ModResult registerActor();
}  // namespace psr_tent_beedle
