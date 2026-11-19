#pragma once
// The soldiers and Link's bombs (agreed with the user, 2026-11-19).
//
// Two squads: the guards at their camp by the Bridge of Eldin, and the royal convoy's escort. A bomb
// of Link's going off near one of a squad's soldiers is a strike against Link:
//   1st  a soldier turns to him: "Oi! Watch where you're throwing those!"
//   2nd  "That's your last warning, lad. Put them away."
//   3rd  "Right! You asked for it!" - and the whole squad comes at him, for real.
// A minute without a bomb and the count starts again. In the fight Link can hurt them but never
// kill them: the one he knocks down to his last heart yields ("Don't tell the captain.", ...) and
// the squad stands down; for a while after, talking to any of them gets a sore line. If Link is the
// one down to his last heart, they stop: "...Let that be a lesson." If he just runs off, they let
// him go. Ordinary travellers only ever run from bombs.
#include "global.h"
#include <dolphin/types.h>
#include "mods/svc/flow.hpp"

class fopAc_ac_c;
class daNpcPasser_c;

namespace grudge {
// 0: not a soldier; 1: the camp guards; 2: the royal escort
int squadOf(const fopAc_ac_c* actor);
// One of Link's bombs went off (or is about to) by this soldier.
void onBomb(daNpcPasser_c* soldier, const fopAc_ac_c* bomb);
// The squad is fighting Link.
bool hostile(const fopAc_ac_c* soldier);
bool squadHostile(int squad);
// Link has knocked this soldier down to his last heart: he yields, the squad stands down.
void yield(daNpcPasser_c* soldier);
// Once a frame.
void tick();
// Beaten by Link not long ago: they say so when he talks to them.
bool sore(const fopAc_ac_c* soldier);

ModResult registerLines(mods::flow::GraphBuilder& graph, u16 group, const MessageLanguage* langs, int nLangs);
void linesReady(bool ok);   // their graph was committed (or not)
ModResult registerActor();
}  // namespace grudge
