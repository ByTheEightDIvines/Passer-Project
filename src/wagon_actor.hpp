#pragma once
#include "mods/svc/actor.h"

#include "f_op/f_op_actor.h"
#include "SSystem/SComponent/c_phase.h"
#include "d/d_bg_w.h"

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

    // Set by the convoy each frame while driven: where the horse is and which way it faces.
    void driveTo(const cXyz& horse, s16 yaw, f32 speed);
    // Spots on the wagon in the world: the driver's bench (seat 0 left, 1 right) and the hitch.
    cXyz benchSeat(int seat) const;
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
