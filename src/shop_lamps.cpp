#include "global.h"
#include "mod_lights.hpp"

#include "shop_lamps.hpp"
#include "travellers.hpp"

#include "SSystem/SComponent/c_math.h"
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "d/d_particle_name.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_draw_priority.h"
#include "f_pc/f_pc_manager.h"
#include "m_Do/m_Do_ext.h"
#include "m_Do/m_Do_mtx.h"
#include "mods/service.hpp"

namespace {
constexpr const char* kArc = "PsrPack";             // the game's lantern (Obj_kntr ob_kantera), our own copy: its
constexpr const char* kModel = "psr_shop_kntr.bmd";  // materials renamed al_kt_* -> ps_kt_*, so Live Reflections leaves it be
// In the shop's own frame (tools/stage + tools/tents/interior2.py): hung on the ridge rope over the
// middle of the floor, the lantern's base 330 up, at these points front to back, 1.6 times its size.
constexpr f32 kBaseY = 330.0f;
constexpr f32 kZ[psrShopLamps_c::kNum] = {-330.0f, 0.0f, 330.0f};
constexpr f32 kScale = 1.6f;
constexpr f32 kFlameY = 12.0f * kScale;             // on the wick: the glass runs 9.5..27 up the lantern
constexpr f32 kFlameScale = kScale;  // as big as the lantern is scaled (2.2 rose out of the lid)
constexpr u32 kHeapSize = 0x80000000;               // auto-sized

bool g_pending = false;                             // created, not yet running
psrShopLamps_c* g_live = nullptr;

bool lightIsOurs(LIGHT_INFLUENCE& l) {
    const int i = (l.mIndex < 0 ? -l.mIndex : l.mIndex) & 0xFFF;
    return i >= 1 && i <= 50 && g_env_light.pointlight[i - 1] == &l;
}
}  // namespace

psrShopLamps_c::~psrShopLamps_c() {
    lightsOut();
    if (g_live == this) g_live = nullptr;
    dComIfG_resDelete(&mPhase, kArc);
}

cPhs_Step psrShopLamps_c::create() {
    fopAcM_ct(this, psrShopLamps_c);
    const cPhs_Step step = dComIfG_resLoad(&mPhase, kArc);
    if (step != cPhs_COMPLEATE_e) {
        return step;
    }
    for (int i = 0; i < kNum; ++i) {
        mpLamp[i] = nullptr;
        mFlame[i] = 0;
        mGlow[i] = LIGHT_INFLUENCE{};
    }
    mLit = false;
    if (!fopAcM_entrySolidHeap(this, createHeapCallBack, kHeapSize)) {
        g_pending = false;
        return cPhs_ERROR_e;
    }
    // The game culls an actor by its box placed with the actor's own matrix; without one set it never
    // draws (the first build of this left it out, and the lanterns didn't show).
    mDoMtx_stack_c::transS(current.pos.x, current.pos.y, current.pos.z);
    mDoMtx_stack_c::YrotM(shape_angle.y);
    cMtx_copy(mDoMtx_stack_c::get(), mMtx);
    fopAcM_SetMtx(this, mMtx);
    fopAcM_setCullSizeBox(this, -120.0f, kBaseY - 40.0f, -460.0f, 120.0f, kBaseY + 120.0f, 460.0f);
    attention_info.flags = 0;
    g_live = this;
    g_pending = false;
    travellers::diagLine("shop    lanterns up (%d models)", mpLamp[0] != nullptr ? kNum : 0);
    Execute();
    return step;
}

int psrShopLamps_c::CreateHeap() {
    auto* data = static_cast<J3DModelData*>(dComIfG_getObjectRes(kArc, kModel));
    if (data == nullptr) {
        return 0;
    }
    for (int i = 0; i < kNum; ++i) {
        mpLamp[i] = mDoExt_J3DModel__create(data, 0x80000, 0x11000084);
        if (mpLamp[i] == nullptr) {
            return 0;
        }
    }
    return 1;
}

int psrShopLamps_c::createHeapCallBack(fopAc_ac_c* a) {
    return static_cast<psrShopLamps_c*>(a)->CreateHeap();
}

int psrShopLamps_c::Delete() {
    this->~psrShopLamps_c();
    return 1;
}

void psrShopLamps_c::lightsOut() {
    for (int i = 0; i < kNum; ++i) {
        if (lightIsOurs(mGlow[i])) dKy_plight_cut(&mGlow[i]);
        mGlow[i].mIndex = 0;
        mFlame[i] = 0;   // a flame not set again this frame goes out by itself (the game's level emitters)
    }
    mLit = false;
}

int psrShopLamps_c::Execute() {
    const bool night = dKy_daynight_check();
    if (night != mLit) {
        travellers::diagLine("shop    lanterns %s", night ? "lit" : "out");
    }
    if (!night) {
        if (mLit) lightsOut();
        mLit = false;
    }
    for (int i = 0; i < kNum; ++i) {
        mDoMtx_stack_c::transS(current.pos.x, current.pos.y, current.pos.z);
        mDoMtx_stack_c::YrotM(shape_angle.y);
        mDoMtx_stack_c::transM(0.0f, kBaseY, kZ[i]);
        cXyz flame;
        const cXyz up(0.0f, kFlameY, 0.0f);
        mDoMtx_stack_c::multVec(&up, &flame);
        mDoMtx_stack_c::scaleM(kScale, kScale, kScale);
        if (mpLamp[i] != nullptr) mpLamp[i]->setBaseTRMtx(mDoMtx_stack_c::get());
        if (!night) continue;
        // Link's lantern fire, as the travellers' lanterns burn (the colour of the room's light once
        // this actor's light settings are made, which happens at its first draw)
        const cXyz scale(kFlameScale, kFlameScale, kFlameScale);
        const dKy_tevstr_c* tev = tevStr.Material_use_fg == 124 ? &tevStr : nullptr;
        mFlame[i] = dComIfGp_particle_set(mFlame[i], ID_ZI_J_KANTERA_FIRE, &flame, tev, nullptr, &scale, 0xFF,
                                          nullptr, -1, nullptr, nullptr, nullptr);
        // the warm light the game's lamp flame (fireWd2) gives
        mGlow[i].mPosition.set(flame.x, flame.y + 10.0f, flame.z);
        mGlow[i].mColor.r = 0xBC;
        mGlow[i].mColor.g = 0x66;
        mGlow[i].mColor.b = 0x42;
        mGlow[i].mPow = 500.0f;
        mGlow[i].mFluctuation = 1.0f;
        dKy_plight_set(&mGlow[i]);   // never adds one twice; the scene's lighting may have cleared it
        mod_lights::offer(mGlow[i].mPosition, 110.0f);   // the canvas, floor and shelves round it
    }
    mLit = night;
    return 1;
}

int psrShopLamps_c::Draw() {
    g_env_light.settingTevStruct(0x20, &current.pos, &tevStr);
    for (J3DModel* m : mpLamp) {
        if (m == nullptr) continue;
        g_env_light.setLightTevColorType_MAJI(m, &tevStr);
        mDoExt_modelUpdateDL(m);
    }
    return 1;
}

namespace {
cPhs_Step psrShopLamps_Create(void* p) { return static_cast<psrShopLamps_c*>(p)->create(); }
int psrShopLamps_Delete(void* p) { return static_cast<psrShopLamps_c*>(p)->Delete(); }
int psrShopLamps_Execute(void* p) { return static_cast<psrShopLamps_c*>(p)->Execute(); }
int psrShopLamps_Draw(void* p) { return static_cast<psrShopLamps_c*>(p)->Draw(); }
int psrShopLamps_IsDelete(void*) { return 1; }
}  // namespace

s16 psrShopLamps_c::sProcName = -1;
ActorHandle psrShopLamps_c::sActorHandle = -1;
const ActorProfileDesc psrShopLamps_c::sProfile = {
    .name = PSR_SHOP_LAMPS_NAME,
    .priority_group = 7,
    .process_size = sizeof(psrShopLamps_c),
    .draw_priority = fpcDwPi_OBJ_LBOX_e,
    .status = fopAcStts_UNK_0x40000_e | fopAcStts_UNK_0x4000_e | fopAcStts_CULL_e,
    .group = fopAc_ACTOR_e,
    .cull_type = fopAc_CULLBOX_CUSTOM_e,
    .create_function = psrShopLamps_Create,
    .delete_function = psrShopLamps_Delete,
    .execute_function = psrShopLamps_Execute,
    .is_delete_function = psrShopLamps_IsDelete,
    .draw_function = psrShopLamps_Draw,
};

namespace psr_shop_lamps {
bool present() { return g_live != nullptr || g_pending; }

void spawn(const cXyz& centre, s16 yaw, int room) {
    if (psrShopLamps_c::sProcName < 0 || present()) return;
    const csXyz angle(0, yaw, 0);
    const fpc_ProcID id = fopAcM_create(psrShopLamps_c::sProcName, 0, &centre, room, &angle, nullptr, -1);
    g_pending = id != fpcM_ERROR_PROCESS_ID_e;
    travellers::diagLine("shop    lanterns %s", g_pending ? "on their way" : "could not be made");
}

void reset() { g_pending = false; }

ModResult registerActor() {
    return svc_actor->register_actor(mod_ctx, &psrShopLamps_c::sProfile, &psrShopLamps_c::sProcName,
                                     &psrShopLamps_c::sActorHandle);
}
}  // namespace psr_shop_lamps
