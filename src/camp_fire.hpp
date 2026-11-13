#pragma once
#include "mods/svc/actor.h"

#include "f_op/f_op_actor.h"
#include "SSystem/SComponent/c_phase.h"
#include "d/d_kankyo.h"
#include "d/d_msg_flow.h"
#include "m_Do/m_Do_ext.h"

// A camp's fire: Coro's own fire from Faron Woods (his pot, rocks and firewood, with the map's
// baked shading), holding one of Yeto's soups by province (Faron Simple, Eldin Good, Lanayru
// Superb) with Coro's bubbling surface on top while it burns. It lights itself from late afternoon
// to morning with the game's fire, glow and crackle. Log stools stand at the camp's empty seats;
// barrels and a crate sit by the tents.
//
// Link may scoop one bottle of soup from each camp's pot per day. Try for a second and the
// nearest camper (or, at an empty camp, a big guy right behind him) yells at him.
//
// Parameters: bits 0-7 the camp's road node, bits 8-9 the soup (1 Simple, 2 Good, 3 Superb).
#define PSR_FIRE_NAME "PsrFire"

class psrFire_c : public fopAc_ac_c {
public:
    request_of_phase_process_class mPhCamp;    // PsrCamp.arc (ours): Coro's fire, the bubbles
    request_of_phase_process_class mPhPack;    // PsrPack.arc (ours): the log stool
    request_of_phase_process_class mPhSoup;    // SnowSoup.arc (the game's): Yeto's three soups
    request_of_phase_process_class mPhBarrel;  // J_taru00.arc (the game's)
    request_of_phase_process_class mPhCrate;   // Obj_kbox.arc (the game's)

    static constexpr int kPartNum = 3;
    J3DModel* mpPart[kPartNum] = {};   // rocks, firewood, pot
    J3DModel* mpSoup = nullptr;
    J3DModel* mpBubbles = nullptr;
    mDoExt_btkAnm mBubbling;
    static constexpr int kStoolMax = 8;
    J3DModel* mpStool[kStoolMax] = {};
    static constexpr int kDecorNum = 4;
    J3DModel* mpDecor[kDecorNum] = {};
    f32 mDecorY[kDecorNum] = {};
    bool mDecorGrounded = false;

    int mCampNode = -1;
    int mSoupLevel = 2;
    bool mGrounded = false;
    bool mLoaded = false;

    bool mLit = false;
    cXyz mFlamePos;
    u32 mFlame[3] = {};
    LIGHT_INFLUENCE mLight;

    bool mCatching = false;        // Link's scoop event with this pot is running
    bool mSwinging = false;        // Link was mid bottle-swing last frame
    int mGreedyStep = 0;
    int mGreedySlot = -1;
    int mGreedyTimer = 0;
    dMsgFlow_c mFlow;
    bool mFlowStarted = false;

    virtual ~psrFire_c();
    cPhs_Step create();
    int CreateHeap();
    int Execute();
    int Draw();
    void snapToGround();
    void groundDecor();
    void placeModels();
    void updateFire();
    void updateSoup();
    void updateGreedy();
    static int createHeapCallBack(fopAc_ac_c*);

    static s16 sProcName;
    static ActorHandle sActorHandle;
    static const ActorProfileDesc sProfile;
};

namespace psr_fire {
// Registers the actor and the daily-scoop save record. Call once from mod_initialize.
ModResult registerActor();
}  // namespace psr_fire

// The "HEY! Don't be greedy!" line (registered with the travellers' talk flow).
uint16_t greedyFlowNode();
