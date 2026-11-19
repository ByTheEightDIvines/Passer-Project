#include "global.h"

#include "tent_beedle.hpp"
#include "beedle_field.hpp"
#include "travellers.hpp"

#include "SSystem/SComponent/c_lib.h"
#include "SSystem/SComponent/c_math.h"
#include "d/actor/d_a_itembase.h"
#include "d/actor/d_a_player.h"
#include "d/actor/d_a_tag_kmsg.h"
#include "d/actor/d_a_tag_shop_item.h"
#include "d/d_com_inf_game.h"
#include "d/d_item_data.h"
#include "d/d_msg_object.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_draw_priority.h"
#include "f_pc/f_pc_manager.h"
#include "m_Do/m_Do_ext.h"
#include "m_Do/m_Do_mtx.h"
#include "mods/service.hpp"

// How the game's shops work (d_shop_system.cpp, d_a_npc_clerkb.cpp, and Malo Mart's talk in zel_02):
// the shopkeeper's flow number G (home.angle.x) is his welcome and G+1 the cursor back on him; each
// TAG_SHOPITM's flow number M (its home.angle.x) is the offer when Link presses A on it and M+1 its
// description while the cursor is on it. Our flows are custom nodes, which the game's flow lookup
// passes straight through (dMsgFlow_c::getInitNodeIndex), so this is the game's own shop at work.
namespace {
constexpr const char* kArc = "P_Beedl";
constexpr const char* kModel = "beedle_tent.bmd";
constexpr u32 kGroup = 1;   // our shop's group (the high nibble of the keeper's, items' and cameras' params)

// his motions (Wind Waker's, on his own skeleton), picked by the motion sequences below; a message's
// talk_anim byte chooses the sequence (daNpcT_c::ctrlMsgAnm), so his lines pick how he talks
struct Motion { const char* bck; bool loop; };
constexpr Motion kMotions[] = {
    {"psr_bs_wait01.bck", true}, {"psr_bs_talk01.bck", true}, {"psr_bs_talk02.bck", true},
    {"psr_bs_talk03.bck", true}, {"psr_bs_talk04.bck", true}, {"psr_bs_praise.bck", false},
};

DUSK_CONSTEXPR daNpcT_evtData_c l_evtList[3] = {{"", 0}, {"DEFAULT_GETITEM", 0}, {"NO_RESPONSE", 0}};
char DUSK_CONST* l_resNameList[2] = {"", "P_Beedl"};
DUSK_CONSTEXPR s8 l_loadResPtrn0[2] = {1, -1};
// no face animations (his face is in his body animations)
DUSK_CONSTEXPR daNpcT_faceMotionAnmData_c l_faceMotionAnmData[1] = {{-1, 0, 0, -1, 0, 0, 0}};
// (unused: setMotionAnm loads his animations by name)
DUSK_CONSTEXPR daNpcT_motionAnmData_c l_motionAnmData[6] = {
    {-1, 2, 1, -1, 0, 0, 0, 0}, {-1, 2, 1, -1, 0, 0, 0, 0}, {-1, 2, 1, -1, 0, 0, 0, 0},
    {-1, 2, 1, -1, 0, 0, 0, 0}, {-1, 2, 1, -1, 0, 0, 0, 0}, {-1, 0, 1, -1, 0, 0, 0, 0},
};
DUSK_CONSTEXPR daNpcT_MotionSeqMngr_c::sequenceStepData_c l_faceMotionSequenceData[24] = {
    {0, -1, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {0, -1, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0},
    {0, -1, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {0, -1, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0},
    {0, -1, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {0, -1, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0},
};
// 0 sitting; 1-4 his four ways of talking; 5 his "THANK YOU" flourish, then sitting again
DUSK_CONSTEXPR daNpcT_MotionSeqMngr_c::sequenceStepData_c l_motionSequenceData[24] = {
    {0, -1, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {1, -1, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0},
    {2, -1, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {3, -1, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0},
    {4, -1, 0}, {-1, 0, 0}, {-1, 0, 0}, {-1, 0, 0}, {5, -1, 1}, {0, -1, 0}, {-1, 0, 0}, {-1, 0, 0},
};

// In the shop's own frame (tools/tents/interior2.py; +z toward the door): cross-legged on the mat on
// the counter, between the lamp pot and the candlestick.
const cXyz kSeat(55.0f, 78.5f, -370.0f);
// The four wares on the four tables at the corners of the aisle, laid out for the game's four-ware cursor
// (dShopSystem_c::chooseItem4: one row, Beedle (0) to the left of 1). Seen from the ware camera inside
// the doorway looking up the aisle toward Beedle, the row runs left to right: near left, far left, far
// right, near right.
const cXyz kSlot[psrTentBeedle_c::kSlots] = {
    cXyz(-130.0f, 72.0f, 240.0f), cXyz(-130.0f, 72.0f, -120.0f),
    cXyz(130.0f, 72.0f, -120.0f), cXyz(130.0f, 72.0f, 240.0f),
};
// the shop cameras: [0] the eye looking at Beedle, [1] the eye looking over the wares (just inside the
// doorway; with four wares the game aims it at the middle of the far pair itself)
const cXyz kCamEye[2] = {cXyz(55.0f, 190.0f, -150.0f), cXyz(0.0f, 170.0f, 470.0f)};
constexpr f32 kMasterLookY = 75.0f;   // over his seat: his chest

bool g_pending = false;
psrTentBeedle_c* g_live = nullptr;
cXyz g_centre;
s16 g_yaw = 0;

// today's sales, by slot (kept while the game runs; a new day brings new wares anyway)
int g_soldDate = -1;
u8 g_soldMask = 0;

cXyz shopToWorld(const cXyz& p) {
    const f32 c = cM_scos(g_yaw), s = cM_ssin(g_yaw);
    return cXyz(g_centre.x + p.x * c + p.z * s, g_centre.y + p.y, g_centre.z - p.x * s + p.z * c);
}

void deleteProc(fpc_ProcID& id) {
    if (id != fpcM_ERROR_PROCESS_ID_e && !fpcM_IsCreating(id)) {
        if (auto* a = fopAcM_SearchByID(id)) fopAcM_delete(a);
    }
    id = fpcM_ERROR_PROCESS_ID_e;
}
}  // namespace

psrTentBeedle_c::~psrTentBeedle_c() {
    for (fpc_ProcID& id : mTag) deleteProc(id);
    for (fpc_ProcID& id : mCam) deleteProc(id);
    if (g_live == this) g_live = nullptr;
    if (heap != nullptr && mpMorf[0] != nullptr) mpMorf[0]->stopZelAnime();
    deleteRes(l_loadResPtrn0, (const char**)l_resNameList);
}

cPhs_Step psrTentBeedle_c::create() {
    daNpcT_ct(this, psrTentBeedle_c, l_faceMotionAnmData, l_motionAnmData, l_faceMotionSequenceData, 4,
              l_motionSequenceData, 4, l_evtList, l_resNameList);
    const int phase = loadRes(l_loadResPtrn0, (const char**)l_resNameList);
    if (phase != cPhs_COMPLEATE_e) return static_cast<cPhs_Step>(phase);

    mFlowNodeNo = beedle_field::shopGreetNode();
    beedle_field::newVisit();
    mTwilight = false;
    for (int i = 0; i < kSlots; ++i) { mWare[i] = -1; mTag[i] = fpcM_ERROR_PROCESS_ID_e; }
    mCam[0] = mCam[1] = fpcM_ERROR_PROCESS_ID_e;
    if (!fopAcM_entrySolidHeap(this, createHeapCallBack, 0x80000000)) {
        g_pending = false;
        travellers::diagLine("shop    Beedle could not be made (his model or heap)");
        return cPhs_ERROR_e;
    }
    fopAcM_SetMtx(this, mpMorf[0]->getModel()->getBaseTRMtx());
    fopAcM_setCullSizeBox(this, -150.0f, -50.0f, -150.0f, 150.0f, 250.0f, 150.0f);
    mSound.init(&current.pos, &eyePos, 3, 1);
    mAcch.Set(fopAcM_GetPosition_p(this), fopAcM_GetOldPosition_p(this), this, 1, &mAcchCir,
              fopAcM_GetSpeed_p(this), fopAcM_GetAngle_p(this), fopAcM_GetShapeAngle_p(this));
    mCcStts.Init(255, 0, this);
    mAcch.CrrPos(dComIfG_Bgsp());
    mGndChk = mAcch.m_gnd;
    mGroundH = mAcch.GetGroundH();
    if (mGroundH != -G_CM3D_F_INF) {
        setEnvTevColor();
        setRoomNo();
    }
    initialize();
    setAngle(home.angle.y);
    current.pos = home.pos;

    // today's wares on the slots (those sold today stay gone); the game's shop items and cameras
    if (g_soldDate != dComIfGs_getDate()) {
        g_soldDate = dComIfGs_getDate();
        g_soldMask = 0;
    }
    int wares[kSlots];
    const int n = beedle_field::tentWares(wares, kSlots);
    int shown = 0;
    for (int i = 0; i < kSlots; ++i) {
        const int ware = i < n ? wares[i] : -1;
        mWare[i] = ware >= 0 && !(g_soldMask & (1u << i)) ? ware : -1;
        if (mWare[i] >= 0) ++shown;
        // an empty slot still gets its tag (the game's shop counts four), shown nothing (soldTick)
        const int w = ware >= 0 ? ware : (n > 0 ? wares[0] : 0);
        const u16 flow = beedle_field::shopWareNode(w);
        const u32 param = (kGroup << 28) | (static_cast<u32>(i + 1) << 24) | beedle_field::wareDisplayItem(w);
        const cXyz at = shopToWorld(kSlot[i]);
        const csXyz angle(static_cast<s16>(flow), static_cast<s16>(g_yaw + (kSlot[i].x > 0 ? -0x4000 : 0x4000)),
                          static_cast<s16>(0xFFFF));   // no save switches: the game's are not ours
        mTag[i] = fopAcM_create(fpcNm_TAG_SHOPITM_e, param, &at, fopAcM_GetRoomNo(this), &angle, nullptr, -1);
    }
    for (int k = 0; k < 2; ++k) {
        const cXyz at = shopToWorld(kCamEye[k]);
        mCam[k] = fopAcM_create(fpcNm_TAG_SHOPCAM_e, (kGroup << 28) | static_cast<u32>(k), &at,
                                fopAcM_GetRoomNo(this), nullptr, nullptr, -1);
    }
    mSerial = beedle_field::boughtSerial();
    g_live = this;
    g_pending = false;
    // his talk is registered in the field's message group (8); the shop stage must load that group, or the
    // game finds none of his flow nodes (the camera turns to him and nothing is said)
    travellers::diagLine("shop    Beedle on his counter, %d wares out (flow %d, message group %d%s)", shown, mFlowNodeNo,
                         dMsgObject_getGroupID(), dMsgObject_getGroupID() == 8 ? "" : ": NOT 8, his talk won't be found");

    mCreating = true;
    Execute();
    mCreating = false;
    return cPhs_COMPLEATE_e;
}

int psrTentBeedle_c::CreateHeap() {
    auto* data = static_cast<J3DModelData*>(dComIfG_getObjectRes(kArc, kModel));
    if (data == nullptr) return 0;
    mpMorf[0] = JKR_NEW mDoExt_McaMorfSO(data, nullptr, nullptr, nullptr, -1, 1.0f, 0, -1, &mSound, 0, 0x11020285);
    if (mpMorf[0] != nullptr && mpMorf[0]->getModel() == nullptr) {
        mpMorf[0]->stopZelAnime();
        mpMorf[0] = nullptr;
    }
    if (mpMorf[0] == nullptr) return 0;
    mpMorf[0]->getModel()->setUserArea(reinterpret_cast<uintptr_t>(this));
    return setMotionAnm(0, 0.0f, FALSE) ? 1 : 0;
}

int psrTentBeedle_c::createHeapCallBack(fopAc_ac_c* a) { return static_cast<psrTentBeedle_c*>(a)->CreateHeap(); }

int psrTentBeedle_c::Delete() {
    this->~psrTentBeedle_c();
    return 1;
}

bool psrTentBeedle_c::setMotionAnm(int idx, f32 morf, BOOL restart) {
    if (idx < 0 || idx >= static_cast<int>(sizeof(kMotions) / sizeof(kMotions[0]))) idx = 0;
    mAnmFlags &= ~(ANM_PAUSE_MORF | ANM_PAUSE_BTK | ANM_PAUSE_BRK | ANM_PAUSE_BPK | ANM_PLAY_MORF |
                   ANM_PLAY_BTK | ANM_PLAY_BRK | ANM_PLAY_BPK);
    auto* bck = static_cast<J3DAnmTransformKey*>(dComIfG_getObjectRes(kArc, kMotions[idx].bck));
    if (bck == nullptr || mpMorf[0] == nullptr) return false;
    if (restart && bck == mpMorf[0]->getAnm()) {
        mAnmFlags |= ANM_PLAY_MORF;
        mMorfLoops = 0;
        return true;
    }
    if (!setMcaMorfAnm(bck, 1.0f, morf, kMotions[idx].loop ? J3DFrameCtrl::EMode_LOOP : J3DFrameCtrl::EMode_NONE, 0, -1)) {
        return false;
    }
    mAnmFlags |= ANM_PAUSE_MORF | ANM_PLAY_MORF;
    mMorfLoops = 0;
    return true;
}

void psrTentBeedle_c::setParam() {
    if (mCamReset) {   // the shop's over: the camera back to Link's (as Malver's)
        if (mShopProcess == 2) mShopCamAction.Reset();
        else mShopCamAction.EventRecoverNotime();
        mCamReset = false;
    }
    if (mShopProcess == 2 && !mInShop) {   // (after an item-get scene the game reopens the talk)
        mSpeakEvent = true;
        field_0xe33 = true;
    }
    attention_info.distances[fopAc_attn_LOCK_e] = daNpcT_getDistTableIdx(5, 6);
    attention_info.distances[fopAc_attn_TALK_e] = attention_info.distances[fopAc_attn_LOCK_e];
    attention_info.distances[fopAc_attn_SPEAK_e] = daNpcT_getDistTableIdx(4, 6);
    attention_info.flags = daPy_py_c::checkNowWolf() ? 0 : (fopAc_AttnFlag_SPEAK_e | fopAc_AttnFlag_TALK_e);
    scale.set(1.0f, 1.0f, 1.0f);
    mCylH = 120.0f;
    mWallR = 30.0f;
    mAttnFovY = 110.0f;
    mAcchCir.SetWallR(mWallR);
    mAcchCir.SetWallH(35.0f);
    mRealShadowSize = 0.0f;
    gravity = 0.0f;   // he sits on his counter: no falling
    speedF = 0.0f;
    speed.setall(0.0f);
    mExpressionMorfFrame = 4.0f;
    mMorfFrames = 8.0f;
}

BOOL psrTentBeedle_c::evtTalk() {
    if (mInShop) {
        shopStep();
        return TRUE;
    }
    mPreItemNo = 0;
    if (dComIfGp_event_chkTalkXY()) {   // an item shown to him: no answer
        if (!dComIfGp_evmng_ChkPresentEnd()) return TRUE;
        mEvtNo = 2;
        evtChange();
        return TRUE;
    }
    if (!mShopFlag || mFlowNodeNo < 0 || static_cast<u32>(mFlowNodeNo) == 0xFFFF) {
        dComIfGp_event_reset();
        return TRUE;
    }
    mShopCamAction.shop_cam_action_init();
    mInShop = true;
    mShopEnter = true;
    shopStep();
    return TRUE;
}

// the game's shop, a frame of it (as Malver's shop(): the talk, the cursor, the wares)
void psrTentBeedle_c::shopStep() {
    if (mShopEnter) {
        if (mShopProcess == 2) {
            shop_init(true);
        } else {
            mShopCamAction.Save();
            initTalk(mFlowNodeNo, nullptr);
            shop_init(false);
        }
        mShopEnter = false;
    }
    mShopProcess = shop_process(this, &mFlow);
    if (mShopProcess != 0) {
        mPlayerActorMngr.entry(daPy_getPlayerActorClass());
        dComIfGp_event_reset();
        mInShop = false;
        mCamReset = true;
    }
    cXyz look = current.pos;
    look.y += kMasterLookY;
    mShopCamAction.setMasterCamCtrPos(&look);
}

void psrTentBeedle_c::setAttnPos() {
    // he stays on his mat (no ground under him but the counter, and he's lower than its top)
    current.pos = home.pos;
    old.pos = home.pos;
    setAngle(home.angle.y);
    setMtx();
    eyePos = current.pos;
    eyePos.y += 95.0f;
    attention_info.position = current.pos;
    attention_info.position.y += 140.0f;
}

void psrTentBeedle_c::drawOtherMdl() {
    if (mShopFlag) {
        cXyz pos(0.0f, 0.0f, 0.0f);
        itemRotate();
        itemZoom(&pos);
        drawCursor();
    }
}

void psrTentBeedle_c::soldTick() {
    // a purchase: the ware under the cursor leaves its cushion for the day
    if (beedle_field::boughtSerial() != mSerial) {
        mSerial = beedle_field::boughtSerial();
        const int k = static_cast<int>(mCursorPos) - 1;
        if (k >= 0 && k < kSlots && mWare[k] == beedle_field::lastBought()) {
            mWare[k] = -1;
            g_soldMask |= static_cast<u8>(1u << k);
        }
    }
    for (int k = 0; k < kSlots; ++k) {
        if (mWare[k] >= 0) continue;
        onFlag(k);              // the cursor passes it by
        onSoldOutItemFlag(k);
        // the display hidden (not deleted: the shop still waits for it to settle back home), and its tag
        // gone (the tag would show it again every frame)
        if (auto* item = static_cast<daItemBase_c*>(fopAcM_SearchByID(mItemCtrl.getItemIndex(k)))) {
            item->hide();
            deleteProc(mTag[k]);
        }
    }
}

int psrTentBeedle_c::Execute() {
    // the shop opens once its tags have their displays (the game's shop reads them as it finds them)
    if (!mCreating && !mShopFlag && !checkShopOpen()) {
        bool ready = true;
        for (int k = 0; k < kSlots; ++k) {
            auto* tag = mTag[k] != fpcM_ERROR_PROCESS_ID_e && !fpcM_IsCreating(mTag[k])
                            ? static_cast<daTag_ShopItem_c*>(fopAcM_SearchByID(mTag[k])) : nullptr;
            if (tag == nullptr || tag->getProcessID() == fpcM_ERROR_PROCESS_ID_e || fpcM_IsCreating(tag->getProcessID())) {
                ready = false;
            }
        }
        for (int k = 0; k < 2; ++k) {
            if (mCam[k] == fpcM_ERROR_PROCESS_ID_e || fpcM_IsCreating(mCam[k])) ready = false;
        }
        if (ready) {
            initShopSystem();
            setSellItemMax(kSlots);
            field_0xf60 = -1;   // the ordinary shop (items found by their tags)
            mShopCamAction.setCamDataIdx(0);
            mShopCamAction.setCamAction(nullptr);
            mShopFlag = true;
        }
    }
    execute();
    if (mShopFlag && searchItemActor()) {
        mShopCamAction.move();
        soldTick();
    }
    return 1;
}

int psrTentBeedle_c::Draw() {
    if (mpMorf[0] == nullptr) return 1;
    return draw(FALSE, FALSE, mRealShadowSize, nullptr, 0.0f, FALSE, TRUE, FALSE);
}

namespace {
cPhs_Step psrTentBeedle_Create(void* p) { return static_cast<psrTentBeedle_c*>(p)->create(); }
int psrTentBeedle_Delete(void* p) { return static_cast<psrTentBeedle_c*>(p)->Delete(); }
int psrTentBeedle_Execute(void* p) { return static_cast<psrTentBeedle_c*>(p)->Execute(); }
int psrTentBeedle_Draw(void* p) { return static_cast<psrTentBeedle_c*>(p)->Draw(); }
int psrTentBeedle_IsDelete(void*) { return 1; }
}  // namespace

s16 psrTentBeedle_c::sProcName = -1;
ActorHandle psrTentBeedle_c::sActorHandle = -1;
const ActorProfileDesc psrTentBeedle_c::sProfile = {
    .name = PSR_TENT_BEEDLE_NAME,
    .priority_group = 7,
    .process_size = sizeof(psrTentBeedle_c),
    .draw_priority = fpcDwPi_NPC_CLERKB_e,
    .status = fopAcStts_UNK_0x40000_e | fopAcStts_UNK_0x4000_e | fopAcStts_CULL_e | fopAcStts_UNK_0x4_e |
              fopAcStts_UNK_0x2_e | fopAcStts_UNK_0x1_e,
    .group = fopAc_NPC_e,
    .cull_type = fopAc_CULLBOX_CUSTOM_e,
    .create_function = psrTentBeedle_Create,
    .delete_function = psrTentBeedle_Delete,
    .execute_function = psrTentBeedle_Execute,
    .is_delete_function = psrTentBeedle_IsDelete,
    .draw_function = psrTentBeedle_Draw,
};

namespace psr_tent_beedle {
bool present() { return g_live != nullptr || g_pending; }

void spawn(const cXyz& shopCentre, s16 shopYaw, int room) {
    if (psrTentBeedle_c::sProcName < 0 || present()) return;
    const u16 greet = beedle_field::shopGreetNode();
    if (greet == 0xFFFF) return;   // his talk isn't ready (yet)
    g_centre = shopCentre;
    g_yaw = shopYaw;
    const cXyz at = shopToWorld(kSeat);
    const csXyz angle(static_cast<s16>(greet), shopYaw, 0);   // home.angle.x: his flow (the game's shop reads it)
    const fpc_ProcID id = fopAcM_create(psrTentBeedle_c::sProcName, kGroup << 28, &at, room, &angle, nullptr, -1);
    g_pending = id != fpcM_ERROR_PROCESS_ID_e;
}

void reset() { g_pending = false; }
void talkTick() {}
bool tagPre(daTag_KMsg_c*) { return false; }
bool tagPost(daTag_KMsg_c*) { return false; }

ModResult registerActor() {
    return svc_actor->register_actor(mod_ctx, &psrTentBeedle_c::sProfile, &psrTentBeedle_c::sProcName,
                                     &psrTentBeedle_c::sActorHandle);
}
}  // namespace psr_tent_beedle
