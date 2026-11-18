#include "global.h"

#include <algorithm>
#include <cmath>

#include "wagon_actor.hpp"

#include "SSystem/SComponent/c_math.h"
#include "d/d_bg_s_gnd_chk.h"
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_draw_priority.h"
#include "f_pc/f_pc_manager.h"
#include "m_Do/m_Do_ext.h"
#include "m_Do/m_Do_mtx.h"
#include "JSystem/J3DGraphBase/J3DSys.h"
#include "JSystem/J3DGraphAnimator/J3DModel.h"
#include "mods/service.hpp"
#include "mods/svc/log.h"

IMPORT_SERVICE(ActorService, svc_actor);

namespace {
const char* kCoachArc = "Coach";     // the disc's own: horse, harness, Telma's wagon, their collision
const char* kOwnArc = "PsrWagon";    // ours: the royal pieces and both cargos
const char* kRoyalModel[8] = {"royal_body.bmd", "royal_harness.bmd", "royal_cover.bmd",
                              "royal_f_l_wheel.bmd", "royal_f_r_wheel.bmd",
                              "royal_b_l_wheel.bmd", "royal_b_r_wheel.bmd", nullptr};
const char* kCargo[2] = {"trav_cargo.bmd", "royal_cargo.bmd"};
const char* kHorseAnim[4] = {"hu_wait_01.bck", "hu_walk_slow.bck", "hu_walk_fast.bck", "hu_run_slow.bck"};
constexpr u32 kHeapSize = 0x100000;

// The rig, as the game's coach puts it together (Coach.arc's bind pose; +z forward):
// the harness sits on the horse, the wagon on the harness's hitch (bs_loc), which is here.
constexpr f32 kHitchY = 120.0f, kHitchZ = -416.7f;
constexpr f32 kRearAxleZ = -187.0f;          // the wagon's rear axle, behind the hitch
constexpr f32 kBenchY = 43.3f, kBenchZ = 90.3f, kBenchHalf = 30.0f;   // teruma_loc: the driver's bench
constexpr f32 kFrontWheelR = 50.0f, kRearWheelR = 65.0f;
// Harness joints 2, 3 are the front wheels, the wagon's 1, 2 the rear ones (they roll about their own x).
constexpr int kHarnessFL = 2, kHarnessFR = 3, kCoachBL = 1, kCoachBR = 2;

int frontWheelCB(J3DJoint* joint, int calc) {
    if (calc != 0) return 1;
    J3DModel* model = j3dSys.getModel();
    auto* w = reinterpret_cast<psrWagon_c*>(model->getUserArea());
    const int j = joint->getJntNo();
    if (w != nullptr) {
        cMtx_copy(model->getAnmMtx(j), mDoMtx_stack_c::get());
        mDoMtx_stack_c::XrotM(w->frontWheelSpin());
        model->setAnmMtx(j, mDoMtx_stack_c::get());
        mDoMtx_copy(mDoMtx_stack_c::get(), J3DSys::mCurrentMtx);
    }
    return 1;
}

int rearWheelCB(J3DJoint* joint, int calc) {
    if (calc != 0) return 1;
    J3DModel* model = j3dSys.getModel();
    auto* w = reinterpret_cast<psrWagon_c*>(model->getUserArea());
    const int j = joint->getJntNo();
    if (w != nullptr) {
        cMtx_copy(model->getAnmMtx(j), mDoMtx_stack_c::get());
        mDoMtx_stack_c::XrotM(w->rearWheelSpin());
        model->setAnmMtx(j, mDoMtx_stack_c::get());
        mDoMtx_copy(mDoMtx_stack_c::get(), J3DSys::mCurrentMtx);
    }
    return 1;
}

f32 groundAt(const cXyz& p, f32 above = 400.0f) {
    dBgS_ObjGndChk gnd;
    cXyz probe(p.x, p.y + above, p.z);
    gnd.SetPos(&probe);
    return dComIfG_Bgsp().GroundCross(&gnd);
}

J3DModel* model(const char* arc, const char* name) {
    auto* data = static_cast<J3DModelData*>(dComIfG_getObjectRes(arc, name));
    return data != nullptr ? mDoExt_J3DModel__create(data, 0x80000, 0x11000084) : nullptr;
}
}  // namespace

psrWagon_c::~psrWagon_c() {
    if (mpBgCoach != nullptr) dComIfG_Bgsp().Release(mpBgCoach);
    if (mpBgHarness != nullptr) dComIfG_Bgsp().Release(mpBgHarness);
    dComIfG_resDelete(&mPhaseCoach, kCoachArc);
    dComIfG_resDelete(&mPhaseOwn, kOwnArc);
}

cPhs_Step psrWagon_c::create() {
    fopAcM_ct(this, psrWagon_c);
    cPhs_Step step = dComIfG_resLoad(&mPhaseCoach, kCoachArc);
    if (step != cPhs_COMPLEATE_e) return step;
    step = dComIfG_resLoad(&mPhaseOwn, kOwnArc);
    if (step != cPhs_COMPLEATE_e) return step;
    mKind = static_cast<u8>(fopAcM_GetParam(this) & 0xFF) == PSR_WAGON_ROYAL ? PSR_WAGON_ROYAL : PSR_WAGON_TRAVELLER;
    mDriven = (fopAcM_GetParam(this) & kPsrWagonDriven) != 0;
    mCoachYaw = shape_angle.y;
    current.angle.y = shape_angle.y;
    if (!fopAcM_entrySolidHeap(this, createHeapCallBack, kHeapSize)) return cPhs_ERROR_e;
    fopAcM_SetMtx(this, mpHorse->getModel()->getBaseTRMtx());
    fopAcM_setCullSizeBox(this, -500.0f, -50.0f, -1100.0f, 500.0f, 500.0f, 400.0f);
    // stand on the ground, the wagon straight out behind the horse
    if (const f32 g = groundAt(current.pos); g != -G_CM3D_F_INF) current.pos.y = g;
    placeParts();
    if (mpBgCoach != nullptr && dComIfG_Bgsp().Regist(mpBgCoach, this)) mpBgCoach = nullptr;
    if (mpBgHarness != nullptr && dComIfG_Bgsp().Regist(mpBgHarness, this)) mpBgHarness = nullptr;
    return cPhs_COMPLEATE_e;
}

int psrWagon_c::createHeapCallBack(fopAc_ac_c* actor) {
    return static_cast<psrWagon_c*>(actor)->CreateHeap();
}

int psrWagon_c::CreateHeap() {
    auto* horse = static_cast<J3DModelData*>(dComIfG_getObjectRes(kCoachArc, "horse.bmd"));
    auto* wait = static_cast<J3DAnmTransform*>(dComIfG_getObjectRes(kCoachArc, kHorseAnim[0]));
    if (horse == nullptr) return 0;
    mpHorse = JKR_NEW mDoExt_McaMorfSO(horse, nullptr, nullptr, wait, J3DFrameCtrl::EMode_LOOP, 1.0f, 0, -1, nullptr,
                                       0x80000, 0x11000084);
    if (mpHorse == nullptr || mpHorse->getModel() == nullptr) return 0;
    mAnim = 0;
    mpHarness = model(kCoachArc, "harness.bmd");
    mpCoach = model(kCoachArc, "coach.bmd");
    if (mpHarness == nullptr || mpCoach == nullptr) return 0;
    mpHarness->setUserArea(reinterpret_cast<uintptr_t>(this));
    mpCoach->setUserArea(reinterpret_cast<uintptr_t>(this));
    J3DModelData* hd = mpHarness->getModelData();
    for (u16 i = 0; i < hd->getJointNum(); i++) {
        if (i == kHarnessFL || i == kHarnessFR) hd->getJointNodePointer(i)->setCallBack(frontWheelCB);
    }
    J3DModelData* cd = mpCoach->getModelData();
    for (u16 i = 0; i < cd->getJointNum(); i++) {
        if (i == kCoachBL || i == kCoachBR) cd->getJointNodePointer(i)->setCallBack(rearWheelCB);
    }
    if (mKind == PSR_WAGON_ROYAL) {
        for (int i = 0; i < 7; i++) {
            mpRoyal[i] = model(kOwnArc, kRoyalModel[i]);
            if (mpRoyal[i] == nullptr) return 0;
        }
    }
    mpCargo = model(kOwnArc, kCargo[mKind]);
    // the wagon's and the harness's own collision, as the game's coach has it
    mpBgCoach = JKR_NEW dBgW();
    if (mpBgCoach == nullptr) return 0;
    if (mpBgCoach->Set(static_cast<cBgD_t*>(dComIfG_getObjectRes(kCoachArc, "coach.dzb")), 1, &mCoachMtx)) {
        mpBgCoach = nullptr;
    } else {
        mpBgCoach->SetCrrFunc(dBgS_MoveBGProc_Typical);
    }
    mpBgHarness = JKR_NEW dBgW();
    if (mpBgHarness == nullptr) return 0;
    if (mpBgHarness->Set(static_cast<cBgD_t*>(dComIfG_getObjectRes(kCoachArc, "harness.dzb")), 1, &mHarnessMtx)) {
        mpBgHarness = nullptr;
    } else {
        mpBgHarness->SetCrrFunc(dBgS_MoveBGProc_Typical);
    }
    return 1;
}

void psrWagon_c::driveTo(const cXyz& horse, s16 yaw, f32 speed) {
    current.pos = horse;
    shape_angle.y = current.angle.y = yaw;
    mSpeed = speed;
}

cXyz psrWagon_c::benchSeat(int seat) const {
    mDoMtx_stack_c::copy(const_cast<psrWagon_c*>(this)->mCoachMtx);
    cXyz local(seat == 0 ? -kBenchHalf : kBenchHalf, kBenchY, kBenchZ), out;
    mDoMtx_stack_c::multVec(&local, &out);
    return out;
}

// Horse at the actor; harness on it; the wagon on the hitch, trailing behind it toward its rear axle
// (so it swings round the bends after the horse, the way a wagon does).
void psrWagon_c::placeParts() {
    const f32 sy = cM_ssin(shape_angle.y), cy = cM_scos(shape_angle.y);
    const cXyz hitch(current.pos.x + sy * kHitchZ, current.pos.y + kHitchY, current.pos.z + cy * kHitchZ);
    if (!mPlaced) {
        mRear.set(hitch.x + sy * kRearAxleZ, hitch.y, hitch.z + cy * kRearAxleZ);
        mPlaced = true;
    }
    cXyz back = mRear - hitch;
    back.y = 0.0f;
    const f32 len = back.absXZ();
    if (len > 1.0f) {
        const f32 k = (-kRearAxleZ) / len;
        back.x *= k;
        back.z *= k;
        mRear.set(hitch.x + back.x, hitch.y, hitch.z + back.z);
        mCoachYaw = cM_atan2s(-back.x, -back.z);
    } else {
        mCoachYaw = shape_angle.y;
    }

    mDoMtx_stack_c::transS(current.pos);
    mDoMtx_stack_c::YrotM(shape_angle.y);
    mpHorse->getModel()->setBaseTRMtx(mDoMtx_stack_c::get());
    mpHarness->setBaseTRMtx(mDoMtx_stack_c::get());
    cMtx_copy(mDoMtx_stack_c::get(), mHarnessMtx);
    if (mKind == PSR_WAGON_ROYAL) mpRoyal[1]->setBaseTRMtx(mDoMtx_stack_c::get());

    mDoMtx_stack_c::transS(hitch);
    mDoMtx_stack_c::YrotM(mCoachYaw);
    mpCoach->setBaseTRMtx(mDoMtx_stack_c::get());
    cMtx_copy(mDoMtx_stack_c::get(), mCoachMtx);
    if (mpCargo != nullptr) mpCargo->setBaseTRMtx(mDoMtx_stack_c::get());
    if (mKind == PSR_WAGON_ROYAL) {
        mpRoyal[0]->setBaseTRMtx(mDoMtx_stack_c::get());
        mpRoyal[2]->setBaseTRMtx(mDoMtx_stack_c::get());
    }
    mpHarness->calc();
    mpCoach->calc();
    if (mKind == PSR_WAGON_ROYAL) {   // the iron wheels ride on the game's wheel joints (spin included)
        mpRoyal[3]->setBaseTRMtx(mpHarness->getAnmMtx(kHarnessFL));
        mpRoyal[4]->setBaseTRMtx(mpHarness->getAnmMtx(kHarnessFR));
        mpRoyal[5]->setBaseTRMtx(mpCoach->getAnmMtx(kCoachBL));
        mpRoyal[6]->setBaseTRMtx(mpCoach->getAnmMtx(kCoachBR));
    }
    if (mpBgCoach != nullptr) mpBgCoach->Move();
    if (mpBgHarness != nullptr) mpBgHarness->Move();
}

void psrWagon_c::setHorseAnim() {
    const f32 v = std::fabs(mSpeed);
    const int want = v < 0.5f ? 0 : (v < 7.0f ? 1 : (v < 14.0f ? 2 : 3));
    if (want != mAnim) {
        mAnim = want;
        auto* a = static_cast<J3DAnmTransform*>(dComIfG_getObjectRes(kCoachArc, kHorseAnim[want]));
        if (a != nullptr) mpHorse->setAnm(a, J3DFrameCtrl::EMode_LOOP, 8.0f, 1.0f, 0.0f, -1.0f);
    }
    // keep the hooves with the ground: the walks are played at a rate that follows the speed
    static constexpr f32 kAnimPace[4] = {1.0f, 4.0f, 9.0f, 18.0f};
    mpHorse->setPlaySpeed(want == 0 ? 1.0f : std::clamp(v / kAnimPace[want], 0.6f, 1.6f));
}

int psrWagon_c::Execute() {
    if (!mDriven) mSpeed = 0.0f;
    // the wheels roll by the distance covered
    const f32 d = mSpeed;
    mFrontSpin = static_cast<s16>(mFrontSpin + static_cast<s32>(d / kFrontWheelR * 10430.38f));
    mRearSpin = static_cast<s16>(mRearSpin + static_cast<s32>(d / kRearWheelR * 10430.38f));
    if (mDriven) {
        // the road's baked height can be off where the field's data had a hole: the ground decides
        if (const f32 g = groundAt(current.pos, 1500.0f); g != -G_CM3D_F_INF && g > current.pos.y - 2500.0f) {
            current.pos.y = g;
        }
    }
    setHorseAnim();
    placeParts();
    mpHorse->play(0, 0);
    mpHorse->modelCalc();
    eyePos = current.pos;
    eyePos.y += 200.0f;
    attention_info.position = eyePos;
    return 1;
}

int psrWagon_c::Draw() {
    g_env_light.settingTevStruct(0, &current.pos, &tevStr);
    J3DModel* list[12] = {};
    int n = 0;
    list[n++] = mpHorse->getModel();
    if (mKind == PSR_WAGON_ROYAL) {
        for (int i = 0; i < 7; i++) list[n++] = mpRoyal[i];
    } else {
        list[n++] = mpHarness;
        list[n++] = mpCoach;
    }
    if (mpCargo != nullptr) list[n++] = mpCargo;
    for (int i = 0; i < n; i++) g_env_light.setLightTevColorType_MAJI(list[i], &tevStr);
    mpHorse->entryDL();
    for (int i = 1; i < n; i++) mDoExt_modelUpdateDL(list[i]);
    return 1;
}

int psrWagon_c::Delete() {
    this->~psrWagon_c();
    return 1;
}

namespace {
cPhs_Step psrWagon_Create(void* i_this) { return static_cast<psrWagon_c*>(i_this)->create(); }
int psrWagon_Delete(void* i_this) { return static_cast<psrWagon_c*>(i_this)->Delete(); }
int psrWagon_Execute(void* i_this) { return static_cast<psrWagon_c*>(i_this)->Execute(); }
int psrWagon_Draw(void* i_this) { return static_cast<psrWagon_c*>(i_this)->Draw(); }
int psrWagon_IsDelete(void*) { return 1; }
}  // namespace

s16 psrWagon_c::sProcName = -1;
ActorHandle psrWagon_c::sActorHandle = -1;

const ActorProfileDesc psrWagon_c::sProfile = {
    .name = PSR_WAGON_NAME,
    .priority_group = 7,
    .process_size = sizeof(psrWagon_c),
    .draw_priority = fpcDwPi_OBJ_LBOX_e,
    .status = fopAcStts_UNK_0x40000_e | fopAcStts_CULL_e,
    .group = fopAc_ACTOR_e,
    .cull_type = fopAc_CULLBOX_CUSTOM_e,
    .create_function = psrWagon_Create,
    .delete_function = psrWagon_Delete,
    .execute_function = psrWagon_Execute,
    .is_delete_function = psrWagon_IsDelete,
    .draw_function = psrWagon_Draw,
};

namespace psr_wagon {
ModResult registerActor() {
    return svc_actor->register_actor(mod_ctx, &psrWagon_c::sProfile, &psrWagon_c::sProcName,
                                     &psrWagon_c::sActorHandle);
}
}  // namespace psr_wagon
