#include "global.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "camp_music.hpp"

#include "SSystem/SComponent/c_math.h"
#include "Z2AudioLib/Z2AudioMgr.h"
#include "d/actor/d_a_npc_passer.h"
#include "d/d_com_inf_game.h"
#include "mods/service.hpp"
#include "mods/svc/audio_res.h"
#include "travellers.hpp"

IMPORT_SERVICE(AudioResService, svc_audio_res);

namespace {
// The seven songs: file stem and length (all three stems of a song are the same length, 32 kHz).
struct Song { const char* name; u32 samples; };
constexpr Song kSongs[] = {
    {"kakariko_village", 1024000}, {"sarias_song", 1152000}, {"song_of_healing", 1024000},
    {"ballad_of_the_wind_fish", 960000}, {"gerudo_valley", 1536000}, {"alttp_forest", 960000},
    {"song_of_storms", 1024000},
};
constexpr int kSongCount = sizeof(kSongs) / sizeof(kSongs[0]);
constexpr const char* kStem[3] = {"guitar", "men", "women"};

// Three borrowed sounds: dungeon-only object sounds that play one held note of a wave nothing else
// uses (an Arbiter's Grounds spinner gear, the Snowpeak boss lock, the Temple of Time balance). While a
// song plays their waves are the song's stems, played at the note's own key so the pitch is right;
// otherwise they are the game's own again.
struct Slot { u16 index; u16 wave; u8 key; };
constexpr Slot kSlots[3] = {{449, 643, 93}, {454, 3828, 1}, {500, 638, 35}};
constexpr u32 kObjectGroup = 8;
u32 soundId(int s) { return (kObjectGroup << 16) | kSlots[s].index; }

constexpr int kLoopsPerSong = 2;      // each song goes round twice, then a short pause and another
constexpr int kPauseFrames = 150;
constexpr f32 kNear = 700.0f, kFar = 2600.0f;   // full volume this close to the fire, silent past far

enum Phase { kIdle, kLoading, kPlaying, kResting };
Phase g_phase = kIdle;
int g_song = -1;
int g_frames = 0;                     // into the current phase
int g_lastBard = -1000;               // frame the bard last played
int g_frame = 0;
int g_men = 0, g_women = 0;           // singing along this frame (counted up, used next frame)
int g_menNow = 0, g_womenNow = 0;
JGeometry::TVec3<f32> g_pos;          // the bard: where the music comes from
JAISoundHandle g_handle[3];
AudioWaveHandle g_wave[3] = {};
f32 g_vol[3] = {};
bool g_installed = false;

void unload() {
    for (int s = 0; s < 3; s++) {
        if (g_handle[s]) g_handle[s]->stop(20);
        if (g_wave[s] != 0) { svc_audio_res->remove_wave(mod_ctx, g_wave[s]); g_wave[s] = 0; }
        g_vol[s] = 0.0f;
    }
}

bool load(int song) {
    unload();
    for (int s = 0; s < 3; s++) {
        char path[96];
        std::snprintf(path, sizeof(path), "res/music/%s_%s.wav", kSongs[song].name, kStem[s]);
        AudioWaveInfo info{};
        info.base_key = kSlots[s].key;
        info.loop = true;
        info.loop_start_sample = 0;
        info.loop_end_sample = kSongs[song].samples;
        info.raw_wave = nullptr;
        if (svc_audio_res->replace_wave(mod_ctx, AUDIO_WAVE_BANK_SOUND_EFFECTS, kSlots[s].wave, path, &info, &g_wave[s]) != MOD_OK) {
            travellers::diagLine("music   could not load %s", path);
            unload();
            return false;
        }
    }
    travellers::diagLine("music   loaded %s", kSongs[song].name);
    return true;
}

int loopFrames(int song) { return static_cast<int>(kSongs[song].samples / 32000.0f * 30.0f); }

f32 distanceGain() {
    auto* link = dComIfGp_getPlayer(0);
    if (link == nullptr) return 0.0f;
    const f32 dx = link->current.pos.x - g_pos.x, dz = link->current.pos.z - g_pos.z;
    const f32 d = std::sqrt(dx * dx + dz * dz);
    if (d <= kNear) return 1.0f;
    if (d >= kFar) return 0.0f;
    const f32 t = (d - kNear) / (kFar - kNear);
    return (1.0f - t) * (1.0f - t);
}

// One singer at full voice; more add a little (they sing the same line), never doubling.
f32 choirGain(int n) { return n <= 0 ? 0.0f : std::min(1.15f, 0.85f + 0.12f * static_cast<f32>(n - 1)); }

void setVolumes() {
    const f32 d = distanceGain();
    const f32 want[3] = {d, d * choirGain(g_menNow), d * choirGain(g_womenNow)};
    for (int s = 0; s < 3; s++) {
        if (!g_handle[s]) continue;
        g_handle[s]->getAuxiliary().moveVolume(want[s], s == 0 ? 4 : 20);   // singers fade in and out
        g_vol[s] = want[s];
    }
}

bool female(int type) { return type == 20 || type == 27 || type == 28 || type == 29; }
}  // namespace

namespace camp_music {

bool isOurWave(unsigned wave) {
    for (const Slot& s : kSlots) if (s.wave == wave) return true;
    return false;
}

bool install() {
    if (svc_audio_res == nullptr) return false;
    // heard at any distance the mod chooses (it sets the volume by how far Link is from the fire),
    // never culled for priority
    AudioSoundTableEffectInfo info = *svc_audio_res->default_effect_info;
    info.always_max_priority = true;
    info.ignore_distance_volume = true;
    info.ignore_dolby = true;
    info.doppler_power = 0;
    info.random_pitch = 0;
    info.random_volume = 0;
    info.volume = 1.0f;
    info.pitch = 1.0f;
    for (const Slot& s : kSlots) {
        svc_audio_res->replace_sound_table_effect(mod_ctx, SE_CATEGORY_OBJECT_SE, s.index, &info, nullptr);
    }
    g_installed = true;
    return true;
}

void bardPlays(daNpcPasser_c* bard) {
    if (bard == nullptr) return;
    g_lastBard = g_frame;
    g_pos.set(bard->current.pos.x, bard->current.pos.y + 100.0f, bard->current.pos.z);
}

void singsAlong(daNpcPasser_c* singer) {
    if (singer == nullptr) return;
    if (female(singer->m_type)) ++g_women; else ++g_men;
}

void tick() {
    ++g_frame;
    g_menNow = g_men; g_womenNow = g_women;
    g_men = g_women = 0;
    if (!g_installed) return;
    const bool bard = g_frame - g_lastBard <= 10;
    if (!bard) {
        if (g_phase != kIdle) {
            travellers::diagLine("music   the bard stopped");
            unload();
            g_phase = kIdle;
        }
        return;
    }
    ++g_frames;
    switch (g_phase) {
    case kIdle:
    case kResting:
        if (g_phase == kResting && g_frames < kPauseFrames) return;
        {
            int next = static_cast<int>(cM_rndF(static_cast<f32>(kSongCount)));
            if (next >= kSongCount) next = kSongCount - 1;
            if (next == g_song) next = (next + 1) % kSongCount;
            g_song = next;
        }
        if (!load(g_song)) { g_phase = kResting; g_frames = 0; return; }
        g_phase = kLoading;
        g_frames = 0;
        return;
    case kLoading:
        if (g_frames < 3) return;   // the new waves take effect at the end of a frame
        for (int s = 0; s < 3; s++) {
            Z2GetAudioMgr()->startSound(JAISoundID(soundId(s)), &g_handle[s], &g_pos);
            if (g_handle[s]) g_handle[s]->getAuxiliary().moveVolume(s == 0 ? distanceGain() : 0.0f, 0);
        }
        travellers::diagLine("music   playing %s (%d %d %d)", kSongs[g_song].name, g_handle[0] ? 1 : 0,
                             g_handle[1] ? 1 : 0, g_handle[2] ? 1 : 0);
        g_phase = kPlaying;
        g_frames = 0;
        return;
    case kPlaying:
        setVolumes();
        if (g_frames >= loopFrames(g_song) * kLoopsPerSong) {
            unload();
            g_phase = kResting;
            g_frames = 0;
        }
        return;
    }
}

}  // namespace camp_music
