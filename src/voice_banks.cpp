#include "global.h"

#include <cstdio>

#include "voice_banks.hpp"

#include "SSystem/SComponent/c_math.h"
#include "d/actor/d_a_npc_passer.h"
#include "mod_sounds.hpp"
#include "mods/svc/audio_res.h"
#include "travellers.hpp"

namespace {
#include "voice_table.inc"
// Each clip is a sound of its own (mod_sounds), on programs 0x88.. in the travellers' voice category.
constexpr u8 kProgram0 = 0x88;
u32 g_sound[kClipCount] = {};
int g_registered = 0;
}  // namespace

namespace voice_banks {

bool install() {
    for (int i = 0; i < kClipCount; i++) {
        const u8 program = static_cast<u8>(kProgram0 + i);
        char path[48];
        std::snprintf(path, sizeof(path), "res/voices/w%04u.wav", static_cast<unsigned>(kVoiceClips[i]));
        if (!mod_sounds::loadWave(program, path, false, 0)) continue;
        g_sound[i] = mod_sounds::addSound(SE_CATEGORY_CHARA_VOICE, program);
        if (g_sound[i] != 0) ++g_registered;
    }
    return g_registered > 0;
}

void tick() {
    static bool logged = false;
    static int lastAnswered = 0;
    if (!logged) {
        logged = true;
        travellers::diagLine("voice   %d of %d clips added", g_registered, kClipCount);
    }
    if (mod_sounds::lookupsAnswered() != lastAnswered) {
        lastAnswered = mod_sounds::lookupsAnswered();
        travellers::diagLine("voice   added-sound wave lookups answered: %d", lastAnswered);
    }
}

void play(daNpcPasser_c* passer, int line) {
    if (passer == nullptr || line < 0 || line > 5) return;
    const int type = passer->m_type;
    if (type < 0 || type >= 30) return;
    const Takes& t = kVoiceTakes[type][line];
    if (t.count == 0) return;
    int k = static_cast<int>(cM_rndF(static_cast<f32>(t.count)));
    if (k >= t.count) k = t.count - 1;
    const u32 id = g_sound[t.clip[k]];
    if (id != 0) passer->mCitizen.mSoundObjSimple1.startSound(id, 0, -1);
}

}  // namespace voice_banks
