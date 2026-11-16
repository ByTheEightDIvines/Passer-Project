#include "global.h"

#include <cstdio>

#include "voice_banks.hpp"

#include "SSystem/SComponent/c_math.h"
#include "d/actor/d_a_npc_passer.h"
#include "mod_sounds.hpp"
#include "mods/svc/audio_res.h"
#include "travellers.hpp"
#include "Z2AudioLib/Z2AudioMgr.h"
#include "d/d_com_inf_game.h"
#include <cmath>

namespace {
#include "voice_table.inc"
// Each clip is a sound of its own (mod_sounds), on programs 0x88.. in the travellers' voice category.
constexpr u8 kProgram0 = 0x88;
u32 g_sound[kClipCount] = {};
int g_registered = 0;
// Heard like the townsfolk: the mod sets each line's volume by how far Link is (the sound table's own
// distance falloff for a new entry isn't one the game's voices use), panned from where it's said.
constexpr f32 kNear = 400.0f, kFar = 3000.0f;
const AudioSoundTableEffectInfo& voiceInfo() {
    static AudioSoundTableEffectInfo info = [] {
        AudioSoundTableEffectInfo i = *svc_audio_res->default_effect_info;
        i.ignore_distance_volume = true;
        i.volume = 1.0f;
        i.always_max_priority = true;
        return i;
    }();
    return info;
}
constexpr int kHandles = 8;              // a few lines can overlap
JAISoundHandle g_handle[kHandles];
JGeometry::TVec3<f32> g_from[kHandles];
int g_next = 0;
int g_started = 0, g_failed = 0;
}  // namespace

namespace voice_banks {

bool install() {
    for (int i = 0; i < kClipCount; i++) {
        const u8 program = static_cast<u8>(kProgram0 + i);
        char path[48];
        std::snprintf(path, sizeof(path), "res/voices/w%04u.wav", static_cast<unsigned>(kVoiceClips[i]));
        if (!mod_sounds::loadWave(program, path, false, 0)) continue;
        g_sound[i] = mod_sounds::addSound(SE_CATEGORY_CHARA_VOICE, program, &voiceInfo());
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
    if (id == 0) return;
    f32 gain = 0.0f;
    if (auto* link = dComIfGp_getPlayer(0)) {
        const f32 dx = link->current.pos.x - passer->current.pos.x, dy = link->current.pos.y - passer->current.pos.y,
                  dz = link->current.pos.z - passer->current.pos.z;
        const f32 d = std::sqrt(dx * dx + dy * dy + dz * dz);
        gain = d <= kNear ? 1.0f : d >= kFar ? 0.0f : 1.0f - (d - kNear) / (kFar - kNear);
    }
    if (gain <= 0.0f) return;
    const int h = g_next; g_next = (g_next + 1) % kHandles;
    if (g_handle[h]) g_handle[h]->stop(0);
    g_from[h].set(passer->eyePos.x, passer->eyePos.y, passer->eyePos.z);
    Z2GetAudioMgr()->startSound(JAISoundID(id), &g_handle[h], &g_from[h]);
    if (g_handle[h]) {
        g_handle[h]->getAuxiliary().moveVolume(gain, 0);
        ++g_started;
    } else {
        ++g_failed;
    }
    travellers::diagLine("voice   clip %d at %.2f: %s (started %d, failed %d)", static_cast<int>(kVoiceClips[t.clip[k]]), gain,
                         g_handle[h] ? "playing" : "did not start", g_started, g_failed);
}

}  // namespace voice_banks
