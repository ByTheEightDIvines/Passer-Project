#pragma once
#include "mods/svc/actor.h"

#include "f_op/f_op_actor.h"
#include "d/d_kankyo.h"
#include "SSystem/SComponent/c_phase.h"
#include "d/d_bg_w.h"
#include "d/d_cc_d.h"

// The convoy wagon: Telma's wagon put together the way the game's own coach is (horse at the actor,
// harness on the horse, the wagon on the harness's hitch), but driven by our convoy code along its
// road instead of the escort mission's rails, and with none of the escort's counters or save flags.
//   params low byte: 0 = the travellers' wagon (Telma's own wagon, the market cargo),
//                    1 = the royal wagon (castle wood, iron wheels, the banner canvas, the treasure).
//   params bit 8:    driven (the convoy moves it); clear = parked where it was made, horse waiting.
// The horse, harness and Telma's wagon come straight from the disc's Coach.arc; the royal pieces and
// both cargos from PsrWagon.arc in the dusk's overlay.
#define PSR_WAGON_NAME "PsrWgn"   // (actor names are at most 7 letters)

enum PsrWagonKind : u8 { PSR_WAGON_TRAVELLER = 0, PSR_WAGON_ROYAL = 1 };
constexpr u32 kPsrWagonDriven = 0x100;

class J3DModel;
class mDoExt_McaMorfSO;
class JPABaseEmitter;

class psrWagon_c : public fopAc_ac_c {
public:
    request_of_phase_process_class mPhaseCoach;
    request_of_phase_process_class mPhaseOwn;
    u8 mKind = PSR_WAGON_TRAVELLER;
    bool mDriven = false;
    mDoExt_McaMorfSO* mpHorse = nullptr;
    J3DModel* mpHarness = nullptr;      // the game's harness and wagon: drawn for the travellers' wagon,
    J3DModel* mpCoach = nullptr;        // posed (never drawn) for the royal one, which needs their joints
    J3DModel* mpRoyal[8] = {};          // body, harness, cover, 4 wheels
    J3DModel* mpCargo = nullptr;
    dBgW* mpBgCoach = nullptr;
    dBgW* mpBgHarness = nullptr;
    Mtx mCoachMtx;
    Mtx mHarnessMtx;
    cXyz mRear;                          // the wagon's rear axle (the wagon trails the hitch by it)
    s16 mCoachYaw = 0;
    s16 mFrontSpin = 0, mRearSpin = 0;  // the wheels' roll
    f32 mSpeed = 0.0f;                   // the convoy's speed this frame (units a frame)
    int mAnim = -1;
    bool mPlaced = false;
    // damage: monsters at it, and fire (a Bulblin's fire arrow in the wood) that burns it down
    static constexpr int kMaxDamage = 255;
    int mDamage = 0;
    int mBurnTick = 0;
    bool mBurning = false;
    bool mWrecked = false;          // burnt out or smashed: a wreck that stays where it is
    bool mLoose = false;            // the horse broke loose and runs on its own
    cXyz mHarnessPos;               // where the harness was left when the horse broke loose
    s16 mHarnessYaw = 0;
    bool mOnRoad = false;           // driveOnRoad: the harness and the rear are given
    s16 mRoadHarnessYaw = 0;
    JPABaseEmitter* mFx[10] = {};   // (unused: Telma's coach fire only exists in her own stage)
    u32 mFlames[8][3] = {};          // the common fire (the camp fires'), at up to 8 spots on the wagon
    LIGHT_INFLUENCE mFireLight{};
    // the two lanterns hung at the cover's front corners, lit at night (out once it's a wreck)
    J3DModel* mpLamp[2] = {};
    u32 mLampFlame[2] = {};
    LIGHT_INFLUENCE mLampLight{};
    bool mLampLit = false;
    void lampsTick();
    void cutLampLight();
    bool mFireLit = false;
    int mSmoke = 0;                 // frames the wreck still smokes
    int mBlaze = 0;                 // destroyed: frames it still burns at full blaze (whatever wrecked it)
    bool mCharred = false;          // drawn as the burnt-out wreck (swapped in mid-blaze)
    // the burnt-out wreck: royal - body, harness, cover, 4 wheels (as mpRoyal), [7] cargo;
    // travellers' - [0] wagon, [1] harness, [2] cover, [7] cargo. Missing ones: the normal look stays
    J3DModel* mpBurnt[8] = {};
    bool mHorseGone = false;        // the loose horse ran off out of sight

    // Hit boxes along the wagon's body: monsters' blows that land on it show (a small spark and a
    // knock of wood) and do damage.
    dCcD_Stts mStts;
    dCcD_Cyl mBody[3];
    int mHitCool = 0;
    void hitTick();
    void hitFx(const cXyz& at);   // a monster's blow lands here

    void addDamage(int amount);
    void ignite();
    bool wrecked() const { return mWrecked; }
    void updateFire();
    void cutFireLight();

    // Set by the convoy each frame while driven: where the horse is and which way it faces.
    void driveTo(const cXyz& horse, s16 yaw, f32 speed);
    // On a road: the horse here, and the spots on the road behind it where the hitch and the rear
    // axle are, so the whole wagon follows the road's own bends (never cutting them into a wall).
    void driveOnRoad(const cXyz& horse, s16 yaw, const cXyz& hitch, const cXyz& rear, f32 speed);
    static f32 hitchBack();   // how far behind the horse the hitch is
    static f32 rearBack();    // ...and the rear axle behind the hitch
    // Spots on the wagon in the world: the driver's bench (seat 0 left, 1 right) and the hitch.
    cXyz benchSeat(int seat) const;
    cXyz middle() const;          // the middle of the wagon's bed
    s16 coachYaw() const { return mCoachYaw; }
    s16 frontWheelSpin() const { return mFrontSpin; }
    s16 rearWheelSpin() const { return mRearSpin; }

    cPhs_Step create();
    int Delete();
    int Execute();
    int Draw();
    ~psrWagon_c();

    static int createHeapCallBack(fopAc_ac_c* actor);
    int CreateHeap();
    void placeParts();
    void setHorseAnim();

    static s16 sProcName;
    static ActorHandle sActorHandle;
    static const ActorProfileDesc sProfile;
};

namespace psr_wagon {
ModResult registerActor();
}
