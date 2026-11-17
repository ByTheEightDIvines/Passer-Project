#pragma once
// Sounds the mod adds: each is a new sound-table entry that plays one held note of a wave the mod adds.
// The game has no sequence for a new entry and no instrument for a new wave, so this supplies them
// with hooks: the sequence lookup serves a tiny one-note sequence for our entries, and the instrument
// lookup answers our program numbers (ones the game's bank has no instrument for) with our wave. A
// third hook answers the wave lookup for our wave IDs (they sit past the end of the game's table).
#include "dolphin/types.h"
#include "mods/svc/audio_res.h"
namespace mod_sounds {
bool install();                                  // the hooks, once (before adding sounds)
// A new sound in a sound-effect category (SoundEffectCategory) for program number `program`
// (0x88..0xEF). Returns its sound ID (0 on failure).
u32 addSound(u8 category, u8 program, const AudioSoundTableEffectInfo* info = nullptr);
// Load a wave from the mod's files (e.g. "res/voices/w0298.wav") for `program`; looping or not.
// Returns false if it can't. unloadWave frees it again (the program then plays nothing).
bool loadWave(u8 program, const char* path, bool loop, u32 loopEnd);
void unloadWave(u8 program);
bool hasWave(u8 program);
int lookupsAnswered();                           // our waves found by the wave lookup (diary)
int seqAnswered();                               // our sounds' note sequences handed to the game (diary)
int instAnswered();                              // our instruments handed to the game (diary)
}  // namespace mod_sounds
