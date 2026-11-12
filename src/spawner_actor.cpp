#include "global.h"

#include "spawner_actor.hpp"
#include "travellers.hpp"

#include "d/d_stage.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_draw_priority.h"
#include "mods/service.hpp"
#include "mods/svc/log.h"
#include "mods/svc/stage.h"

#include <cstring>

cPhs_Step psrSpawner_c::create() {
    fopAcM_ct(this, psrSpawner_c);
    mScattered = false;
    mTick = 0;
    return cPhs_COMPLEATE_e;
}

int psrSpawner_c::Execute() {
    ++mTick;
    if (travellers::spawnerTick(this, !mScattered)) {
        mScattered = true;
    }
    return 1;
}

namespace {
cPhs_Step psrSpawner_Create(void* i_this) { return static_cast<psrSpawner_c*>(i_this)->create(); }
int psrSpawner_Delete(void* i_this) {
    static_cast<psrSpawner_c*>(i_this)->~psrSpawner_c();
    return 1;
}
int psrSpawner_Execute(void* i_this) { return static_cast<psrSpawner_c*>(i_this)->Execute(); }
int psrSpawner_IsDelete(void*) { return 1; }
int psrSpawner_Draw(void*) { return 1; }   // nothing to draw
}  // namespace

s16 psrSpawner_c::sProcName = -1;
ActorHandle psrSpawner_c::sActorHandle = -1;
const ActorProfileDesc psrSpawner_c::sProfile = {
    .name = PSR_SPAWNER_NAME,
    .priority_group = 7,
    .process_size = sizeof(psrSpawner_c),
    .draw_priority = fpcDwPi_PASSER_MNG_e,   // same slot as the vanilla passer spawner
    .status = fopAcStts_UNK_0x40000_e,       // no culling: it must run every tick its room is loaded
    .group = fopAc_ACTOR_e,
    .cull_type = fopAc_CULLBOX_0_e,
    .create_function = psrSpawner_Create,
    .delete_function = psrSpawner_Delete,
    .execute_function = psrSpawner_Execute,
    .is_delete_function = psrSpawner_IsDelete,
    .draw_function = psrSpawner_Draw,
};

namespace psr_spawner {
ModResult registerAndPlace() {
    ModResult r = svc_actor->register_actor(mod_ctx, &psrSpawner_c::sProfile, &psrSpawner_c::sProcName,
                                            &psrSpawner_c::sActorHandle);
    if (r != MOD_OK) {
        return r;
    }
    // One spawner per room the roads pass through, in the room's own actor list (all layers).
    for (int i = 0;; ++i) {
        travellers::SpawnerSpot spot{};
        if (!travellers::spawnerSpot(i, &spot)) {
            break;
        }
        stage_actor_data_class record{};
        std::strncpy(record.name, PSR_SPAWNER_NAME, sizeof(record.name));
        record.base.parameters = 0;
        // High above the room: well clear of any of the game's "hider" (suspend) boxes, which would
        // otherwise freeze it. It has no collision and never moves.
        record.base.position = cXyz(spot.x, spot.y + 20000.0f, spot.z);
        record.base.angle = csXyz(0, 0, 0);
        record.base.setID = 0xFFFF;
        r = svc_stage->add_actor(mod_ctx, spot.stage, static_cast<uint8_t>(spot.room), -1, &record,
                                 sizeof(record), nullptr);
        if (r != MOD_OK) {
            return r;
        }
    }
    return MOD_OK;
}
}  // namespace psr_spawner
