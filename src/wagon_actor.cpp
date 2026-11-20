#include "psr_heap.hpp"
#include "global.h"
#include "mod_lights.hpp"

#include <algorithm>
#include <cmath>

#include "wagon_actor.hpp"

#include "SSystem/SComponent/c_math.h"
#include "d/d_bg_s_gnd_chk.h"
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "d/d_particle_name.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_draw_priority.h"
#include "f_pc/f_pc_manager.h"
#include "m_Do/m_Do_ext.h"
#include "m_Do/m_Do_mtx.h"
#include "JSystem/J3DGraphBase/J3DSys.h"
#include "JSystem/J3DGraphAnimator/J3DModel.h"
#include "JSystem/JParticle/JPAEmitter.h"
#include "traveller_gear.hpp"
#include "m_Do/m_Do_audio.h"
#include "mods/service.hpp"
#include "mods/svc/log.h"


namespace {
const char* kCoachArc = "Coach";     // the disc's own: horse, harness, Telma's wagon, their collision
const char* kOwnArc = "PsrWagon";    // ours: the royal pieces and both cargos
const char* kRoyalModel[8] = {"royal_body.bmd", "royal_harness.bmd", "royal_cover.bmd",
                              "royal_f_l_wheel.bmd", "royal_f_r_wheel.bmd",
                              "royal_b_l_wheel.bmd", "royal_b_r_wheel.bmd", nullptr};
const char* kCargo[2] = {"trav_cargo.bmd", "royal_cargo.bmd"};
const char* kBurntRoyal[8] = {"royal_body_burnt.bmd", "royal_harness_burnt.bmd", "royal_cover_burnt.bmd",
                              "royal_f_l_wheel_burnt.bmd", "royal_f_r_wheel_burnt.bmd",
                              "royal_b_l_wheel_burnt.bmd", "royal_b_r_wheel_burnt.bmd", "royal_cargo_burnt.bmd"};
const char* kBurntTrav[8] = {"trav_coach_burnt.bmd", "trav_harness_burnt.bmd", "trav_cover_burnt.bmd",
                             nullptr, nullptr, nullptr, nullptr, "trav_cargo_burnt.bmd"};
// the lanterns: our copy of the game's lantern (materials renamed, see shop_lamps.cpp), 1.6 times its
// size (as the shop's), each hanging by its handle from the end of the side wall's top beam where it
// sticks out at the front corner (the same on both wagons: x to 120, its underside at y 151, z 90..108;
// the handle's loop over it)
constexpr const char* kLampModel = "psr_wgn_kntr.bmd";
constexpr f32 kLampScale = 1.6f;
constexpr f32 kLampX = 113.0f, kLampHandleY = 155.0f, kLampZ = 99.0f;
constexpr f32 kLampHandle = 44.2f;   // the top of its handle above its base
constexpr f32 kLampWick = 12.0f;     // the flame above its base (the glass runs 9.5..27)
constexpr int kBlazeFrames = 30 * 10;   // destroyed: it burns at full blaze this long, then smoulders
constexpr int kCharAfter = 45;          // ...and turns to the burnt-out wreck this far in (the fire at its height)
const char* kHorseAnim[4] = {"hu_wait_01.bck", "hu_walk_slow.bck", "hu_walk_fast.bck", "hu_run_slow.bck"};
constexpr u32 kHeapSize = 0x200000;   // horse, harness, wagon, the royal pieces, the cargo and the burnt wreck

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

// The fire Telma's coach burns with (its own effects): the more damage, the more of them.
constexpr u16 kFireFx[10] = {0x85F0, 0x85EF, 0x85E7, 0x85E8, 0x85E9, 0x85EA, 0x85EB, 0x85EC, 0x85ED, 0x85EE};
constexpr int kCoachBody = 4;            // the wagon model's body joint (the fire follows it)
constexpr int kBurnEvery = 6;            // a burning wagon takes 1 damage this often (about 50 s to burn out)
constexpr int kSmokeFrames = 30 * 40;    // the wreck smoulders this long

// A Bulblin's arrow in the wagon's wood sets it alight (ours, the bowman's, never do).
void arrowStick(dBgW*, fopAc_ac_c* self, fopAc_ac_c* arrow, cXyz&) {
    if (arrow == nullptr || fopAcM_GetName(arrow) != fpcNm_E_ARROW_e) return;
    if (traveller_gear::isOurArrow(arrow)) return;
    static_cast<psrWagon_c*>(self)->ignite();
    static_cast<psrWagon_c*>(self)->addDamage(8);
    fopAcM_delete(arrow);
}

f32 groundAt(const cXyz& p, f32 above = 400.0f) {
    dBgS_ObjGndChk gnd;
    cXyz probe(p.x, p.y + above, p.z);
    gnd.SetPos(&probe);
    return dComIfG_Bgsp().GroundCross(&gnd);
}

J3DModel* loadModel(const char* arc, const char* name) {
    auto* data = static_cast<J3DModelData*>(dComIfG_getObjectRes(arc, name));
    return data != nullptr ? mDoExt_J3DModel__create(data, 0x80000, 0x11000084) : nullptr;
}
}  // namespace

psrWagon_c::~psrWagon_c() {
    cutFireLight();
    cutLampLight();
    for (JPABaseEmitter*& e : mFx) {
        if (e != nullptr) { e->becomeInvalidEmitter(); e = nullptr; }
    }
    if (mpBgCoach != nullptr) dComIfG_Bgsp().Release(mpBgCoach);
    if (mpBgHarness != nullptr) dComIfG_Bgsp().Release(mpBgHarness);
    dComIfG_resDelete(&mPhaseCoach, kCoachArc);
    dComIfG_resDelete(&mPhaseOwn, kOwnArc);
}

namespace {
const dCcD_SrcCyl kBodySrc = {
    {
        {0x0, {{0x0, 0x0, 0x0}, {0x0, 0x0}, 0x0}},
        {dCcD_SE_NONE, 0x0, 0x0, 0x0, 0x0},
        {dCcD_SE_WOOD, 0x0, 0x0, 0x0, 0x0},
        {0x0},
    },
    {{{0.0f, 0.0f, 0.0f}, 110.0f, 230.0f}},
};
constexpr int kHitDamage = 12;   // a monster's blow on the wagon (it takes 255)
}  // namespace

// A small knock: the game's "hit something hard" sparks (not the big hit star) and a crack of wood.
void psrWagon_c::hitFx(const cXyz& at) {
    dComIfGp_particle_set(0x117, &at, nullptr, nullptr);   // ID_ZI_J_HITNG_A
    dComIfGp_particle_set(0x118, &at, nullptr, nullptr);   // ID_ZI_J_HITNG_B
    mDoAud_seStart(Z2SE_HIT_WOOD_WEAPON, &at, 0, dComIfGp_getReverb(fopAcM_GetRoomNo(this)));
}

void psrWagon_c::hitTick() {
    if (mWrecked) return;
    if (mHitCool > 0) --mHitCool;
    // three boxes along the wagon (its own frame: origin at the hitch, the body runs back to -460)
    static const f32 kAlong[3] = {-80.0f, -250.0f, -420.0f};
    for (int i = 0; i < 3; i++) {
        dCcD_Cyl& c = mBody[i];
        if (c.ChkTgHit()) {
            fopAc_ac_c* by = c.GetTgHitAc();
            cXyz at = *c.GetTgHitPosP();
            c.ClrTgHit();
            if (by != nullptr && fopAcM_GetGroup(by) == fopAc_ENEMY_e && mHitCool == 0) {
                hitFx(at);
                addDamage(kHitDamage);
                mHitCool = 8;   // one swing, one knock
            }
        }
        mDoMtx_stack_c::copy(mCoachMtx);
        cXyz local(0.0f, 0.0f, kAlong[i]), w;
        mDoMtx_stack_c::multVec(&local, &w);
        c.SetC(w);
        dComIfG_Ccsp()->Set(&c);
    }
}

cPhs_Step psrWagon_c::create() {
    fopAcM_ct(this, psrWagon_c);
    cPhs_Step step = dComIfG_resLoad(&mPhaseCoach, kCoachArc, psrHeap());
    if (step != cPhs_COMPLEATE_e) return step;
    step = dComIfG_resLoad(&mPhaseOwn, kOwnArc, psrHeap());
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
    // the hit boxes: the same target groups as the travellers' (so the monsters' blows land), never
    // pushing anything about
    mStts.Init(0xFF, 0xFF, this);
    for (dCcD_Cyl& c : mBody) {
        c.Set(kBodySrc);
        c.SetStts(&mStts);
        c.SetTgType(0xD8FBFDFF);
        c.SetTgSPrm(0x5);
        c.SetTgSe(dCcD_SE_WOOD);
    }
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
    mpHarness = loadModel(kCoachArc, "harness.bmd");
    mpCoach = loadModel(kCoachArc, "coach.bmd");
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
            mpRoyal[i] = loadModel(kOwnArc, kRoyalModel[i]);
            if (mpRoyal[i] == nullptr) return 0;
        }
    }
    mpCargo = loadModel(kOwnArc, kCargo[mKind]);
    for (J3DModel*& l : mpLamp) l = loadModel(kOwnArc, kLampModel);   // (missing: no lanterns, nothing else)
    for (int i = 0; i < 8; i++) {
        const char* f = (mKind == PSR_WAGON_ROYAL ? kBurntRoyal : kBurntTrav)[i];
        mpBurnt[i] = f != nullptr ? loadModel(kOwnArc, f) : nullptr;   // (missing: it just keeps its look)
    }
    // the wagon's and the harness's own collision, as the game's coach has it
    mpBgCoach = JKR_NEW dBgW();
    if (mpBgCoach == nullptr) return 0;
    if (mpBgCoach->Set(static_cast<cBgD_t*>(dComIfG_getObjectRes(kCoachArc, "coach.dzb")), 1, &mCoachMtx)) {
        mpBgCoach = nullptr;
    } else {
        mpBgCoach->SetCrrFunc(dBgS_MoveBGProc_Typical);
        mpBgCoach->SetArrowStickCallback(arrowStick);
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
    mOnRoad = false;
}

f32 psrWagon_c::hitchBack() { return -kHitchZ; }
f32 psrWagon_c::rearBack() { return -kRearAxleZ; }

void psrWagon_c::driveOnRoad(const cXyz& horse, s16 yaw, const cXyz& hitch, const cXyz& rear, f32 speed) {
    current.pos = horse;
    shape_angle.y = current.angle.y = yaw;
    mSpeed = speed;
    // the shafts run from the hitch to the horse; the wagon from the rear axle to the hitch
    mRoadHarnessYaw = cM_atan2s(horse.x - hitch.x, horse.z - hitch.z);
    mRear.set(rear.x, hitch.y + kHitchY, rear.z);
    mOnRoad = true;
}

cXyz psrWagon_c::middle() const {
    mDoMtx_stack_c::copy(const_cast<psrWagon_c*>(this)->mCoachMtx);
    cXyz local(0.0f, 60.0f, -170.0f), out;
    mDoMtx_stack_c::multVec(&local, &out);
    return out;
}

void psrWagon_c::addDamage(int amount) {
    if (mWrecked) return;
    mDamage = std::min(kMaxDamage, mDamage + amount);
    if (mDamage >= kMaxDamage) {
        mWrecked = true;
        mLoose = true;
        // what the camera checks it against was the horse (its matrix): the horse bolts off down the road
        // and the wreck stays, so the wreck blinked in and out with the way the camera faced
        // (while the horse is still in sight, running off, nothing is culled; Execute moves the box to
        // the wreck once it's gone)
        fopAcM_OffStatus(this, fopAcStts_CULL_e);
        mSmoke = kSmokeFrames;
        mBlaze = kBlazeFrames;   // however it was wrecked (fire, or monsters smashing it): it goes up in flames
        // the moment it goes: a burst of smoke and splinters (a bomb's), as the fire takes it
        const cXyz at = middle();
        dComIfGp_particle_set(0x95, &at, nullptr, nullptr);   // ID_IT_JT_BMEX_SMOKE01
        dComIfGp_particle_set(0x96, &at, nullptr, nullptr);   // ID_IT_JT_BMEX_SMOKE02
    }
}

void psrWagon_c::ignite() {
    if (!mWrecked) mBurning = true;
}

// Telma's coach's fire, by how far gone the wagon is; once it's a wreck it dies down to smoke.
void psrWagon_c::updateFire() {
    int want = 0;   // how many of the effects burn
    if (mWrecked && mBlaze > 0) {
        --mBlaze;
        want = 10;
        if (kBlazeFrames - mBlaze >= kCharAfter) mCharred = true;
    } else if (mWrecked) {
        mCharred = true;
        if (mSmoke > 0) --mSmoke;
        want = mSmoke > kSmokeFrames / 2 ? 6 : (mSmoke > 0 ? 3 : 0);
    } else if (mBurning) {
        want = 4 + mDamage * 6 / kMaxDamage;
    }
    // The game's own fire (the camp fires': glow, flames, sparks), in spots over the wagon in its own
    // frame (origin at the hitch, +z forward; the body runs back from the hitch): the more of it is
    // burning, the more spots. Telma's coach fire (kFireFx) only exists in her stage's effects.
    static const cXyz kSpots[8] = {
        cXyz(0.0f, 150.0f, -260.0f), cXyz(-55.0f, 120.0f, -160.0f), cXyz(55.0f, 120.0f, -360.0f),
        cXyz(0.0f, 170.0f, -420.0f), cXyz(-60.0f, 100.0f, -330.0f), cXyz(60.0f, 110.0f, -200.0f),
        cXyz(0.0f, 90.0f, -120.0f),  cXyz(-40.0f, 80.0f, -460.0f),
    };
    static const u16 kIds[3] = {0x3AD, 0x3AF, 0x3AE};   // ID_ZF_J_FIRE00_GLOW, FIRE02_FIRE, FIRE01_SPARK
    const int spots = want <= 0 ? 0 : std::min(8, 1 + want * 8 / 10);
    for (int i = 0; i < 8; i++) {
        if (i >= spots) {
            for (u32& id : mFlames[i]) id = 0;   // not set again this frame: it goes out by itself
            continue;
        }
        mDoMtx_stack_c::copy(mCoachMtx);
        cXyz at;
        mDoMtx_stack_c::multVec(&kSpots[i], &at);
        const bool smoulder = mWrecked && mBlaze <= 0;
        const f32 k = smoulder ? 0.6f : 1.4f;
        const cXyz scale(k, k, k);
        for (int j = 0; j < 3; j++) {
            if (smoulder && j == 2) { mFlames[i][j] = 0; continue; }   // no sparks off the embers
            mFlames[i][j] = dComIfGp_particle_set(mFlames[i][j], kIds[j], &at, nullptr, nullptr, &scale, 0xFF,
                                                  nullptr, -1, nullptr, nullptr, nullptr);
        }
    }
    // its light: on the people round it, and (the nearest fire to the camera) on the ground
    if (spots > 0) {
        mDoMtx_stack_c::copy(mCoachMtx);
        mDoMtx_stack_c::multVec(&kSpots[0], &mFireLight.mPosition);
        mFireLight.mColor.r = 0xBC;
        mFireLight.mColor.g = 0x66;
        mFireLight.mColor.b = 0x42;
        mFireLight.mPow = 300.0f + 60.0f * spots;
        mFireLight.mFluctuation = 1.0f;
        if (!mFireLit) { dKy_plight_set(&mFireLight); mFireLit = true; }
        mod_lights::offer(mFireLight.mPosition, 60.0f + 15.0f * spots);
    } else if (mFireLit) {
        cutFireLight();
    }
}

void psrWagon_c::lampsTick() {
    const bool lit = dKy_daynight_check() && !mWrecked;
    for (int i = 0; i < 2; i++) {
        mDoMtx_stack_c::copy(mCoachMtx);
        mDoMtx_stack_c::transM(i == 0 ? -kLampX : kLampX, kLampHandleY - kLampHandle * kLampScale, kLampZ);
        cXyz flame;
        const cXyz wick(0.0f, kLampWick * kLampScale, 0.0f);
        mDoMtx_stack_c::multVec(&wick, &flame);
        mDoMtx_stack_c::scaleM(kLampScale, kLampScale, kLampScale);
        if (mpLamp[i] != nullptr) mpLamp[i]->setBaseTRMtx(mDoMtx_stack_c::get());
        if (!lit || mpLamp[i] == nullptr) { mLampFlame[i] = 0; continue; }   // (a level emitter not set again goes out)
        const cXyz scale(kLampScale, kLampScale, kLampScale);
        mLampFlame[i] = dComIfGp_particle_set(mLampFlame[i], ID_ZI_J_KANTERA_FIRE, &flame, nullptr, nullptr, &scale,
                                              0xFF, nullptr, -1, nullptr, nullptr, nullptr);
        if (i == 1) {   // one warm light between the two (on the drivers, the horse and the road)
            mDoMtx_stack_c::copy(mCoachMtx);
            const cXyz mid(0.0f, kLampHandleY - kLampHandle * kLampScale + kLampWick * kLampScale + 10.0f, kLampZ);
            mDoMtx_stack_c::multVec(&mid, &mLampLight.mPosition);
            mLampLight.mColor.r = 0xBC;
            mLampLight.mColor.g = 0x66;
            mLampLight.mColor.b = 0x42;
            mLampLight.mPow = 500.0f;
            mLampLight.mFluctuation = 1.0f;
            if (!mLampLit) { dKy_plight_set(&mLampLight); mLampLit = true; }
            mod_lights::offer(mLampLight.mPosition, 90.0f);
        }
    }
    if (!lit && mLampLit) cutLampLight();
}

void psrWagon_c::cutLampLight() {
    const int i = (mLampLight.mIndex < 0 ? -mLampLight.mIndex : mLampLight.mIndex) & 0xFFF;
    if (mLampLit && i >= 1 && i <= 50 && g_env_light.pointlight[i - 1] == &mLampLight) dKy_plight_cut(&mLampLight);
    mLampLit = false;
}

void psrWagon_c::cutFireLight() {
    const int i = (mFireLight.mIndex < 0 ? -mFireLight.mIndex : mFireLight.mIndex) & 0xFFF;
    if (mFireLit && i >= 1 && i <= 50 && g_env_light.pointlight[i - 1] == &mFireLight) dKy_plight_cut(&mFireLight);
    mFireLit = false;
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
    if (!mLoose) {
        mHarnessPos = current.pos;
        mHarnessYaw = mOnRoad ? mRoadHarnessYaw : shape_angle.y;
    }
    const f32 sy = cM_ssin(mHarnessYaw), cy = cM_scos(mHarnessYaw);
    cXyz hitch(mHarnessPos.x + sy * kHitchZ, mHarnessPos.y + kHitchY, mHarnessPos.z + cy * kHitchZ);
    // On the ground: the hitch at its own height over the ground under it (not the horse's), and the
    // wagon pitched to the ground under its rear axle, so it rides the slopes instead of floating
    // over them or ploughing into them. (Probed from just over the ground, under the wagon's own floor.)
    s16 harnessPitch = 0, coachPitch = 0;
    if (mDriven && !mWrecked) {
        cXyz hp = hitch;
        hp.y = mHarnessPos.y;
        const f32 gH = groundAt(hp, 70.0f);
        if (gH != -G_CM3D_F_INF && std::fabs(gH - mHarnessPos.y) < 150.0f) {
            hitch.y = gH + kHitchY;
            harnessPitch = cM_atan2s(gH - mHarnessPos.y, -kHitchZ);
        }
        cXyz rp = mPlaced ? mRear : hitch;
        rp.y = hitch.y - kHitchY;
        const f32 gR = groundAt(rp, 70.0f);
        if (gR != -G_CM3D_F_INF && std::fabs(gR - (hitch.y - kHitchY)) < 150.0f) {
            coachPitch = cM_atan2s(gR - (hitch.y - kHitchY), -kRearAxleZ);
        }
    }
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
    mDoMtx_stack_c::transS(mHarnessPos);
    mDoMtx_stack_c::YrotM(mHarnessYaw);
    mDoMtx_stack_c::XrotM(harnessPitch);
    mpHarness->setBaseTRMtx(mDoMtx_stack_c::get());
    cMtx_copy(mDoMtx_stack_c::get(), mHarnessMtx);
    if (mKind == PSR_WAGON_ROYAL) mpRoyal[1]->setBaseTRMtx(mDoMtx_stack_c::get());

    mDoMtx_stack_c::transS(hitch);
    mDoMtx_stack_c::YrotM(mCoachYaw);
    mDoMtx_stack_c::XrotM(coachPitch);
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
    if (mWrecked && mHorseGone && !fopAcM_CheckStatus(this, fopAcStts_CULL_e)) {
        // the horse is gone: culled again, against the wreck's own frame (the hitch) and round its pieces
        fopAcM_SetMtx(this, mCoachMtx);
        fopAcM_setCullSizeBox(this, -450.0f, -250.0f, -750.0f, 450.0f, 500.0f, 300.0f);
        fopAcM_OnStatus(this, fopAcStts_CULL_e);
    }
    if (!mDriven) mSpeed = 0.0f;
    if (mBurning && !mWrecked && ++mBurnTick >= kBurnEvery) {
        mBurnTick = 0;
        addDamage(1);
    }
    // the wheels roll by the distance covered
    const f32 d = mLoose ? 0.0f : mSpeed;   // a wreck's wheels don't turn
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
    updateFire();
    lampsTick();
    hitTick();
    eyePos = current.pos;
    eyePos.y += 200.0f;
    attention_info.position = eyePos;
    return 1;
}

int psrWagon_c::Draw() {
    g_env_light.settingTevStruct(0, &current.pos, &tevStr);
    J3DModel* list[12] = {};
    int n = 0;
    list[n++] = mpHorse->getModel();   // (drawn below, through its own morf, unless it ran off)
    // the burnt-out wreck, when every piece of it is there (else the normal look stays)
    bool burnt = mCharred && mpBurnt[0] != nullptr && mpBurnt[1] != nullptr && mpBurnt[2] != nullptr;
    if (burnt && mKind == PSR_WAGON_ROYAL) {
        for (int i = 3; i < 7; i++) burnt = burnt && mpBurnt[i] != nullptr;
    }
    if (burnt) {
        if (mKind == PSR_WAGON_ROYAL) {
            for (int i = 0; i < 7; i++) {   // each where its royal piece is
                mpBurnt[i]->setBaseTRMtx(mpRoyal[i]->getBaseTRMtx());
                list[n++] = mpBurnt[i];
            }
        } else {
            mpBurnt[0]->setBaseTRMtx(mCoachMtx);
            mpBurnt[2]->setBaseTRMtx(mCoachMtx);
            mpBurnt[1]->setBaseTRMtx(mHarnessMtx);
            for (int i = 0; i < 3; i++) list[n++] = mpBurnt[i];
        }
    } else if (mKind == PSR_WAGON_ROYAL) {
        for (int i = 0; i < 7; i++) list[n++] = mpRoyal[i];
    } else {
        list[n++] = mpHarness;
        list[n++] = mpCoach;
        // The coach shows its damage through its materials' K colours, as Telma's coach drives them
        // (d_a_npc_coach.cpp: undamaged 255 / 255 / 125; the canvas chars first, then the wood). Left
        // alone they read as burnt: the black cover.
        J3DModelData* cd = mpCoach->getModelData();
        if (cd->getMaterialNum() >= 2) {
            const int dmg = mDamage, half = kMaxDamage / 2;
            const u8 burnt = static_cast<u8>(dmg * 0xFF / kMaxDamage);
            cd->getMaterialNodePointer(0)->getTevKColor(1)->a = static_cast<u8>(0xFF - burnt);
            if (dmg < half) {
                cd->getMaterialNodePointer(1)->getTevKColor(1)->a = static_cast<u8>(0xFF - ((dmg * 0xFF / kMaxDamage) << 1));
                cd->getMaterialNodePointer(1)->getTevKColor(2)->a = 125;
            } else {
                cd->getMaterialNodePointer(1)->getTevKColor(1)->a = 0;
                cd->getMaterialNodePointer(1)->getTevKColor(2)->a = static_cast<u8>(125.0f + (dmg - half) * 20.0f / half);
            }
        }
    }
    if (!mCharred) for (J3DModel* l : mpLamp) if (l != nullptr && n < 12) list[n++] = l;   // the lanterns (gone with the wreck)
    J3DModel* cargo = burnt && mpBurnt[7] != nullptr ? mpBurnt[7] : mpCargo;
    if (cargo != nullptr) {
        if (cargo != mpCargo) cargo->setBaseTRMtx(mCoachMtx);
        list[n++] = cargo;
    }
    for (int i = 0; i < n; i++) g_env_light.setLightTevColorType_MAJI(list[i], &tevStr);
    if (!mHorseGone) mpHorse->entryDL();
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
