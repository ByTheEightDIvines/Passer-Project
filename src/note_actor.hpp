#pragma once
#include "mods/svc/actor.h"

#include "f_op/f_op_actor.h"
#include "SSystem/SComponent/c_phase.h"
#include "JSystem/J3DGraphBase/J3DPacket.h"
#include "helpers/gx_helper.h"

// A fallen traveller's soul: a little cyan wisp with a carved face, head up and tail trailing, that
// rises out of the game's smoke puff, sways as it floats up and fades away (about 3.5 seconds).
// Drawn by the mod itself as a camera-facing sprite (psr_note.bti in PsrPack.arc: four frames of a
// flickering tail, cross-faded), so nothing of the game's shared effects is touched.
#define PSR_NOTE_NAME "PsrNote"

class psrNote_c;

class psrNotePacket_c : public J3DPacket {
public:
    psrNote_c* mpOwner = nullptr;
    void draw() override;
};

class psrNote_c : public fopAc_ac_c {
public:
    request_of_phase_process_class mPhase;
    psrNotePacket_c mPacket;
    TGXTexObj mTex;
    bool mTexReady = false;
    Mtx mMtx;
    int mFrame = 0;
    cXyz mBase;
    int mKind = 0;      // which note
    f32 mDrift = 0.0f;  // sideways drift (camera units a frame)
    f32 mSize = 26.0f;
    f32 mSwayPhase = 0.0f;

    virtual ~psrNote_c();
    cPhs_Step create();
    int Delete();
    int Execute();
    int Draw();

    static s16 sProcName;
    static ActorHandle sActorHandle;
    static const ActorProfileDesc sProfile;
};

namespace psr_note {
// A note off a musician's head.
void spawn(const cXyz& head, int room);
ModResult registerActor();
}  // namespace psr_note
