#pragma once

class fopAc_ac_c;

// Where the convoy people's lines go. The dialogue is written last: each spot below has a
// placeholder, and spotFor() says which spot a character is at right now, so the lines drop in
// without touching anything else. (Not shown in game yet: the talk hookup comes with the lines.)
namespace dialogue {
enum Spot : int {
    SPOT_NONE = -1,
    // the wagon parked below a twilight wall, with its people standing about
    SPOT_NOD_WALL1,          // Faron-Eldin wall: how scary it is; they won't go through till it's gone
    SPOT_NOD_WALL2,          // Eldin-Lanayru wall: the same people, recognising Link from the first wall
    // the guards' camp at the Eldin bridge (out once Castle Town is lit)
    SPOT_GUARD_BRIDGE_OUT,   // the bridge is gone: they're stuck here
    SPOT_GUARD_BRIDGE_BACK,  // the bridge is back: "back on the road after one more ale" (and stay)
    // on the road
    SPOT_CONVOY_GORGE,       // the gorge travellers' convoy
    SPOT_CONVOY_WEST,        // the west-gate travellers' convoy
    SPOT_CONVOY_ROYAL,       // the royal convoy on the long rough road
    SPOT_COUNT
};

Spot spotFor(const fopAc_ac_c* actor);
const char* placeholder(Spot spot);   // the marker text until the real line is written
}  // namespace dialogue
