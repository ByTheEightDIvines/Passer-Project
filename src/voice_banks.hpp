#pragma once
// The townsfolk's voices (their "hey", "whoa", screams...) and the Castle Town band live in Castle
// Town's own sound banks, which the game only loads there: out in the field every call to them was
// silent. While travellers are about, those two banks are loaded too; they are let go again just
// before the game loads the next area's sounds, so they never crowd that area's own out.
namespace voice_banks {
// Once a frame while travellers are out (cheap: only acts when a bank isn't loaded yet).
void tick();
bool install();
}  // namespace voice_banks
