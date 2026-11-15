#pragma once
#include "mods/svc/actor.h"

#include "f_op/f_op_actor.h"
#include "SSystem/SComponent/c_phase.h"
#include "JSystem/J3DGraphBase/J3DPacket.h"
#include "helpers/gx_helper.h"

// A thought or speech bubble over a traveller's head: one icon from psr_bubbles.bti (22 of them, with their
// bubbles and animation frames, in PsrPack.arc; see bubble_looks.inc), drawn by the mod as a camera-facing sprite. It pops in with a little
// bounce, holds, and fades; the traveller moves it along with its head (psr_bubble::follow).
#define PSR_BUBBLE_NAME "PsrBubl"

class psrBubble_c;

class psrBubblePacket_c : public J3DPacket {
public:
    psrBubble_c* mpOwner = nullptr;
    void draw() override;
};

class psrBubble_c : public fopAc_ac_c {
public:
    request_of_phase_process_class mPhase;
    psrBubblePacket_c mPacket;
    TGXTexObj mTex;
    bool mTexReady = false;
    Mtx mMtx;
    int mFrame = 0;
    int mIcon = 0;
    int mLife = 75;

    virtual ~psrBubble_c();
    cPhs_Step create();
    int Delete();
    int Execute();
    int Draw();

    static s16 sProcName;
    static ActorHandle sActorHandle;
    static const ActorProfileDesc sProfile;
};

namespace psr_bubble {
// The icons, in the sheet's order.
enum Icon {
    kHappy, kAngry, kScared, kSad, kSurprised, kConfused, kLove, kSleepy, kAttack, kFlee,
    kDefend, kHungry, kThirsty, kTired, kMusic, kChat, kCook, kTravel, kFire, kHeal, kJoke, kLaugh, kCount
};
// A thought cloud (a need, about to be acted on) rather than a speech or burst bubble.
bool isThought(int icon);
// Starts a bubble at `pos` (over the head); returns its process ID.
fpc_ProcID spawn(const cXyz& pos, int room, int icon);
// Keeps a live bubble over the head; false once it has gone.
bool follow(fpc_ProcID id, const cXyz& pos);
void remove(fpc_ProcID id);
ModResult registerActor();
}  // namespace psr_bubble
