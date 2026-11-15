#include "global.h"

#include <cstdio>

#include "voice_banks.hpp"

#include "d/actor/d_a_npc_passer.h"
#include "mods/service.hpp"
#include "mods/svc/audio_res.h"
#include "travellers.hpp"

IMPORT_SERVICE(AudioResService, svc_audio_res);

namespace {
#include "voice_table.inc"
int g_registered = 0;
}  // namespace

namespace voice_banks {

// The voice clips ship with the mod (res/voices/wNNNN.wav, cut from Castle Town's own sound banks)
// and are registered over their slots in the sound-effect bank, so they're always loaded: the
// travellers' voices play anywhere, with nothing loaded per area.
bool install() {
    if (svc_audio_res == nullptr) return false;
    for (u16 w : kVoiceWaves) {
        char path[48];
        std::snprintf(path, sizeof(path), "res/voices/w%04u.wav", static_cast<unsigned>(w));
        if (svc_audio_res->replace_wave(mod_ctx, AUDIO_WAVE_BANK_SOUND_EFFECTS, w, path, nullptr, nullptr) == MOD_OK) {
            ++g_registered;
        }
    }
    return g_registered > 0;
}

void tick() {
    static bool logged = false;
    if (!logged) {
        logged = true;
        travellers::diagLine("voice   %d of %d clips registered", g_registered,
                             static_cast<int>(sizeof(kVoiceWaves) / sizeof(kVoiceWaves[0])));
    }
}

void play(daNpcPasser_c* passer, int line) {
    if (passer == nullptr || line < 0 || line > 5) return;
    const int type = passer->m_type;
    const u32 id = type >= 0 && type < 30 ? kVoiceIds[type][line] : 0x50111u + 6u * type + line;
    passer->mCitizen.mSoundObjSimple1.startSound(id, 0, -1);
}

}  // namespace voice_banks
