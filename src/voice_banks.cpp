#include "global.h"

#include "voice_banks.hpp"

#include "Z2AudioLib/Z2SceneMgr.h"
#include "mods/service.hpp"
#include "mods/svc/hook.hpp"
#include "travellers.hpp"

namespace {
// Castle Town's two sound-effect banks (Z2SeWave_66/67): the townsfolk voices and the band.
constexpr u8 kBanks[] = {0x42, 0x43};
bool g_ours[sizeof(kBanks)] = {};   // loaded by us (not by the area itself)
int g_tries[sizeof(kBanks)] = {};   // failed loads this area (no room: give up, don't retry every frame)
constexpr int kMaxTries = 3;
int g_lastStatus[sizeof(kBanks)] = {-1, -1};   // for the diary: logged when it changes

bool sceneWants(Z2SceneMgr* mgr, u8 bank) {
    return mgr->requestSeWave_1 == bank || mgr->requestSeWave_2 == bank || mgr->loadedSeWave_1 == bank ||
           mgr->loadedSeWave_2 == bank;
}

void letGo(Z2SceneMgr* mgr) {
    for (size_t i = 0; i < sizeof(kBanks); ++i) {
        g_tries[i] = 0;
        g_lastStatus[i] = -1;
        if (!g_ours[i]) continue;
        g_ours[i] = false;
        if (!sceneWants(mgr, kBanks[i])) mgr->eraseSeWave(kBanks[i]);
    }
}

DEFINE_HOOK(&Z2SceneMgr::load1stDynamicWave, SceneLoadWaves);

HookAction onLoadWavesPre(ModContext*, void* args, void*, void*) {
    if (auto* mgr = mods::arg<Z2SceneMgr*>(args, 0)) letGo(mgr);
    return HOOK_CONTINUE;
}
}  // namespace

namespace voice_banks {

void tick() {
    Z2SceneMgr* mgr = Z2GetSceneMgr();
    if (mgr == nullptr || !travellers::inNetworkStage()) return;
    if (mgr->load1stWait != 0) return;   // the area's own sounds are still coming in
    for (size_t i = 0; i < sizeof(kBanks); ++i) {
        const int st = mgr->getSeLoadStatus(kBanks[i]);
        if (st != g_lastStatus[i]) {
            travellers::diagLine("voice   bank 0x%02x status %d (scene wants %d, ours %d, tries %d)", kBanks[i], st,
                             sceneWants(mgr, kBanks[i]) ? 1 : 0, g_ours[i] ? 1 : 0, g_tries[i]);
            g_lastStatus[i] = st;
        }
        if (st != 0) continue;   // loaded (or loading) already
        if (sceneWants(mgr, kBanks[i]) || g_tries[i] >= kMaxTries) continue;
        if (mgr->loadSeWave(kBanks[i])) {
            g_ours[i] = true;
            travellers::diagLine("voice   bank 0x%02x load started", kBanks[i]);
        } else {
            ++g_tries[i];
            travellers::diagLine("voice   bank 0x%02x load refused (try %d)", kBanks[i], g_tries[i]);
        }
    }
}

bool install() { return mods::hook::add_pre<SceneLoadWaves>(onLoadWavesPre) == MOD_OK; }

}  // namespace voice_banks
