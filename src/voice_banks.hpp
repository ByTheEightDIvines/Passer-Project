#pragma once
// The travellers' voices. The game keeps the townsfolk's voice lines in Castle Town's own sound
// banks (only loaded there), and many characters have no clip at all for some lines. So the mod
// ships the clips itself (res/voices/, cut from those banks) and adds each as a sound of its own
// (mod_sounds); each traveller type plays its own line where the game has one, and a shared man's or
// woman's line where it doesn't (voice_table.inc).
class daNpcPasser_c;
namespace voice_banks {
bool install();
void tick();   // (once: notes in the diary how many clips registered)
// line: 0 away, 1 surprise, 2 fear, 3 hey, 4 laugh, 5 hmm
void play(daNpcPasser_c* passer, int line);
}  // namespace voice_banks
