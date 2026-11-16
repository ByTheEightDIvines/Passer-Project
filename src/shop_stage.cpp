#include "global.h"

#include <cstring>

#include "shop_stage.hpp"

#include "d/d_com_inf_game.h"
#include "d/d_stage.h"
#include "d/d_kankyo.h"
#include "mods/service.hpp"
#include "mods/svc/stage.h"
#include "travellers.hpp"


namespace {
constexpr const char* kShop = "R_PSR00";
constexpr s16 kReturnPoint = 0x1F4;          // our own spawn number in the field (the game's are 0..255)
constexpr f32 kShopDoorZ = 500.0f;            // the shop room's doorway (spawn 0 is just inside it)
constexpr f32 kShopDoorHalfW = 85.0f;
StageActorHandle g_point = 0;
int g_fieldRoom = -1;
char g_field[9] = {};                         // the field stage the tent stands in (F_SP121 or F_SP122)
int g_cool = 0;                               // frames before another trip (a stage change takes a moment)
int g_settle = 0;                             // frames since the shop loaded (no leaving straight away)

bool stageIs(const char* name) {
    const char* s = dComIfGp_getStartStageName();
    return s != nullptr && std::strcmp(s, name) == 0;
}
}  // namespace

namespace shop_stage {

bool install() { return svc_stage != nullptr; }

bool inShop() { return stageIs(kShop); }

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
    dComIfGp_setNextStage(kShop, 0, 0, -1);
}

void tick() {
    if (g_cool > 0) --g_cool;
    if (!inShop()) { g_settle = 0; return; }
    // the lamp flames (fireWd2, switch 0x20, lit while it's off): out by day, lit at night
    if (dKy_daynight_check()) dComIfGs_offSwitch(0x20, 0);
    else dComIfGs_onSwitch(0x20, 0);
    auto* link = dComIfGp_getPlayer(0);
    if (link == nullptr || dComIfGp_event_runCheck()) return;
    if (++g_settle < 30 || g_cool > 0) return;
    const cXyz& p = link->current.pos;
    if (p.z > kShopDoorZ - 20.0f && p.x > -kShopDoorHalfW && p.x < kShopDoorHalfW && g_fieldRoom >= 0 && g_field[0] != 0) {
        g_cool = 150;
        travellers::diagLine("shop    out of Beedle's shop to %s room %d", g_field, g_fieldRoom);
        dComIfGp_setNextStage(g_field, kReturnPoint, static_cast<s8>(g_fieldRoom), -1);
    }
}

}  // namespace shop_stage
