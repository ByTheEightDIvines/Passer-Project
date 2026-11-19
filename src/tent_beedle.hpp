#pragma once
#include "mods/svc/actor.h"

#include "d/d_shop_system.h"

class daTag_KMsg_c;

// Beedle in his tent (R_PSR00): a real game shopkeeper (dShopSystem_c, as Malver and Malo are),
// sitting cross-legged on the mat on his counter with his Wind Waker shop animations. Today's five
// wares sit on the cushioned tables as the game's own shop items (TAG_SHOPITM, each with its
// daShopItem_c display) and the game's shop does the rest: its camera, its cursor hopping between
// the wares, the description box, the offer and its choice. Our part is the talk (beedle_field.cpp):
// his welcome, the offer, the purchase (the usual price - 5), "THANK YOU!", and "GOODBYE!" when Link
// backs out. A bought ware leaves its cushion for the day. Spawned by shop_stage while Link is inside.
#define PSR_TENT_BEEDLE_NAME "PsrBdSt"

class psrTentBeedle_c : public dShopSystem_c {
public:
    static constexpr int kSlots = 5;

    psrTentBeedle_c(daNpcT_faceMotionAnmData_c const* a, daNpcT_motionAnmData_c const* b,
                    daNpcT_MotionSeqMngr_c::sequenceStepData_c const* c, int d,
                    daNpcT_MotionSeqMngr_c::sequenceStepData_c const* e, int f, daNpcT_evtData_c const* g,
                    char DUSK_CONST* DUSK_CONST* h)
        : dShopSystem_c(a, b, c, d, e, f, g, h) {}
    ~psrTentBeedle_c();

    cPhs_Step create();
    int CreateHeap();
    int Delete();
    int Execute();
    int Draw();
    static int createHeapCallBack(fopAc_ac_c*);

    // daNpcT_c
    void setParam() override;
    BOOL evtTalk() override;
    void setAttnPos() override;
    void drawOtherMdl() override;
    bool setMotionAnm(int idx, f32 morf, BOOL restart) override;

    void shopStep();
    void soldTick();
    void cameraTick();

    int mWare[kSlots];                 // the ware on each slot (-1: none today / sold)
    fpc_ProcID mTag[kSlots];           // its TAG_SHOPITM
    fpc_ProcID mCam[2];                // the shop's two camera marks (TAG_SHOPCAM)
    int mShopProcess = 0;
    int mSerial = 0;
    bool mShopFlag = false;
    bool mInShop = false;
    bool mShopEnter = false;
    bool mCamReset = false;

    static s16 sProcName;
    static ActorHandle sActorHandle;
    static const ActorProfileDesc sProfile;
};

namespace psr_tent_beedle {
bool present();
void spawn(const cXyz& shopCentre, s16 shopYaw, int room);
void reset();
void talkTick();
// the KMsg hook (the tent shop no longer uses a marker: always false)
bool tagPre(daTag_KMsg_c* tag);
bool tagPost(daTag_KMsg_c* tag);
ModResult registerActor();
}  // namespace psr_tent_beedle
