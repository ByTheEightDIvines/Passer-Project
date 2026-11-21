#include "global.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "camp_music.hpp"

#include "SSystem/SComponent/c_counter.h"
#include "SSystem/SComponent/c_math.h"
#include "Z2AudioLib/Z2AudioMgr.h"
#include "d/actor/d_a_npc_passer.h"
#include "d/d_com_inf_game.h"
#include "mods/service.hpp"
#include "mods/svc/audio_res.h"
#include "travellers.hpp"
#include "mod_sounds.hpp"


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

// Our own sounds (mod_sounds): guitar, men, women, on programs 0xE0..0xE2. Only the playing song's
// three stems are loaded (as added waves); nothing of the game's is replaced.
constexpr u8 kProgram0 = 0xE0;
// Every song's stems stay loaded from start-up (programs 0xE0 + song*3 + stem): loading one when the
// bard starts playing stalled the game for a moment.
u32 g_soundId[8][3] = {};

constexpr int kLoopsPerSong = 2;      // each song goes round twice, then a short pause and another
constexpr int kPauseFrames = 150;
constexpr f32 kNear = 700.0f, kFar = 2600.0f;   // full volume this close to the fire, silent past far

enum Phase { kIdle, kLoading, kPlaying, kResting };
Phase g_phase = kIdle;
int g_song = -1;
u32 soundId(int s) { return g_soundId[g_song < 0 ? 0 : g_song][s]; }
int g_frames = 0;                     // into the current phase (game frames)
// Timed in GAME frames (g_Counter): the mod's update can run more often than the game's frame, and
// the bard reports once per game frame, so counting updates made his reports look like gaps and cut the
// song off moments after it began. And a gap is forgiven for a while (he stands up a moment, a fight
// nearby, a frame the game skips him): the song carries on and picks up where it was.
constexpr u32 kGrace = 90;            // 3 s without a report before the song stops
u32 g_lastBard = 0;                   // game frame the bard last played
bool g_heardBard = false;
u32 g_lastCounter = 0xFFFFFFFFu;
int g_men = 0, g_women = 0;           // singing along this frame (counted up, used next frame)
int g_menNow = 0, g_womenNow = 0;
JGeometry::TVec3<f32> g_pos;          // the bard: where the music comes from
JAISoundHandle g_handle[3];
f32 g_vol[3] = {};
bool g_installed = false;

void unload() {   // stop playing (the songs stay loaded)
    for (int s = 0; s < 3; s++) {
        if (g_handle[s]) g_handle[s]->stop(20);
        g_vol[s] = 0.0f;
    }
}

bool g_ready[8] = {};
bool load(int song) {
    unload();
    if (!g_ready[song]) travellers::diagLine("music   %s isn't loaded", kSongs[song].name);
    return g_ready[song];
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

int g_loggedMen = -1, g_loggedWomen = -1;
void setVolumes() {
    if (g_menNow != g_loggedMen || g_womenNow != g_loggedWomen) {
        g_loggedMen = g_menNow; g_loggedWomen = g_womenNow;
        travellers::diagLine("music   singing: %d men, %d women", g_menNow, g_womenNow);
    }
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
    static_assert(kSongCount <= 8, "programs 0xE0.. hold 8 songs of 3 stems");
    int ready = 0;
    for (int song = 0; song < kSongCount; song++) {
        bool ok = true;
        for (int s = 0; s < 3 && ok; s++) {
            const u8 prg = static_cast<u8>(kProgram0 + song * 3 + s);
            char path[96];
            std::snprintf(path, sizeof(path), "res/music/%s_%s.wav", kSongs[song].name, kStem[s]);
            ok = mod_sounds::loadWave(prg, path, true, kSongs[song].samples);
            if (ok) {
                g_soundId[song][s] = mod_sounds::addSound(SE_CATEGORY_OBJECT_SE, prg, &info);
                ok = g_soundId[song][s] != 0;
            }
        }
        g_ready[song] = ok;
        ready += ok ? 1 : 0;
    }
    if (ready == 0) return false;
    g_installed = true;
    return true;
}

void bardPlays(daNpcPasser_c* bard) {
    if (bard == nullptr) return;
    g_lastBard = g_Counter.mCounter0;
    g_heardBard = true;
    ++g_men;                               // he sings as he plays
    g_pos.set(bard->current.pos.x, bard->current.pos.y + 100.0f, bard->current.pos.z);
}

void singsAlong(daNpcPasser_c* singer) {
    if (singer == nullptr) return;
    if (female(singer->m_type)) ++g_women; else ++g_men;
}

void tick() {
    const u32 now = g_Counter.mCounter0;
    if (now == g_lastCounter) return;     // once per game frame (the singers' counts are per game frame too)
    g_lastCounter = now;
    g_menNow = g_men; g_womenNow = g_women;
    g_men = g_women = 0;
    if (!g_installed) return;
    const bool bard = g_heardBard && now - g_lastBard <= kGrace;
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
        // (diagnosis, 2026-11-19: the songs start but nobody hears them) are the three sounds still
        // alive a moment in, and at what volume, with Link how far off?
        if (g_frames == 15 || g_frames == 90 || g_frames == 300 || (g_frames % 900) == 0) {
            auto* link = dComIfGp_getPlayer(0);
            const f32 d = link != nullptr ? std::sqrt((link->current.pos.x - g_pos.x) * (link->current.pos.x - g_pos.x) +
                                                      (link->current.pos.z - g_pos.z) * (link->current.pos.z - g_pos.z)) : -1.0f;
            travellers::diagLine("music   %s +%d: alive %d %d %d  vol %.2f %.2f %.2f  link %.0f off  ids %x %x %x",
                                 kSongs[g_song].name, g_frames, g_handle[0] ? 1 : 0, g_handle[1] ? 1 : 0,
                                 g_handle[2] ? 1 : 0, g_vol[0], g_vol[1], g_vol[2], d, soundId(0), soundId(1), soundId(2));
        }
        if (g_frames >= loopFrames(g_song) * kLoopsPerSong) {
            unload();
            g_phase = kResting;
            g_frames = 0;
        }
        return;
    }
}

}  // namespace camp_music
