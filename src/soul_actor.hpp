#pragma once
#include "mods/svc/actor.h"

#include "f_op/f_op_actor.h"
#include "SSystem/SComponent/c_phase.h"
#include "JSystem/J3DGraphBase/J3DPacket.h"
#include "helpers/gx_helper.h"

// A fallen traveller's soul: a little cyan wisp with a carved face, head up and tail trailing, that
// rises out of the game's smoke puff, sways as it floats up and fades away (about 3.5 seconds).
// Drawn by the mod itself as a camera-facing sprite (psr_soul.bti in PsrPack.arc: four frames of a
// flickering tail, cross-faded), so nothing of the game's shared effects is touched.
#define PSR_SOUL_NAME "PsrSoul"

class psrSoul_c;

class psrSoulPacket_c : public J3DPacket {
public:
    psrSoul_c* mpOwner = nullptr;
    void draw() override;
};

class psrSoul_c : public fopAc_ac_c {
public:
    request_of_phase_process_class mPhase;
    psrSoulPacket_c mPacket;
    TGXTexObj mTex;
    bool mTexReady = false;
    Mtx mMtx;
    int mFrame = 0;
    cXyz mBase;

    virtual ~psrSoul_c();
    cPhs_Step create();
    int Delete();
    int Execute();
    int Draw();

    static s16 sProcName;
    static ActorHandle sActorHandle;
    static const ActorProfileDesc sProfile;
};

namespace psr_soul {
// Releases a soul from where a traveller fell (its feet).
void spawn(const cXyz& pos, int room);
ModResult registerActor();
}  // namespace psr_soul
