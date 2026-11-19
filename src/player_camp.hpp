#pragma once
// Link's own camp. Midna keeps a set of camping gear (found in Faron's twilight); out in Hyrule Field
// her menu's last choice becomes "More...", with "Set up camp" in it. She summons the camp onto the
// flattest open ground near Link (clear of the travellers' camps and the roads): Coro's fire (no pot),
// a bench and a small tent, all from the mod's own camp pieces. Link lights the fire with a lantern
// swing, as Coro's; as a wolf, Midna sparks it for him. Checking the tent asks "Go to sleep?": yes, and
// the camera pulls back over the camp while the sky rolls on twelve hours, and Midna wakes him.
//
// One camp at a time: summoning again moves it. Where it stands is kept with the save file.
#include "global.h"
#include "mods/api.h"

namespace player_camp {
// Registers the camp actor, Midna's menu additions, the camp's lines, the camera and the save record.
// Call once from mod_initialize (after the flow and actor services are up).
ModResult install();
// Once per frame (mod_update): brings the camp out when Link is near where it stands, takes it away when
// he leaves, and starts a summon Midna was asked for.
void update();
// Link is asleep in his tent (the tent shuts and lights up from inside).
bool sleeping();

// Test buttons (Mods window): Midna has the gear and has told Link about it / forget all of it.
void testGiveGear();
void testReset();
}  // namespace player_camp
