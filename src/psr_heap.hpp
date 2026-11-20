#pragma once
// Where the mod's archives load: the game's Zelda pool, not the default archive pool.
//
// The archive pool (about 18 MB on PC) is where Hyrule Field's own files live, and with ours on top it
// ran full: a 4.9 MB PsrPack reload, then Beedle's 1.27 MB archive, found no gap big enough and Dusklight
// aborted (it stops on the first failed allocation). The Zelda pool is the game's own place for things
// that stay loaded, and its own fallback for archives that don't fit (on GameCube mountArchive retries
// there first); in the field it had about 26 MB free. The game pool (66 MB free) is left alone on
// purpose: every actor's heap is carved from it and freed again, so a long-lived archive there would
// chop it up.
//
// The first load of an archive decides its pool; later loads of the same archive just share it.
#include "JSystem/JKernel/JKRExpHeap.h"
#include "m_Do/m_Do_ext.h"

inline JKRHeap* psrHeap() {
    return mDoExt_getZeldaHeap();
}
