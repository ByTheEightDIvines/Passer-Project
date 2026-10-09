#include "global.h"

#include "d/actor/d_a_npc_passer.h"
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "d/d_particle_name.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_name.h"
#include "mods/service.hpp"
#include "mods/svc/hook.hpp"
#include "mods/svc/log.h"

#include <array>
#include <cmath>
#include <cstring>

DEFINE_MOD();
IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(LogService, svc_log);

namespace {

constexpr s8 kFieldRoom = 5;
constexpr int kPasserType = 8;  // MAN_a2
constexpr f32 kThreatRadius = 900.0f;
constexpr int kStartingHealth = 3;
constexpr int kFallFrames = 72;

struct PasserState {
    daNpcPasser_c* actor = nullptr;
    int health = kStartingHealth;
    int fallFrame = 0;
    bool dead = false;
};

std::array<PasserState, 16> g_states{};

bool inPrototypeScope(const fopAc_ac_c* actor) {
    const char* stage = dComIfGp_getStartStageName();
    return actor != nullptr && stage != nullptr && std::strcmp(stage, "F_SP121") == 0 &&
           fopAcM_GetRoomNo(actor) == kFieldRoom &&
           fopAcM_GetName(const_cast<fopAc_ac_c*>(actor)) == fpcNm_NPC_PASSER_e &&
           (fopAcM_GetParam(actor) & 0xFF) == kPasserType;
}

PasserState* findState(daNpcPasser_c* actor, bool create) {
    for (auto& state : g_states) {
        if (state.actor == actor) {
            return &state;
        }
    }
    if (create) {
        for (auto& state : g_states) {
            if (state.actor == nullptr) {
                state = PasserState{};
                state.actor = actor;
                return &state;
            }
        }
    }
    return nullptr;
}

struct EnemySearch {
    daNpcPasser_c* passer;
    bool found;
};

void* findNearbyEnemy(void* candidate, void* context) {
    auto* search = static_cast<EnemySearch*>(context);
    if (search == nullptr || search->found || !fopAcM_IsActor(candidate)) {
        return nullptr;
    }

    auto* enemy = static_cast<fopAc_ac_c*>(candidate);
    if (fopAcM_GetGroup(enemy) != fopAc_ENEMY_e ||
        fopAcM_GetRoomNo(enemy) != fopAcM_GetRoomNo(search->passer)) {
        return nullptr;
    }

    const cXyz& a = search->passer->current.pos;
    const cXyz& b = enemy->current.pos;
    const f32 dx = a.x - b.x;
    const f32 dz = a.z - b.z;
    if (dx * dx + dz * dz <= kThreatRadius * kThreatRadius) {
        search->found = true;
        return candidate;
    }
    return nullptr;
}

bool enemyNearby(daNpcPasser_c* passer) {
    EnemySearch search{passer, false};
    fpcM_Search(findNearbyEnemy, &search);
    return search.found;
}

void setDamageable(daNpcPasser_c* passer) {
    // Passing the type mask 0xD8FBFDFF enables normal weapon attacks to register against this
    // cylinder. SetTgType takes a u32 directly in the local Dusk headers.
    passer->mCyl.SetTgType(0xD8FBFDFF);
    passer->mCyl.OnTgSetBit();
    passer->mCyl.SetTgHitMark(CcG_Tg_UNK_MARK_1);
    passer->mCyl.SetTgSe(dCcD_SE_SOFT_BODY);
}

void beginDeath(daNpcPasser_c* passer, PasserState& state) {
    state.dead = true;
    state.fallFrame = 0;
    passer->health = 0;
    passer->attention_info.flags = 0;
    passer->mCyl.OffTgSetBit();
    passer->speedF = 0.0f;
    passer->speed.set(0.0f, 0.0f, 0.0f);

    const cXyz scale(1.0f, 1.0f, 1.0f);
    dComIfGp_particle_set(static_cast<u16>(dPa_RM(ID_ZF_S_PODEATH00SMK)),
                          &passer->current.pos, &passer->shape_angle, &scale);
    dComIfGp_particle_set(static_cast<u16>(dPa_RM(ID_ZF_S_PODEATH02SP)),
                          &passer->current.pos, &passer->shape_angle, &scale);
}

DEFINE_HOOK(&daNpcPasser_c::create_init, PasserCreateInit);
DEFINE_HOOK(&daNpcPasser_c::setCollision, PasserSetCollision);
DEFINE_HOOK(&daNpcPasser_c::setBaseMtx, PasserSetBaseMtx);
DEFINE_HOOK(&daNpcPasser_c::execute, PasserExecute);
DEFINE_HOOK(&daNpcCd2_c::checkFearSituation, PasserFearCheck);

void onCreateInitPost(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    if (!inPrototypeScope(passer)) {
        return;
    }
    auto* state = findState(passer, true);
    if (state == nullptr) {
        return;
    }

    passer->health = kStartingHealth;
    // Preserve vanilla talk/speak attention and add battle lock-on attention.
    passer->attention_info.flags |= fopAc_AttnFlag_BATTLE_e;
    passer->attention_info.distances[fopAc_attn_BATTLE_e] = 180;
    setDamageable(passer);
}

void onSetCollisionPost(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    if (inPrototypeScope(passer)) {
        if (auto* state = findState(passer, false); state != nullptr && !state->dead) {
            setDamageable(passer);
        }
    }
}

void onBaseMtxPost(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    auto* state = findState(passer, false);
    if (!inPrototypeScope(passer) || state == nullptr || !state->dead || passer->mpMorf == nullptr) {
        return;
    }

    const int progress = state->fallFrame < 36 ? state->fallFrame : 36;
    const s16 roll = static_cast<s16>(progress * (0x4000 / 30));
    mDoMtx_stack_c::transS(passer->current.pos.x, passer->current.pos.y, passer->current.pos.z);
    mDoMtx_stack_c::YrotM(passer->shape_angle.y);
    mDoMtx_stack_c::ZrotM(roll);
    passer->mpMorf->getModel()->setBaseTRMtx(mDoMtx_stack_c::get());
    passer->mpMorf->modelCalc();
}

HookAction onExecutePre(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    auto* state = findState(passer, false);
    if (!inPrototypeScope(passer) || state == nullptr) {
        return HOOK_CONTINUE;
    }

    if (state->dead) {
        ++state->fallFrame;
        if (state->fallFrame >= kFallFrames) {
            fopAcM_delete(passer);
            state->actor = nullptr;
        }
        return HOOK_SKIP_ORIGINAL;
    }

    if (passer->mCyl.ChkTgHit()) {
        passer->mCyl.ResetTgHit();
        --state->health;
        passer->health = static_cast<s16>(state->health);
        if (state->health <= 0) {
            beginDeath(passer, *state);
        } else if (enemyNearby(passer)) {
            passer->setAction(daNpcPasser_c::MODE_1);
        }
    }
    return HOOK_CONTINUE;
}

void onFearCheckPost(ModContext*, void* args, void* retval, void*) {
    auto* passer = static_cast<daNpcPasser_c*>(mods::arg<daNpcCd2_c*>(args, 0));
    if (retval != nullptr && inPrototypeScope(passer) && enemyNearby(passer)) {
        *static_cast<bool*>(retval) = true;
    }
}

}  // namespace

extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError*) {
    if (mods::hook::add_post<PasserCreateInit>(onCreateInitPost) != MOD_OK ||
        mods::hook::add_post<PasserSetCollision>(onSetCollisionPost) != MOD_OK ||
        mods::hook::add_post<PasserSetBaseMtx>(onBaseMtxPost) != MOD_OK ||
        mods::hook::add_pre<PasserExecute>(onExecutePre) != MOD_OK ||
        mods::hook::add_post<PasserFearCheck>(onFearCheckPost) != MOD_OK) {
        svc_log->error(mod_ctx, "Hyrule Field passer prototype hook registration failed");
        return MOD_UNAVAILABLE;
    }
    svc_log->info(mod_ctx, "Hyrule Field room 5 passer prototype initialized");
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) { return MOD_OK; }
MOD_EXPORT ModResult mod_shutdown(ModError*) { return MOD_OK; }
}

