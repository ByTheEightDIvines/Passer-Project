#pragma once
// The old women's freeze spell: a monster held in Link's own freeze ice for a few seconds (the game's
// LK_FREEZ effect, the one Link gets from Freezards), then it thaws, or a blow shatters it and it dies.
//
// Link's freeze effects only exist in the snowy stages' particle packs. We carry a byte-for-byte copy
// of just those three effects (psr_freeze.jpc in PsrPack.arc) and give the game's particle manager a
// third pack slot for it, next to the always-loaded pack and the stage's. Nothing of the game's own
// packs is touched.
#include "global.h"
#include "dolphin/types.h"

class fopAc_ac_c;
class cXyz;

namespace psr_freeze {
// Freeze this monster now (false if it can't be: already frozen, our effects not loaded yet).
bool freeze(fopAc_ac_c* enemy);
bool isFrozen(const fopAc_ac_c* actor);
// One of the game effects in our own pack (psr_fx.jpc), shown anywhere: the freeze ice and its
// breaking (0x8818-0x881A) and the treasure chest's appear puff (0x8840-0x8845).
bool effect(u16 id, const cXyz& pos, s16 yaw, const cXyz& scale);
// Our copy of the effects (psr_fx.jpc in PsrPack.arc; call while a traveller has it loaded).
void loadEffects();
bool install();
}  // namespace psr_freeze
