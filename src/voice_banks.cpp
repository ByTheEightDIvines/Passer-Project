#include "global.h"

#include <cstdio>

#include "voice_banks.hpp"

#include "d/actor/d_a_npc_passer.h"
#include "mods/service.hpp"
#include "mods/svc/audio_res.h"
#include "mods/svc/hook.hpp"
#include "JSystem/JAudio2/JASBasicWaveBank.h"
#include "travellers.hpp"
#include "camp_music.hpp"

IMPORT_SERVICE(AudioResService, svc_audio_res);

namespace {
#include "voice_table.inc"
int g_registered = 0;
int g_rescued = 0;

bool isOurs(u32 wave) {
    for (u16 w : kVoiceWaves) if (w == wave) return true;
    return camp_music::isOurWave(wave);   // the campfire songs' borrowed slots too
}

// The engine looks up a clip's slot before it checks for a mod's replacement, and the slot only
// exists while the clip's own bank (Castle Town's) is loaded: out in the field the lookup failed
// and our registered clip was never reached. For our voice clips, hand back the slot regardless;
// the engine then finds our clip (always loaded) and plays that.
DEFINE_HOOK(&JASBasicWaveBank::getWaveHandle, WaveHandleLookup);
void onWaveHandlePost(ModContext*, void* args, void* retval, void*) {
    auto** out = static_cast<JASWaveHandle**>(retval);
    auto* bank = mods::arg<const JASBasicWaveBank*>(args, 0);
    const u32 no = mods::arg<u32>(args, 1);
    if (out == nullptr || *out != nullptr || bank == nullptr || bank->bankId != 0) return;
    if (bank->mWaveTable == nullptr || bank->mHandleCount == 0) return;
    if (camp_music::isOurWave(no) && no >= bank->mHandleCount) {
        // a wave the mod added (its ID is past the end of the game's table): any real entry will do,
        // the engine then plays the added wave registered under this ID
        *out = bank->mWaveTable;
        ++g_rescued;
        return;
    }
    if (no >= bank->mHandleCount || !isOurs(no)) return;
    *out = bank->mWaveTable + no;
    ++g_rescued;
}
}  // namespace

namespace voice_banks {

// The voice clips ship with the mod (res/voices/wNNNN.wav, cut from Castle Town's own sound banks)
// and are registered over their slots in the sound-effect bank, so they're always loaded: the
// travellers' voices play anywhere, with nothing loaded per area.
bool install() {
    if (svc_audio_res == nullptr) return false;
    if (mods::hook::add_post<WaveHandleLookup>(onWaveHandlePost) != MOD_OK) return false;
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
    static int lastRescued = 0;
    if (!logged) {
        logged = true;
        travellers::diagLine("voice   %d of %d clips registered", g_registered,
                             static_cast<int>(sizeof(kVoiceWaves) / sizeof(kVoiceWaves[0])));
    }
    if (g_rescued != lastRescued) {   // our clips played with their bank not loaded (diary, for checking)
        travellers::diagLine("voice   clip lookups answered by the mod: %d", g_rescued);
        lastRescued = g_rescued;
    }
}

void play(daNpcPasser_c* passer, int line) {
    if (passer == nullptr || line < 0 || line > 5) return;
    const int type = passer->m_type;
    const u32 id = type >= 0 && type < 30 ? kVoiceIds[type][line] : 0x50111u + 6u * type + line;
    passer->mCitizen.mSoundObjSimple1.startSound(id, 0, -1);
}

}  // namespace voice_banks
