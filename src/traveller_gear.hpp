#pragma once
// Traveller looks: retextured bodies and packs (see traveller_gear.cpp).
class daNpcPasser_c;
class cXyz;
#include "global.h"

namespace traveller_gear {
bool install();
// Sit this traveller down on its log at a camp fire (true if it did), or note it got up.
bool sit(daNpcPasser_c* passer, bool on);
bool sitting(int slot);
// Once a frame: fighters carry a torch at night, everyone else a lantern, the old women a hooked
// staff all day with the lantern lit on it at night; flames and light. Returns whether the left arm
// should hold something up now.
bool lightTick(daNpcPasser_c* passer, bool night, bool atCamp);
// Fighters put the torch down to fight and pick it up again after.
bool torchInHand(daNpcPasser_c* passer);
void dropTorch(daNpcPasser_c* passer);
// Lying on the ground: where to stand (and face) to pick it up.
bool torchOnGround(daNpcPasser_c* passer, cXyz* standAt, s16* yaw);
void takeTorch(daNpcPasser_c* passer);
}
