#include "global.h"

#include <cstring>

#include "mod_sounds.hpp"

#include "JSystem/JAudio2/JASBasicBank.h"
#include "JSystem/JAudio2/JASBasicInst.h"
#include "JSystem/JAudio2/JASBasicWaveBank.h"
#include "JSystem/JAudio2/JASChannel.h"
#include "JSystem/JAudio2/JASOscillator.h"
#include "JSystem/JAudio2/JAUSeqCollection.h"
#include "mods/service.hpp"
#include "mods/svc/audio_res.h"
#include "mods/svc/hook.hpp"

IMPORT_SERVICE(AudioResService, svc_audio_res);

namespace {
constexpr u8 kBank = 0x35;             // a sound-effect bank (its programs stop at 0x86)
constexpr u8 kKey = 0x3C;              // the note, at the waves' own base key: no pitch change
constexpr int kMaxSounds = 112;
constexpr u32 kRoot0 = 0x60;           // after the two shared routines
struct Sound { u32 id; u8 program; };
Sound g_sounds[kMaxSounds];
int g_count = 0;
u16 g_wave[256] = {};                  // per program: the added wave's ID (0: none)
AudioWaveHandle g_handle[256] = {};
int g_answered = 0;
// The sequences: the game's own held single-note layout. The two shared routines sit where the
// game's sound-effect collection keeps them, so the calls and jumps read the same: 0x30 the prologue,
// 0x4D "wait until done, then end". Each sound: its root track (opens the note track, then the tail)
// and, 0x10 after it, its note track (prologue, instrument, the held note).
alignas(32) u8 g_seq[kRoot0 + 0x20 * kMaxSounds] = {};

void writeSeq(int i, u8 program) {
    u8* r = g_seq + kRoot0 + 0x20 * i;
    const u32 sub = kRoot0 + 0x20 * i + 0x10;
    const u8 root[] = {0xc1, 0x00, static_cast<u8>(sub >> 16), static_cast<u8>(sub >> 8), static_cast<u8>(sub), 0xc7, 0x00, 0x00, 0x4d};
    const u8 note[] = {0xc3, 0x00, 0x00, 0x30, 0xe1, kBank, program, kKey, 0x00, 0x7f, 0x00, 0xff};
    std::memcpy(r, root, sizeof(root));
    std::memcpy(r + 0x10, note, sizeof(note));
}

DEFINE_HOOK_SYMBOL("JAUSeqDataMgr_SeqCollection::getSeqData",
                   JAISeqDataMgr::SeqDataReturnValue(JAUSeqDataMgr_SeqCollection*, JAISoundID, JAISeqData*), SeqLookup);
void onSeqLookupPost(ModContext*, void* args, void* retval, void*) {
    const u32 id = static_cast<u32>(mods::arg<JAISoundID>(args, 1));
    for (int i = 0; i < g_count; i++) {
        if (g_sounds[i].id != id) continue;
        mods::arg<JAISeqData*>(args, 2)->set(g_seq, kRoot0 + 0x20 * i);
        *static_cast<JAISeqDataMgr::SeqDataReturnValue*>(retval) = JAISeqDataMgr::SeqDataReturnValue_2;
        return;
    }
}

// The envelope every note needs: full volume at once, a quick fade on release. The game's own plain
// notes use this one (JASBank::noteOnOsc); without an envelope a note never reports itself finished, so
// its audio channel is never freed and the mixer runs out of them.
const JASOscillator::Point kRelease[2] = {{0x0001, 0x000A, 0x0000}, {0x000F, 0x0000, 0x0000}};
const JASOscillator::Data kEnvelope = {0, 1.0f, nullptr, kRelease, 1.0f, 0.0f};
const JASOscillator::Data* kOscillators[2] = {&kEnvelope, nullptr};

DEFINE_HOOK_SYMBOL("JASBasicBank::getInstParam", bool(const JASBasicBank*, int, int, int, JASInstParam*), InstLookup);
void onInstLookupPost(ModContext*, void* args, void* retval, void*) {
    if (*static_cast<bool*>(retval)) return;          // the game has an instrument there: never ours
    const int prg = mods::arg<int>(args, 1);
    if (prg < 0 || prg > 0xFF || g_wave[prg] == 0) return;
    auto* p = mods::arg<JASInstParam*>(args, 4);
    p->init();
    p->mWaveId = g_wave[prg];
    p->mChannelType = CHANNEL_WAVE;
    p->mOscillators = const_cast<JASOscillator::Data**>(kOscillators);
    p->mOscillatorCount = 1;
    p->mDirectRelease = 0;
    p->mDontSetKey = 0;
    *static_cast<bool*>(retval) = true;
}

bool ours(u32 wave) {
    if (wave == 0) return false;
    for (u16 w : g_wave) if (w == wave) return true;
    return false;
}
DEFINE_HOOK(&JASBasicWaveBank::getWaveHandle, WaveHandleLookup);
void onWaveHandlePost(ModContext*, void* args, void* retval, void*) {
    auto** out = static_cast<JASWaveHandle**>(retval);
    auto* bank = mods::arg<const JASBasicWaveBank*>(args, 0);
    const u32 no = mods::arg<u32>(args, 1);
    if (out == nullptr || *out != nullptr || bank == nullptr || bank->bankId != 0) return;
    if (bank->mWaveTable == nullptr || bank->mHandleCount == 0 || !ours(no)) return;
    // an added wave (its ID is past the end of the game's table): any real entry will do, the engine
    // then plays the added wave registered under this ID
    *out = bank->mWaveTable;
    ++g_answered;
}
}  // namespace

namespace mod_sounds {

bool install() {
    if (svc_audio_res == nullptr) return false;
    const u8 prologue[] = {0xd8, 0x69, 0x19, 0x99, 0xea, 0x00, 0xff, 0xff, 0xd8, 0x6b, 0x00, 0x78, 0xb8, 0x02, 0x00, 0xc5};
    const u8 tail[] = {0xda, 0x03, 0x60, 0x00, 0x00, 0xc8, 0x01, 0x00, 0x00, 0x5d, 0xf0, 0x01, 0xc7, 0x00, 0x00, 0x4d, 0xff};
    std::memcpy(g_seq + 0x30, prologue, sizeof(prologue));
    std::memcpy(g_seq + 0x4D, tail, sizeof(tail));
    if (mods::hook::add_post<SeqLookup>(onSeqLookupPost) != MOD_OK) return false;
    if (mods::hook::add_post<InstLookup>(onInstLookupPost) != MOD_OK) return false;
    if (mods::hook::add_post<WaveHandleLookup>(onWaveHandlePost) != MOD_OK) return false;
    return true;
}

u32 addSound(u8 category, u8 program, const AudioSoundTableEffectInfo* given) {
    if (g_count >= kMaxSounds || program < 0x88 || program > 0xFF) return 0;
    AudioSoundTableEffectInfo info = given != nullptr ? *given : *svc_audio_res->default_effect_info;
    u16 idx = 0;
    if (svc_audio_res->add_sound_table_effect(mod_ctx, static_cast<SoundEffectCategory>(category), &info, nullptr, &idx) != MOD_OK) {
        return 0;
    }
    const u32 id = (static_cast<u32>(category) << 16) | idx;
    writeSeq(g_count, program);
    g_sounds[g_count++] = {id, program};
    return id;
}

bool loadWave(u8 program, const char* path, bool loop, u32 loopEnd) {
    unloadWave(program);
    AudioWaveInfo info{};
    info.base_key = kKey;
    info.loop = loop;
    info.loop_start_sample = 0;
    info.loop_end_sample = loop ? loopEnd : 0xFFFFFFFFu;
    info.raw_wave = nullptr;
    u16 wave = 0;
    if (svc_audio_res->add_wave(mod_ctx, AUDIO_WAVE_BANK_SOUND_EFFECTS, path, &info, &g_handle[program], &wave) != MOD_OK) {
        g_handle[program] = 0;
        return false;
    }
    g_wave[program] = wave;
    return true;
}

void unloadWave(u8 program) {
    if (g_handle[program] != 0) svc_audio_res->remove_wave(mod_ctx, g_handle[program]);
    g_handle[program] = 0;
    g_wave[program] = 0;
}

bool hasWave(u8 program) { return g_wave[program] != 0; }
int lookupsAnswered() { return g_answered; }

}  // namespace mod_sounds
