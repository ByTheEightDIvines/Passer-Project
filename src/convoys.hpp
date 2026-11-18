#pragma once

class daNpcPasser_c;

// The escort convoys and the parked wagons by the twilight walls (see convoys.cpp).
namespace convoys {
void update();                          // once per frame (mod_update)
void placeHeld(daNpcPasser_c* passer);  // a rider or escort being placed by hand: put it where it goes
// Who a convoy person is, for their lines: -1 not one of ours; else the convoy kind
// (0 gorge travellers, 1 west-gate travellers, 2 royal) or 10/11 for the wagon parked at wall 1/2
int crewSpot(int slot);
}
