#pragma once

class daNpcPasser_c;

// The escort convoys and the parked wagons by the twilight walls (see convoys.cpp).
namespace convoys {
void update();                          // once per frame (mod_update)
void placeHeld(daNpcPasser_c* passer);  // a rider or escort being placed by hand: put it where it goes
}
