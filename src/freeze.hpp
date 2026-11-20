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
struct cXyz;

namespace psr_freeze {
// Freeze this monster now (false if it can't be: already frozen, our effects not loaded yet).
bool freeze(fopAc_ac_c* enemy);
bool isFrozen(const fopAc_ac_c* actor);
// One of the game effects in our own pack (psr_fx.jpc), shown anywhere, through the game's own effect
// routine (dComIfGp_particle_set): the pack's lookup is hooked so our numbers (kFx*, all 0xF0xx, a range
// no game pack uses) come from our pack, every other effect exactly as before.
bool effect(u16 id, const cXyz& pos, s16 yaw, const cXyz& scale);
// Our pack's effects (copied from the game's packs; their original numbers there in brackets).
constexpr u16 kFxIceBreakA = 0xF000;    // LK_FREEZEND_A (Snowpeak 0x8818)
constexpr u16 kFxIceBreakB = 0xF001;    // LK_FREEZEND_B (0x8819)
constexpr u16 kFxIceBlock = 0xF002;     // LK_FREEZ_A (0x881A)
constexpr u16 kFxChestPuff = 0xF003;    // the treasure chest's appear puff, 6 parts (dungeons' 0x8840-0x8845)
constexpr u16 kFxDeathSmoke = 0xF009;   // the monster puff's smoke, grey (common 0x108)
constexpr u16 kFxSpellGlow = 0xF00A;    // the old women's spell (the chest puff's, icy blue: 0x110-0x112)
constexpr u16 kFxSpellSparks = 0xF00B;
constexpr u16 kFxSpellBurst = 0xF00C;
constexpr u16 kFxMidnaPop = 0xF00D;     // Midna's snap, the sparkle (Faron spring 0x86BA)
constexpr u16 kFxMidnaPop2 = 0xF00E;    // ...its flash (0x86C2)
constexpr u16 kFxMidnaHand = 0xF00F;    // ...her hand's glow (0x86BC, 0x86BD)
constexpr u16 kFxMidnaHand2 = 0xF010;
// Our copy of the effects (psr_fx.jpc in PsrPack.arc; call while a traveller has it loaded).
void loadEffects();
bool install();
}  // namespace psr_freeze
