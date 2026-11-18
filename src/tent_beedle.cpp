#include "global.h"

#include "tent_beedle.hpp"
#include "beedle_field.hpp"
#include "travellers.hpp"

#include "SSystem/SComponent/c_lib.h"
#include "SSystem/SComponent/c_math.h"
#include "d/actor/d_a_player.h"
#include "d/actor/d_a_tag_kmsg.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_draw_priority.h"
#include "f_pc/f_pc_manager.h"
#include "m_Do/m_Do_ext.h"
#include "m_Do/m_Do_mtx.h"
#include "mods/service.hpp"

namespace {
constexpr const char* kArc = "P_Beedl";
constexpr const char* kModel = "beedle_tent.bmd";
// His anims (Wind Waker's, on his own skeleton): sitting, his four ways of talking, his "THANK YOU".
constexpr const char* kWait = "psr_bs_wait01.bck";
constexpr const char* kTalks[4] = {"psr_bs_talk01.bck", "psr_bs_talk02.bck", "psr_bs_talk03.bck", "psr_bs_talk04.bck"};
constexpr const char* kPraise = "psr_bs_praise.bck";
constexpr u32 kHeapSize = 0x80000000;   // auto-sized

// In the shop's own frame (tools/tents/interior2.py; +z toward the door): cross-legged on the mat on
// the counter (its top 98 up; the mat 1.5 over it; his lowest point is 21 over his origin), between
// the lamp pot and the candlestick. And the six cushioned tables (STANDS: the cushion tops).
const cXyz kSeat(55.0f, 78.5f, -370.0f);
const cXyz kStand[psrTentBeedle_c::kStands] = {
    cXyz(-130.0f, 72.0f, -120.0f), cXyz(-130.0f, 72.0f, 60.0f), cXyz(-130.0f, 72.0f, 240.0f),
    cXyz(130.0f, 72.0f, -120.0f),  cXyz(130.0f, 72.0f, 60.0f),  cXyz(130.0f, 72.0f, 240.0f),
};
constexpr f32 kItemReach = 170.0f;     // Link this near a ware: the marker's on it
constexpr f32 kBeedleReach = 340.0f;   // ...or this near him (at the counter's front)

bool g_pending = false;
psrTentBeedle_c* g_live = nullptr;
cXyz g_centre;
s16 g_yaw = 0;

// today's sales, by cushion (kept while the game runs; a new day brings new wares anyway)
int g_soldDate = -1;
u8 g_soldMask = 0;

// the talk marker
fpc_ProcID g_tag = fpcM_ERROR_PROCESS_ID_e;
int g_target = -1;         // -1 nobody; 0 Beedle; 1..6 the ware on that cushion
bool g_talking = false;
int g_talkTarget = -1;
int g_serial = 0;

cXyz shopToWorld(const cXyz& p) {
    const f32 c = cM_scos(g_yaw), s = cM_ssin(g_yaw);
    return cXyz(g_centre.x + p.x * c + p.z * s, g_centre.y + p.y, g_centre.z - p.x * s + p.z * c);
}

void play(psrTentBeedle_c* b, const char* file, bool loop, int which) {
    if (b == nullptr || b->mpMorf == nullptr || b->mAnim == which) return;
    auto* anm = static_cast<J3DAnmTransform*>(dComIfG_getObjectRes(kArc, file));
    if (anm == nullptr) return;
    b->mpMorf->setAnm(anm, loop ? J3DFrameCtrl::EMode_LOOP : J3DFrameCtrl::EMode_NONE, 8.0f, 1.0f, 0.0f, -1.0f);
    b->mAnim = which;
}

cXyz targetPos(int t) {
    if (t == 0) return shopToWorld(cXyz(kSeat.x, 0.0f, kSeat.z + 90.0f));   // at the counter's front
    return shopToWorld(kStand[t - 1]);
}

void dropTag() {
    if (g_tag != fpcM_ERROR_PROCESS_ID_e && !fpcM_IsCreating(g_tag)) {
        if (auto* t = fopAcM_SearchByID(g_tag)) fopAcM_delete(t);
    }
    g_tag = fpcM_ERROR_PROCESS_ID_e;
}
}  // namespace

psrTentBeedle_c::~psrTentBeedle_c() {
    for (fpc_ProcID& id : mItem) {
        if (id != fpcM_ERROR_PROCESS_ID_e && !fpcM_IsCreating(id)) {
            if (auto* a = fopAcM_SearchByID(id)) fopAcM_delete(a);
        }
        id = fpcM_ERROR_PROCESS_ID_e;
    }
    if (g_live == this) g_live = nullptr;
    g_talking = false;
    g_tag = fpcM_ERROR_PROCESS_ID_e;   // (the stage change takes the marker too)
    dComIfG_resDelete(&mPhase, kArc);
}

cPhs_Step psrTentBeedle_c::create() {
    fopAcM_ct(this, psrTentBeedle_c);
    const cPhs_Step step = dComIfG_resLoad(&mPhase, kArc);
    if (step != cPhs_COMPLEATE_e) return step;
    mpMorf = nullptr;
    mAnim = -1;
    mPraise = 0;
    for (int i = 0; i < kStands; ++i) {
        mWare[i] = -1;
        mItem[i] = fpcM_ERROR_PROCESS_ID_e;
    }
    if (!fopAcM_entrySolidHeap(this, createHeapCallBack, kHeapSize)) {
        g_pending = false;
        travellers::diagLine("shop    Beedle could not be made (his model or heap)");
        return cPhs_ERROR_e;
    }
    mDoMtx_stack_c::transS(current.pos.x, current.pos.y, current.pos.z);
    mDoMtx_stack_c::YrotM(shape_angle.y);
    cMtx_copy(mDoMtx_stack_c::get(), mMtx);
    fopAcM_SetMtx(this, mMtx);
    fopAcM_setCullSizeBox(this, -120.0f, -20.0f, -120.0f, 120.0f, 200.0f, 120.0f);
    attention_info.flags = 0;
    g_live = this;
    g_pending = false;
    play(this, kWait, true, 0);

    // today's wares on the cushions (those sold today stay gone)
    if (g_soldDate != dComIfGs_getDate()) {
        g_soldDate = dComIfGs_getDate();
        g_soldMask = 0;
    }
    int wares[kStands];
    const int n = beedle_field::tentWares(wares, kStands);
    for (int i = 0; i < n; ++i) {
        if (g_soldMask & (1u << i)) continue;
        mWare[i] = wares[i];
        const cXyz at = shopToWorld(kStand[i]);
        const csXyz angle(0, static_cast<s16>(g_yaw + 0x8000), 0);   // turned toward the aisle... roughly
        mItem[i] = fopAcM_create(fpcNm_ShopItem_e, beedle_field::wareDisplayItem(wares[i]), &at,
                                 fopAcM_GetRoomNo(this), &angle, nullptr, -1);
    }
    travellers::diagLine("shop    Beedle on his counter, %d wares out", n);
    return step;
}

int psrTentBeedle_c::CreateHeap() {
    auto* data = static_cast<J3DModelData*>(dComIfG_getObjectRes(kArc, kModel));
    auto* wait = static_cast<J3DAnmTransform*>(dComIfG_getObjectRes(kArc, kWait));
    if (data == nullptr) return 0;
    mpMorf = JKR_NEW mDoExt_McaMorfSO(data, nullptr, nullptr, wait, J3DFrameCtrl::EMode_LOOP, 1.0f, 0, -1, nullptr,
                                      0x80000, 0x11000084);
    return mpMorf != nullptr && mpMorf->getModel() != nullptr ? 1 : 0;
}

int psrTentBeedle_c::createHeapCallBack(fopAc_ac_c* a) { return static_cast<psrTentBeedle_c*>(a)->CreateHeap(); }

int psrTentBeedle_c::Delete() {
    this->~psrTentBeedle_c();
    return 1;
}

int psrTentBeedle_c::Execute() {
    if (mpMorf == nullptr) return 1;
    // his "THANK YOU" flourish, then back to talking (or sitting)
    if (mPraise > 0 && --mPraise == 0) {
        mAnim = -1;
        play(this, g_talking ? kTalks[0] : kWait, true, g_talking ? 1 : 0);
    }
    mpMorf->play(0, 0);
    mpMorf->getModel()->setBaseTRMtx(mMtx);
    mpMorf->modelCalc();
    return 1;
}

int psrTentBeedle_c::Draw() {
    if (mpMorf == nullptr) return 1;
    g_env_light.settingTevStruct(0, &current.pos, &tevStr);
    g_env_light.setLightTevColorType_MAJI(mpMorf->getModel(), &tevStr);
    mpMorf->entryDL();
    return 1;
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
    .draw_priority = fpcDwPi_OBJ_LBOX_e,
    .status = fopAcStts_UNK_0x40000_e | fopAcStts_UNK_0x4000_e | fopAcStts_CULL_e | fopAcStts_NOPAUSE_e,
    .group = fopAc_ACTOR_e,
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
    g_centre = shopCentre;
    g_yaw = shopYaw;
    const cXyz at = shopToWorld(kSeat);
    const csXyz angle(0, shopYaw, 0);
    const fpc_ProcID id = fopAcM_create(psrTentBeedle_c::sProcName, 0, &at, room, &angle, nullptr, -1);
    g_pending = id != fpcM_ERROR_PROCESS_ID_e;
}

void reset() {
    g_pending = false;
    g_tag = fpcM_ERROR_PROCESS_ID_e;
    g_talking = false;
    g_target = -1;
}

void talkTick() {
    psrTentBeedle_c* b = g_live;
    if (b == nullptr) return;
    if (g_talking) {
        // a purchase while he's being talked to: his flourish, and the ware leaves its cushion
        if (beedle_field::boughtSerial() != g_serial) {
            g_serial = beedle_field::boughtSerial();
            b->mAnim = -1;
            play(b, kPraise, false, 2);
            b->mPraise = 75;
            const int k = g_talkTarget - 1;
            if (k >= 0 && k < psrTentBeedle_c::kStands && b->mWare[k] == beedle_field::lastBought()) {
                if (b->mItem[k] != fpcM_ERROR_PROCESS_ID_e && !fpcM_IsCreating(b->mItem[k])) {
                    if (auto* a = fopAcM_SearchByID(b->mItem[k])) fopAcM_delete(a);
                }
                b->mItem[k] = fpcM_ERROR_PROCESS_ID_e;
                b->mWare[k] = -1;
                g_soldMask |= static_cast<u8>(1u << k);
            }
        }
        if (!dComIfGp_event_runCheck()) {
            g_talking = false;
            beedle_field::sayGoodbye(b);
            b->mAnim = -1;
            if (b->mPraise == 0) play(b, kWait, true, 0);
        }
        return;
    }
    auto* link = dComIfGp_getPlayer(0);
    if (link == nullptr || dComIfGp_event_runCheck()) return;
    // who the marker's on: the ware Link is at, else Beedle if he's at the counter
    int want = -1;
    f32 best = kItemReach;
    for (int i = 0; i < psrTentBeedle_c::kStands; ++i) {
        if (b->mWare[i] < 0) continue;
        const f32 d = targetPos(i + 1).absXZ(link->current.pos);
        if (d < best) { best = d; want = i + 1; }
    }
    if (want < 0 && targetPos(0).absXZ(link->current.pos) < kBeedleReach) want = 0;
    g_target = want;
    const bool gone = g_tag == fpcM_ERROR_PROCESS_ID_e || (!fpcM_IsCreating(g_tag) && fopAcM_SearchByID(g_tag) == nullptr);
    if (gone && want >= 0) {
        constexpr u32 kParams = 0x0080A000u | (6u << 5) | 3u;   // as the passers' marker (see passer_prototype)
        const csXyz angle(0xFF, 0, static_cast<s16>(0xFFFF));
        cXyz at = targetPos(want);
        g_tag = fopAcM_create(fpcNm_TAG_KMSG_e, kParams, &at, dComIfGp_roomControl_getStayNo(), &angle, nullptr, -1);
    }
}

bool tagPre(daTag_KMsg_c* tag) {
    if (g_tag == fpcM_ERROR_PROCESS_ID_e || tag == nullptr || fopAcM_GetID(tag) != g_tag) return false;
    tag->home.roomNo = static_cast<s8>(dComIfGp_roomControl_getStayNo());
    const int t = g_talking ? g_talkTarget : g_target;
    if (t >= 0) {
        const cXyz at = targetPos(t);
        tag->current.pos = at;
        tag->home.pos = at;
    }
    if (!g_talking) tag->mFlowNodeNo = beedle_field::tentHelloNode();   // never an empty line
    if (!g_talking && t >= 0 && g_live != nullptr && tag->eventInfo.checkCommandTalk()) {
        const u16 node = t == 0 ? beedle_field::tentHelloNode() : beedle_field::tentOfferNode(g_live->mWare[t - 1]);
        if (node != 0xFFFF) {
            tag->mFlowNodeNo = node;
            g_talking = true;
            g_talkTarget = t;
            g_serial = beedle_field::boughtSerial();
            g_live->mAnim = -1;
            play(g_live, kTalks[static_cast<int>(cM_rndF(4.0f)) & 3], true, 1);
        }
    }
    return true;
}

bool tagPost(daTag_KMsg_c* tag) {
    if (g_tag == fpcM_ERROR_PROCESS_ID_e || tag == nullptr || fopAcM_GetID(tag) != g_tag) return false;
    if ((g_target < 0 && !g_talking) || g_live == nullptr || daPy_py_c::checkNowWolf()) tag->attention_info.flags = 0;
    return true;
}

ModResult registerActor() {
    return svc_actor->register_actor(mod_ctx, &psrTentBeedle_c::sProfile, &psrTentBeedle_c::sProcName,
                                     &psrTentBeedle_c::sActorHandle);
}
}  // namespace psr_tent_beedle
