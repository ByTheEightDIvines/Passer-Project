#pragma once
// Traveller looks: retextured bodies and packs (see traveller_gear.cpp).
class daNpcPasser_c;
class cXyz;
class fopAc_ac_c;
#include "global.h"

namespace traveller_gear {
// Recoloured outfits (hair and clothes; never skin) for the bodies that more than one traveller wears:
// how many this body type has besides its own (0 for most). A person's look 0 is the body as it is,
// 1..lookVariants(type) one of the recolours (PV<tt><v>.arc in the overlay).
int lookVariants(int type);
// The food or drink it takes out for a meal (1 food held by one end, 2 bottle, 3 goblet, 4 food in the
// palm, 5 tankard; 0 puts it away): one of that kind at random.
// Picks this meal's item and returns the loop to play (1 eat, 2 drink, 3 sip, 4 nibble, 5 quaff,
// 6 eat off the side), or 0 if there is nothing to hold.
int pickMeal(daNpcPasser_c* passer, int kind);
// Nothing in hand (for a laugh, which rides the meal layer).
void clearMeal(daNpcPasser_c* passer);
bool install();
// Sit this traveller down on its log at a camp fire (true if it did), or note it got up.
bool sit(daNpcPasser_c* passer, bool on);
bool sitting(int slot);
// Sitting on a camp bench: how far to raise (or lower) the body onto its seat; 0 otherwise.
f32 benchLift(daNpcPasser_c* passer);
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
// The bard (MAN_b): his guitar out (the game's j_guitar on his chest) or put away.
bool isBard(const daNpcPasser_c* passer);
void setPlaying(daNpcPasser_c* passer, bool on);
// The bowman: an arrow off his bow at a monster (the Bulblins' arrow actor, aimed as they aim, made
// to hurt monsters only and drawn as Link's arrow). False if none could be loosed.
bool shootArrow(daNpcPasser_c* passer, fopAc_ac_c* target);
// One of his arrows (never hurts a traveller).
bool isOurArrow(const fopAc_ac_c* actor);
// The old women: where the lantern on the staff hangs (its base), false if she has no staff.
bool lanternMtx(daNpcPasser_c* passer, Mtx out);
}
