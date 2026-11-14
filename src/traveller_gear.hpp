#pragma once
// Traveller looks: retextured bodies and packs (see traveller_gear.cpp).
class daNpcPasser_c;

namespace traveller_gear {
bool install();
// Sit this traveller down on its log at a camp fire (true if it did), or note it got up.
bool sit(daNpcPasser_c* passer, bool on);
bool sitting(int slot);
// Once a frame: carry a torch or lantern (each traveller has one or the other) while `wanted`,
// with its flame and light. Returns whether it is carrying one now (the arm then holds it up).
bool lightTick(daNpcPasser_c* passer, bool wanted);
}
