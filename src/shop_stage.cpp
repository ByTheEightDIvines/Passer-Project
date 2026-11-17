#include "global.h"

#include <cstring>

#include "shop_stage.hpp"

#include "SSystem/SComponent/c_math.h"
#include "d/d_com_inf_game.h"
#include "d/d_stage.h"
#include "d/d_kankyo.h"
#include "mods/service.hpp"
#include "mods/svc/stage.h"
#include "travellers.hpp"
#include "mod_sounds.hpp"
#include "mods/svc/audio_res.h"
#include "Z2AudioLib/Z2AudioMgr.h"


namespace {
constexpr const char* kShop = "R_PSR00";
constexpr s16 kReturnPoint = 0x1F4;          // our own spawn number in the field (the game's are 0..255)
constexpr f32 kShopDoorZ = 500.0f;            // the shop room's doorway, in the shop's own frame
// The shop is room 8 of a copy of the field (F_SP122's settings), standing where Beedle's tent stands
// (tools/stage/build_shop_field.py): its own frame is the tent's, at this spot and facing.
constexpr int kShopRoom = 8;
constexpr f32 kShopX = -72921.4f, kShopZ = 5115.4f;
constexpr s16 kShopYaw = 9935;
constexpr f32 kShopDoorHalfW = 85.0f;
StageActorHandle g_point = 0;
int g_fieldRoom = -1;
char g_field[9] = {};                         // the field stage the tent stands in (F_SP121 or F_SP122)
int g_cool = 0;                               // frames before another trip (a stage change takes a moment)
int g_settle = 0;                             // frames since the shop loaded (no leaving straight away)

// Beedle's shop music: our own added sound (mod_sounds program 0xF5), looping the whole track while
// Link is inside, following him so it never pans; faded out on the way out.
constexpr u8 kMusicProgram = 0xF5;
constexpr u32 kMusicSamples = 1547928;        // res/music/beedle_shop.wav, 22.05 kHz mono (under 2^21 samples, like the campfire songs)
constexpr f32 kMusicVolume = 0.8f;
u32 g_musicId = 0;

JAISoundHandle g_music;
JGeometry::TVec3<f32> g_musicPos;

bool stageIs(const char* name) {
    const char* s = dComIfGp_getStartStageName();
    return s != nullptr && std::strcmp(s, name) == 0;
}
}  // namespace

namespace shop_stage {

bool install() { return svc_stage != nullptr; }

bool inShop() { return stageIs(kShop); }

bool installMusic() {
    if (svc_audio_res == nullptr) return false;
    if (!mod_sounds::loadWave(kMusicProgram, "res/music/beedle_shop.wav", true, kMusicSamples)) return false;
    AudioSoundTableEffectInfo info = *svc_audio_res->default_effect_info;
    info.always_max_priority = true;
    info.ignore_distance_volume = true;
    info.ignore_dolby = true;
    info.doppler_power = 0;
    info.random_pitch = 0;
    info.random_volume = 0;
    info.volume = 1.0f;
    info.pitch = 1.0f;
    g_musicId = mod_sounds::addSound(SE_CATEGORY_OBJECT_SE, kMusicProgram, &info);
    return g_musicId != 0;
}

int g_musicLog = 0;
void musicTick(bool inside, bool settled) {
    if (g_musicId == 0) return;
    if (!inside) {
        if (g_music) g_music->stop(30);
        return;
    }
    if (!settled) return;   // wait until the shop has settled (the game brings its sound back up after a stage change)
    if (++g_musicLog >= 150) {   // every 5 seconds: is it still going, and how loud
        g_musicLog = 0;
        // the three steps of playing one of our sounds, counted since start-up: its note sequence,
        // its instrument, its wave. Whichever stops rising while the music "plays" is where it breaks.
        travellers::diagLine("shop    music %s volume %.2f | sequences %d instruments %d waves %d",
                             g_music ? "still playing" : "stopped",
                             g_music ? g_music->getAuxiliary().params_.mVolume : 0.0f, mod_sounds::seqAnswered(),
                             mod_sounds::instAnswered(), mod_sounds::lookupsAnswered());
    }
    if (auto* link = dComIfGp_getPlayer(0)) {
        g_musicPos.set(link->current.pos.x, link->current.pos.y + 100.0f, link->current.pos.z);
    }
    if (!g_music) {
        Z2GetAudioMgr()->startSound(JAISoundID(g_musicId), &g_music, &g_musicPos);
        if (g_music) g_music->getAuxiliary().moveVolume(kMusicVolume, 0);
        travellers::diagLine("shop    music %s", g_music ? "playing" : "did not start");
    }
}

void enter(const cXyz& doorOut, s16 yawOut, int fieldRoom) {
    if (g_cool > 0 || svc_stage == nullptr || fieldRoom < 0) return;
    const char* here = dComIfGp_getStartStageName();
    if (here == nullptr) return;
    char field[9] = {};
    std::strncpy(field, here, 8);
    // the way back: a spawn point of ours in front of the tent (a plain standing spawn, like the game's)
    if (g_point != 0) { svc_stage->remove_actor_edit(mod_ctx, g_point); g_point = 0; }
    stage_actor_data_class rec{};
    std::memcpy(rec.name, "Link", 5);
    rec.base.parameters = 0xFF00003F;
    rec.base.position = doorOut;
    rec.base.angle = csXyz(0, yawOut, kReturnPoint);
    rec.base.setID = 0xFFFF;
    if (svc_stage->add_actor(mod_ctx, field, static_cast<u8>(fieldRoom), -1, &rec, sizeof(rec), &g_point) != MOD_OK) {
        travellers::diagLine("shop    could not add the way back; not going in");
        g_point = 0;
        return;
    }
    g_fieldRoom = fieldRoom;
    std::memcpy(g_field, field, sizeof(g_field));
    g_cool = 150;
    travellers::diagLine("shop    into Beedle's shop (back to %s room %d)", g_field, fieldRoom);
    dComIfGp_setNextStage(kShop, 0, kShopRoom, -1);
}

void tick() {
    if (g_cool > 0) --g_cool;
    musicTick(inShop(), g_settle >= 30);
    if (!inShop()) { g_settle = 0; return; }
    // the lamp flames (fireWd2, switch 0x20, lit while it's off): out by day, lit at night
    if (dKy_daynight_check()) dComIfGs_offSwitch(0x20, kShopRoom);
    else dComIfGs_onSwitch(0x20, kShopRoom);
    auto* link = dComIfGp_getPlayer(0);
    if (link == nullptr || dComIfGp_event_runCheck()) return;
    if (++g_settle < 30 || g_cool > 0) return;
    // Link in the shop's own frame (x across, z toward the doorway)
    const f32 dx = link->current.pos.x - kShopX, dz = link->current.pos.z - kShopZ;
    const f32 c = cM_scos(kShopYaw), sn = cM_ssin(kShopYaw);
    const f32 lx = dx * c - dz * sn, lz = dx * sn + dz * c;
    if (lz > kShopDoorZ - 20.0f && lx > -kShopDoorHalfW && lx < kShopDoorHalfW && g_fieldRoom >= 0 && g_field[0] != 0) {
        g_cool = 150;
        travellers::diagLine("shop    out of Beedle's shop to %s room %d", g_field, g_fieldRoom);
        dComIfGp_setNextStage(g_field, kReturnPoint, static_cast<s8>(g_fieldRoom), -1);
    }
}

}  // namespace shop_stage
