#include "global.h"

#include "tent_actor.hpp"

#include "d/d_bg_s_gnd_chk.h"
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_draw_priority.h"
#include "m_Do/m_Do_ext.h"
#include "m_Do/m_Do_mtx.h"
#include "mods/service.hpp"
#include "mods/svc/log.h"

IMPORT_SERVICE(ActorService, svc_actor);

namespace {
const char* kArcName = "PsrTent";
const char* kModel[PSR_TENT_KIND_NUM] = {"a_tent.bmd", "wall_tent.bmd", "beedle_tent.bmd"};
const char* kCollision[PSR_TENT_KIND_NUM] = {"a_tent.dzb", "wall_tent.dzb", "beedle_tent.dzb"};
const char* kGlow = "beedle_glow.bmd";
// Room for the model instance, its display lists and the collider (the tents are big meshes).
constexpr u32 kHeapSize = 0x80000;

// Beedle's tent: props from the game's own archives standing on the little stage behind the curtain,
// plus the entry lantern. Positions are in the tent's own space (doorway toward +z).
struct StageProp {
    const char* arc;
    const char* model;
    f32 x, y, z, scale;
};
constexpr StageProp kStage[] = {
    {"HTable", "ytable.bmd", -130.0f, 0.0f, -50.0f, 0.62f},
    {"HTable", "ytable.bmd", 130.0f, 0.0f, -50.0f, 0.62f},
    {"Obj_kntr", "ob_kantera.bmd", 0.0f, 250.0f, 40.0f, 1.6f},     // hanging behind the curtain
    {"Obj_kntr", "ob_kantera.bmd", 0.0f, 205.0f, 250.0f, 1.6f},    // the entry lantern
};
static_assert(sizeof(kStage) / sizeof(kStage[0]) <= psrTent_c::kStageMax);

bool isNight() {
    const int hour = dKy_getdaytime_hour();
    return hour >= 19 || hour < 6;
}
}  // namespace

psrTent_c::~psrTent_c() {
    if (mpCollider != NULL) {
        dComIfG_Bgsp().Release(mpCollider);
    }
    dComIfG_resDelete(&mPhase, kArcName);
    if ((fopAcM_GetParam(this) & 0xFF) == PSR_TENT_BEEDLE) {
        dComIfG_resDelete(&mPhaseTable, "HTable");
        dComIfG_resDelete(&mPhaseLamp, "Obj_kntr");
    }
}

cPhs_Step psrTent_c::create() {
    fopAcM_ct(this, psrTent_c);
    cPhs_Step step = dComIfG_resLoad(&mPhase, kArcName);
    if (step != cPhs_COMPLEATE_e) {
        return step;
    }
    if ((fopAcM_GetParam(this) & 0xFF) == PSR_TENT_BEEDLE) {
        // The stage props come straight from the disc archives, so they match the player's mods/HD packs.
        const cPhs_Step t = dComIfG_resLoad(&mPhaseTable, "HTable");
        const cPhs_Step l = dComIfG_resLoad(&mPhaseLamp, "Obj_kntr");
        if (t == cPhs_ERROR_e || l == cPhs_ERROR_e) {
            return cPhs_ERROR_e;
        }
        if (t != cPhs_COMPLEATE_e || l != cPhs_COMPLEATE_e) {
            return t != cPhs_COMPLEATE_e ? t : l;
        }
    }
    mpModel = NULL;
    mpGlow = NULL;
    mpCollider = NULL;
    mGrounded = false;
    mStageNum = 0;
    mKind = static_cast<u8>(fopAcM_GetParam(this) & 0xFF);
    if (mKind >= PSR_TENT_KIND_NUM) {
        mKind = PSR_TENT_A;
    }
    // Sit on the ground before the tent's own collision exists, so the probe can't hit the roof.
    snapToGround();
    if (!fopAcM_entrySolidHeap(this, createHeapCallBack, kHeapSize)) {
        return cPhs_ERROR_e;
    }
    if (mpCollider != NULL && dComIfG_Bgsp().Regist(mpCollider, this) == true) {
        return cPhs_ERROR_e;
    }
    fopAcM_SetMtx(this, mpModel->getBaseTRMtx());
    // Generous cull box: Beedle's banner pole stands about 5 m tall, guy ropes reach 3 m out.
    fopAcM_setCullSizeBox(this, -520.0f, -20.0f, -520.0f, 520.0f, 560.0f, 520.0f);
    Execute();
    return step;
}

void psrTent_c::snapToGround() {
    dBgS_ObjGndChk gnd;
    cXyz probe = current.pos;
    probe.y += 400.0f;
    gnd.SetPos(&probe);
    const f32 ground = dComIfG_Bgsp().GroundCross(&gnd);
    if (ground != -G_CM3D_F_INF) {
        current.pos.y = ground;
        home.pos.y = ground;
        mGrounded = true;
        tevStr.room_no = dComIfG_Bgsp().GetRoomId(gnd);
        tevStr.YukaCol = dComIfG_Bgsp().GetPolyColor(gnd);
    }
}

int psrTent_c::CreateHeap() {
    auto* data = static_cast<J3DModelData*>(dComIfG_getObjectRes(kArcName, kModel[mKind]));
    if (data == NULL) {
        return 0;
    }
    mpModel = mDoExt_J3DModel__create(data, 0x80000, 0x11000084);
    if (mpModel == NULL) {
        return 0;
    }
    mpGlow = NULL;
    mStageNum = 0;
    if (mKind == PSR_TENT_BEEDLE) {
        for (const StageProp& prop : kStage) {
            auto* d = static_cast<J3DModelData*>(dComIfG_getObjectRes(prop.arc, prop.model));
            if (d == NULL) {
                continue;
            }
            J3DModel* m = mDoExt_J3DModel__create(d, 0x80000, 0x11000084);
            if (m != NULL) {
                mpStage[mStageNum++] = m;
            }
        }
        auto* glow = static_cast<J3DModelData*>(dComIfG_getObjectRes(kArcName, kGlow));
        if (glow != NULL) {
            mpGlow = mDoExt_J3DModel__create(glow, 0x80000, 0x11000084);
        }
    }
    mpCollider = JKR_NEW dBgW();
    if (mpCollider == NULL) {
        return 0;
    }
    auto* dzb = static_cast<cBgD_t*>(dComIfG_getObjectRes(kArcName, kCollision[mKind]));
    if (dzb == NULL || mpCollider->Set(dzb, 1, &mColliderMtx) == true) {
        return 0;
    }
    mpCollider->SetCrrFunc(dBgS_MoveBGProc_Typical);
    return 1;
}

int psrTent_c::createHeapCallBack(fopAc_ac_c* actor) {
    return static_cast<psrTent_c*>(actor)->CreateHeap();
}

int psrTent_c::Delete() {
    this->~psrTent_c();
    return 1;
}

int psrTent_c::Execute() {
    if (!mGrounded) {
        snapToGround();   // the room's ground may load after the tent
    }
    mDoMtx_stack_c::transS(current.pos.x, current.pos.y, current.pos.z);
    mDoMtx_stack_c::YrotM(shape_angle.y);
    mpModel->setBaseTRMtx(mDoMtx_stack_c::get());
    if (mpGlow != NULL) {
        mpGlow->setBaseTRMtx(mDoMtx_stack_c::get());
    }
    for (int i = 0; i < mStageNum; i++) {
        const StageProp& prop = kStage[i];
        mDoMtx_stack_c::transS(current.pos.x, current.pos.y, current.pos.z);
        mDoMtx_stack_c::YrotM(shape_angle.y);
        mDoMtx_stack_c::transM(prop.x, prop.y, prop.z);
        mDoMtx_stack_c::scaleM(prop.scale, prop.scale, prop.scale);
        mpStage[i]->setBaseTRMtx(mDoMtx_stack_c::get());
    }
    if (mpCollider != NULL) {
        PSMTXCopy(mpModel->getBaseTRMtx(), mColliderMtx);
        mpCollider->Move();
    }
    eyePos = attention_info.position = current.pos;
    attention_info.flags = 0;
    return 1;
}

int psrTent_c::Draw() {
    g_env_light.settingTevStruct(0x20, &current.pos, &tevStr);
    g_env_light.setLightTevColorType_MAJI(mpModel, &tevStr);
    dComIfGd_setListBG();
    mDoExt_modelUpdateDL(mpModel);
    dComIfGd_setList();
    for (int i = 0; i < mStageNum; i++) {
        g_env_light.setLightTevColorType_MAJI(mpStage[i], &tevStr);
        mDoExt_modelUpdateDL(mpStage[i]);
    }
    if (mpGlow != NULL && isNight()) {
        // Lit from inside: drawn without the scene's lighting so it stays bright in the dark.
        mDoExt_modelUpdateDL(mpGlow);
    }
    return 1;
}

namespace {
cPhs_Step psrTent_Create(void* i_this) { return static_cast<psrTent_c*>(i_this)->create(); }
int psrTent_Delete(void* i_this) { return static_cast<psrTent_c*>(i_this)->Delete(); }
int psrTent_Execute(void* i_this) { return static_cast<psrTent_c*>(i_this)->Execute(); }
int psrTent_Draw(void* i_this) { return static_cast<psrTent_c*>(i_this)->Draw(); }
int psrTent_IsDelete(void*) { return 1; }
}  // namespace

s16 psrTent_c::sProcName = -1;
ActorHandle psrTent_c::sActorHandle = -1;
const ActorProfileDesc psrTent_c::sProfile = {
    .name = PSR_TENT_NAME,
    .priority_group = 7,
    .process_size = sizeof(psrTent_c),
    .draw_priority = fpcDwPi_OBJ_LBOX_e,
    .status = fopAcStts_UNK_0x40000_e | fopAcStts_UNK_0x4000_e | fopAcStts_CULL_e,
    .group = fopAc_ACTOR_e,
    .cull_type = fopAc_CULLBOX_CUSTOM_e,
    .create_function = psrTent_Create,
    .delete_function = psrTent_Delete,
    .execute_function = psrTent_Execute,
    .is_delete_function = psrTent_IsDelete,
    .draw_function = psrTent_Draw,
};

namespace psr_tent {
ModResult registerActor() {
    return svc_actor->register_actor(mod_ctx, &psrTent_c::sProfile, &psrTent_c::sProcName,
                                     &psrTent_c::sActorHandle);
}
}  // namespace psr_tent
