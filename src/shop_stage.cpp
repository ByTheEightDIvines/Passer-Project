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
#include "mods/svc/audio_res.h"
#include "Z2AudioLib/Z2AudioMgr.h"
#include "d/d_particle_name.h"


namespace {
constexpr const char* kShop = "R_PSR00";
constexpr s16 kReturnPoint = 0x1F4;          // our own spawn number in the field (the game's are 0..255)
constexpr f32 kShopDoorZ = 500.0f;            // the shop room's doorway, in the shop's own frame
// The shop is room 0 of its stage (tools/stage/build_shop_kak.py: the Kakariko interior base, with the
// field's sky, time-of-day lighting and terrain), standing where Beedle's tent stands: its own frame is
// the tent's, at this spot and facing.
constexpr int kShopRoom = 0;
constexpr f32 kShopX = -72921.4f, kShopZ = 5115.4f;
constexpr s16 kShopYaw = 9935;
constexpr f32 kShopDoorHalfW = 85.0f;
StageActorHandle g_point = 0;
int g_fieldRoom = -1;
char g_field[9] = {};                         // the field stage the tent stands in (F_SP121 or F_SP122)
int g_cool = 0;                               // frames before another trip (a stage change takes a moment)
int g_settle = 0;                             // frames since the shop loaded (no leaving straight away)

// Beedle's shop music: a game music stream of our own (res: overlay/Audiores/Stream/psr_beedle.ast, made by
// tools/music/make_ast.py), added to the sound table with the audio service's add_sound_table_stream (a
// new ID, nothing of the game's replaced) and played as the game plays its own streamed music
// (Z2SeqMgr bgmStreamPrepare / bgmStreamPlay). It loops the whole track and stops on a stage change.
constexpr const char* kMusicPath = "AudioRes/Stream/psr_beedle.ast";   // as the game's sound table spells them
u16 g_streamId = 0;
bool g_streamOk = false;
bool g_musicOn = false;
u32 musicSoundId() { return 0x02000000u | g_streamId; }    // section 2 (streams), the allocated ID

// The three hanging lanterns' flames, at night: Link's own lantern fire (what the travellers' lanterns
// burn), sized to the shop's lanterns (Obj_kntr at 1.6), each with the warm point light the game's lamp
// flame (fireWd2) gives (its colour and strength: that lit the room right). The scene's lighting clears
// its point lights when it is made and torn down (d_kankyo plight_init), so they are added again every
// night frame (dKy_plight_set never adds one twice) and only taken out by us while still in the shop.
constexpr f32 kShopY = -1551.7f;
constexpr f32 kLanternFlameY = 330.0f + 23.0f * 1.6f;   // the lantern's base, plus the lantern flame's height on it
constexpr f32 kLanternZ[3] = {-330.0f, 0.0f, 330.0f};
constexpr f32 kLanternFlameScale = 1.3f;
u32 g_flame[3] = {};
LIGHT_INFLUENCE g_glow[3] = {};
bool lightIsOurs(LIGHT_INFLUENCE& l) {
    const int i = (l.mIndex < 0 ? -l.mIndex : l.mIndex) & 0xFFF;
    return i >= 1 && i <= 50 && g_env_light.pointlight[i - 1] == &l;
}
void lanternTick(bool inside) {
    const bool lit = inside && dKy_daynight_check();
    const f32 c = cM_scos(kShopYaw), sn = cM_ssin(kShopYaw);
    for (int i = 0; i < 3; ++i) {
        if (!lit) {
            if (inside && lightIsOurs(g_glow[i])) dKy_plight_cut(&g_glow[i]);   // daybreak in the shop
            g_glow[i].mIndex = 0;
            g_flame[i] = 0;
            continue;
        }
        const f32 lz = kLanternZ[i];
        const cXyz flame(kShopX + lz * sn, kShopY + kLanternFlameY, kShopZ + lz * c);   // local (0, y, lz) in the world
        const cXyz scale(kLanternFlameScale, kLanternFlameScale, kLanternFlameScale);
        g_flame[i] = dComIfGp_particle_set(g_flame[i], ID_ZI_J_KANTERA_FIRE, &flame, nullptr, nullptr, &scale,
                                           0xFF, nullptr, -1, nullptr, nullptr, nullptr);
        g_glow[i].mPosition.set(flame.x, flame.y + 10.0f, flame.z);
        g_glow[i].mColor.r = 0xBC;
        g_glow[i].mColor.g = 0x66;
        g_glow[i].mColor.b = 0x42;
        g_glow[i].mPow = 500.0f;
        g_glow[i].mFluctuation = 1.0f;
        dKy_plight_set(&g_glow[i]);
    }
}

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
    AudioSoundTableStreamInfo info = *svc_audio_res->default_stream_info;   // stereo: left, right
    info.volume = 1.0f;
    info.stop_on_scene_change = true;
    AudioSoundTableHandle handle = 0;
    g_streamOk = svc_audio_res->add_sound_table_stream(mod_ctx, kMusicPath, &info, &handle, &g_streamId) == MOD_OK;
    return g_streamOk;
}

// The game's music manager is the audio manager itself (class Z2AudioMgr : ... public Z2SeqMgr), reached
// through Z2AudioMgr::getInterface() (exported game data), as the game's own mDoAud_bgmStream* do. Not
// Z2GetSeqMgr(): its JASGlobalInstance<Z2SeqMgr> is a template global, so in a mod it is the mod's own
// (empty) copy, not the game's.
Z2SeqMgr* seqMgr() { return Z2AudioMgr::getInterface(); }

void musicTick(bool inside, bool settled) {
    if (!g_streamOk) return;
    Z2SeqMgr* seq = seqMgr();
    if (seq == nullptr) return;
    if (!inside) {
        if (g_musicOn && seq->getStreamBgmID() == musicSoundId()) seq->bgmStreamStop(30);
        g_musicOn = false;
        return;
    }
    if (g_musicOn || !settled) return;   // once the shop has settled (the game brings its sound back up after a stage change)
    g_musicOn = true;
    seq->bgmStreamPrepare(musicSoundId());
    seq->bgmStreamPlay();
    travellers::diagLine("shop    music stream %04x started", g_streamId);
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
    lanternTick(inShop());
    if (!inShop()) { g_settle = 0; return; }
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
