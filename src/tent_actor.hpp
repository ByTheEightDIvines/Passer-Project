#pragma once
#include "mods/svc/actor.h"

#include "f_op/f_op_actor.h"
#include "SSystem/SComponent/c_phase.h"
#include "d/d_bg_w.h"
#include "d/d_bg_s_acch.h"
#include "d/d_kankyo.h"

// The camp tents: one actor, the tent kind is the low byte of its parameters.
//   0 = small A-tent, 1 = big wall tent, 2 = Beedle's tent (with its night glow).
// Models and collision come from PsrTent.arc, which ships in the dusk's overlay (a new archive;
// nothing on the disc is replaced).
#define PSR_TENT_NAME "PsrTent"

enum PsrTentKind : u8 { PSR_TENT_A = 0, PSR_TENT_WALL = 1, PSR_TENT_BEEDLE = 2, PSR_TENT_GUARD = 3, PSR_TENT_KIND_NUM };
// Parameter bit: Link's own tent (player_camp.cpp), not a travellers' bed.
constexpr u32 PSR_TENT_PLAYER = 0x100;

class psrTent_c : public fopAc_ac_c {
public:
    request_of_phase_process_class mPhase;
    request_of_phase_process_class mPhaseLamp;    // PsrPack.arc (the entry lantern, psr_shop_kntr.bmd)
    request_of_phase_process_class mPhaseRoom;    // PsrBdRm.arc: the shop seen through Beedle's tent (his tent only)
    J3DModel* mpModel;
    static constexpr int kStageMax = 4;
    J3DModel* mpStage[kStageMax];                 // the game's own props behind Beedle's curtain
    int mStageNum;
    J3DModel* mpRoom[2];        // Beedle's tent only: the baked room, day and night
    J3DModel* mpBedroll;        // small tent only: a bedroll on the floor, head at the back
    J3DModel* mpFlap[2];        // small tent only: the door flaps, left and right (the right is the left mirrored)
    J3DModel* mpLit[3];         // small tent at night with someone inside: lit from within (tent, both flaps)
    bool mLit = false;
    ActorId mFlame = 0;         // (old: the game's fireWd2 in the entry lantern; no longer made)
    bool mHasFlame = false;
    u32 mLampFlame = 0;         // Beedle's tent at night: Link's lantern fire on the entry lantern's wick
    LIGHT_INFLUENCE mLampGlow{};
    bool mGlowLit = false;
    f32 mFlapOpen = 1.0f;       // 1 = tied back open, 0 = shut
    f32 mFlapTarget = 1.0f;
    void setFlaps(bool open) { mFlapTarget = open ? 1.0f : 0.0f; }
    Mtx mColliderMtx;
    dBgW* mpCollider;
    u8 mKind;
    bool mGrounded;
    s16 mTiltX = 0;             // lean to follow the slope it stands on (about its own x axis)
    s16 mTiltZ = 0;             // ...and about its own z axis

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
// How much bigger than the modelled size each tent stands in the world (A-tent, wall tent, Beedle's,
// the guards' wall tent).
constexpr f32 kScale[PSR_TENT_KIND_NUM] = {1.3f, 1.3f, 1.6f, 1.3f};
// Half the size of each tent's footprint on the ground at that scale (x across, z front to back).
constexpr f32 kFootX[PSR_TENT_KIND_NUM] = {150.0f * 1.3f, 310.0f * 1.3f, 380.0f * 1.6f, 310.0f * 1.3f};
constexpr f32 kFootZ[PSR_TENT_KIND_NUM] = {245.0f * 1.3f, 445.0f * 1.3f, 548.0f * 1.6f, 445.0f * 1.3f};

// Someone (a traveller, the cook) ended up standing on a tent's canvas: the sloped roof counts as
// ground, so anyone who bumped into a tent walked up onto it. Puts them back down on the grass just
// outside the nearest side of that tent. Returns true if they were moved.
bool pushOff(fopAc_ac_c* npc, dBgS_Acch& acch);

// A tent stands across the straight way from `from` to `to`: the corner (out past the canvas) to walk
// to first so the way goes round it. Returns that tent (nullptr if none is in the way).
fopAc_ac_c* detour(const cXyz& from, const cXyz& to, cXyz* corner);

// Registers the actor with the game. Call once from mod_initialize.
ModResult registerActor();
}  // namespace psr_tent
