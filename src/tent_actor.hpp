#pragma once
#include "mods/svc/actor.h"

#include "f_op/f_op_actor.h"
#include "SSystem/SComponent/c_phase.h"
#include "d/d_bg_w.h"

// The camp tents: one actor, the tent kind is the low byte of its parameters.
//   0 = small A-tent, 1 = big wall tent, 2 = Beedle's tent (with its night glow).
// Models and collision come from PsrTent.arc, which ships in the dusk's overlay (a new archive;
// nothing on the disc is replaced).
#define PSR_TENT_NAME "PsrTent"

enum PsrTentKind : u8 { PSR_TENT_A = 0, PSR_TENT_WALL = 1, PSR_TENT_BEEDLE = 2, PSR_TENT_KIND_NUM };

class psrTent_c : public fopAc_ac_c {
public:
    request_of_phase_process_class mPhase;
    request_of_phase_process_class mPhaseTable;   // HTable.arc (Beedle's stage)
    request_of_phase_process_class mPhaseLamp;    // Obj_kntr.arc (the passers' lantern)
    J3DModel* mpModel;
    static constexpr int kStageMax = 4;
    J3DModel* mpStage[kStageMax];                 // the game's own props behind Beedle's curtain
    int mStageNum;
    J3DModel* mpGlow;           // Beedle's tent only: windows, lantern and the room through the curtain
    Mtx mColliderMtx;
    dBgW* mpCollider;
    u8 mKind;
    bool mGrounded;

    virtual ~psrTent_c();
    cPhs_Step create();
    int CreateHeap();
    int Delete();
    int Execute();
    int Draw();
    void snapToGround();
    static int createHeapCallBack(fopAc_ac_c*);

    static s16 sProcName;
    static ActorHandle sActorHandle;
    static const ActorProfileDesc sProfile;
};

namespace psr_tent {
// Registers the actor with the game. Call once from mod_initialize.
ModResult registerActor();
}  // namespace psr_tent
