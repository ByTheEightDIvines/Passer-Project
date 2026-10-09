#include "global.h"

#include "d/actor/d_a_npc_passer.h"
#include "d/actor/d_a_tag_kmsg.h"
#include "d/d_com_inf_game.h"
#include "d/d_particle_name.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_name.h"
#include "m_Do/m_Do_mtx.h"
#include "mods/service.hpp"
#include "mods/svc/flow.hpp"
#include "mods/svc/hook.hpp"
#include "mods/svc/log.h"

#include <array>
#include <cmath>
#include <cstring>
#include <vector>

DEFINE_MOD();
IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(FlowService, svc_flow);
IMPORT_SERVICE(MessageService, svc_message);
IMPORT_SERVICE(LogService, svc_log);

namespace {

constexpr s8 kFieldRoom = 5;
constexpr int kPasserType = 8;  // MAN_a2
constexpr f32 kThreatRadius = 900.0f;
constexpr f32 kTalkTagRadius = 180.0f;
constexpr int kStartingHealth = 3;
constexpr int kDeathDuration = 84;
constexpr u16 kTalkAnimation = 6;
constexpr u16 kTalkFlowGroup = 0;
constexpr std::array kLanguages{
    MESSAGE_LANGUAGE_ENGLISH, MESSAGE_LANGUAGE_GERMAN, MESSAGE_LANGUAGE_FRENCH,
    MESSAGE_LANGUAGE_SPANISH, MESSAGE_LANGUAGE_ITALIAN, MESSAGE_LANGUAGE_JAPANESE,
};

struct PasserState {
    daNpcPasser_c* actor = nullptr;
    int health = kStartingHealth;
    int deathFrame = 0;
    f32 knockbackX = 0.0f;
    f32 knockbackY = 0.0f;
    f32 knockbackZ = 0.0f;
    bool dead = false;
    bool grounded = false;
    bool talking = false;
};

std::array<PasserState, 16> g_states{};
mods::flow::RegisteredMessage g_greeting;
mods::flow::Graph g_talkGraph;
uint16_t g_talkNode = 0xFFFF;

bool inFieldRoom(const fopAc_ac_c* actor) {
    const char* stage = dComIfGp_getStartStageName();
    return actor != nullptr && stage != nullptr && std::strcmp(stage, "F_SP121") == 0 &&
           fopAcM_GetRoomNo(actor) == kFieldRoom;
}

bool isPrototypePasser(const fopAc_ac_c* actor) {
    return inFieldRoom(actor) &&
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

fopAc_ac_c* nearestEnemy(const daNpcPasser_c* passer, f32 maxDistance) {
    struct Search {
        const daNpcPasser_c* passer;
        f32 maxDistanceSq;
        f32 nearestDistanceSq;
        fopAc_ac_c* result;
    } search{passer, maxDistance * maxDistance, maxDistance * maxDistance, nullptr};

    auto findEnemy = [](void* candidate, void* context) -> void* {
        auto* data = static_cast<Search*>(context);
        if (data == nullptr || !fopAcM_IsActor(candidate)) {
            return nullptr;
        }
        auto* enemy = static_cast<fopAc_ac_c*>(candidate);
        if (fopAcM_GetGroup(enemy) != fopAc_ENEMY_e ||
            fopAcM_GetRoomNo(enemy) != fopAcM_GetRoomNo(data->passer)) {
            return nullptr;
        }
        const f32 dx = enemy->current.pos.x - data->passer->current.pos.x;
        const f32 dz = enemy->current.pos.z - data->passer->current.pos.z;
        const f32 distanceSq = dx * dx + dz * dz;
        if (distanceSq <= data->nearestDistanceSq) {
            data->nearestDistanceSq = distanceSq;
            data->result = enemy;
        }
        return nullptr;
    };
    fpcM_Search(findEnemy, &search);
    return search.result;
}

daNpcPasser_c* nearestPasser(const daTag_KMsg_c* tag) {
    daNpcPasser_c* result = nullptr;
    f32 nearestDistanceSq = kTalkTagRadius * kTalkTagRadius;
    for (auto& state : g_states) {
        if (state.actor == nullptr || state.dead || !isPrototypePasser(state.actor)) {
            continue;
        }
        if (fopAcM_GetRoomNo(state.actor) != fopAcM_GetRoomNo(tag)) {
            continue;
        }
        const f32 dx = state.actor->current.pos.x - tag->current.pos.x;
        const f32 dy = state.actor->current.pos.y - tag->current.pos.y;
        const f32 dz = state.actor->current.pos.z - tag->current.pos.z;
        const f32 distanceSq = dx * dx + dy * dy + dz * dz;
        if (distanceSq <= nearestDistanceSq) {
            nearestDistanceSq = distanceSq;
            result = state.actor;
        }
    }
    return result;
}

daNpcPasser_c* nearestThreatTarget(const fopAc_ac_c* enemy) {
    if (enemy == nullptr || fopAcM_GetGroup(enemy) != fopAc_ENEMY_e || !inFieldRoom(enemy)) {
        return nullptr;
    }
    daNpcPasser_c* result = nullptr;
    f32 nearestDistanceSq = kThreatRadius * kThreatRadius;
    for (auto& state : g_states) {
        if (state.actor == nullptr || state.dead || !isPrototypePasser(state.actor) ||
            fopAcM_GetRoomNo(state.actor) != fopAcM_GetRoomNo(enemy)) {
            continue;
        }
        const f32 dx = state.actor->current.pos.x - enemy->current.pos.x;
        const f32 dz = state.actor->current.pos.z - enemy->current.pos.z;
        const f32 distanceSq = dx * dx + dz * dz;
        if (distanceSq <= nearestDistanceSq) {
            nearestDistanceSq = distanceSq;
            result = state.actor;
        }
    }
    return result;
}

bool isEnemyPlayerQuery(void* args, const fopAc_ac_c** enemyOut,
                        const fopAc_ac_c** playerOut, daNpcPasser_c** targetOut) {
    const auto* enemy = mods::arg<const fopAc_ac_c*>(args, 0);
    const auto* target = mods::arg<const fopAc_ac_c*>(args, 1);
    if (enemy == nullptr || target == nullptr || target != dComIfGp_getPlayer(0)) {
        return false;
    }
    auto* passer = nearestThreatTarget(enemy);
    if (passer == nullptr) {
        return false;
    }
    *enemyOut = enemy;
    *playerOut = target;
    *targetOut = passer;
    return true;
}

void redirectEnemyAngleY(void* args, void* retval) {
    if (retval == nullptr) {
        return;
    }
    const fopAc_ac_c* enemy = nullptr;
    const fopAc_ac_c* player = nullptr;
    daNpcPasser_c* passer = nullptr;
    if (isEnemyPlayerQuery(args, &enemy, &player, &passer)) {
        *static_cast<s16*>(retval) = cLib_targetAngleY(&enemy->current.pos, &passer->current.pos);
    }
}

void redirectEnemyAngleX(void* args, void* retval) {
    if (retval == nullptr) {
        return;
    }
    const fopAc_ac_c* enemy = nullptr;
    const fopAc_ac_c* player = nullptr;
    daNpcPasser_c* passer = nullptr;
    if (isEnemyPlayerQuery(args, &enemy, &player, &passer)) {
        *static_cast<s16*>(retval) = cLib_targetAngleX(&enemy->current.pos, &passer->current.pos);
    }
}

void redirectEnemyDistance(void* args, void* retval, bool squared, bool xzOnly) {
    if (retval == nullptr) {
        return;
    }
    const fopAc_ac_c* enemy = nullptr;
    const fopAc_ac_c* player = nullptr;
    daNpcPasser_c* passer = nullptr;
    if (!isEnemyPlayerQuery(args, &enemy, &player, &passer)) {
        return;
    }
    const f32 dx = passer->current.pos.x - enemy->current.pos.x;
    const f32 dy = xzOnly ? 0.0f : passer->current.pos.y - enemy->current.pos.y;
    const f32 dz = passer->current.pos.z - enemy->current.pos.z;
    const f32 distanceSq = dx * dx + dy * dy + dz * dz;
    *static_cast<f32*>(retval) = squared ? distanceSq : std::sqrt(distanceSq);
}

void redirectEnemySeenAngle(void* args, void* retval) {
    if (retval == nullptr) {
        return;
    }
    const fopAc_ac_c* enemy = nullptr;
    const fopAc_ac_c* player = nullptr;
    daNpcPasser_c* passer = nullptr;
    if (isEnemyPlayerQuery(args, &enemy, &player, &passer)) {
        *static_cast<s32*>(retval) = std::abs(static_cast<s16>(
            cLib_targetAngleY(&enemy->current.pos, &passer->current.pos) - enemy->shape_angle.y));
    }
}

void setDamageable(daNpcPasser_c* passer) {
    // Accept ordinary weapon target hits on the NPC's existing collision cylinder.
    passer->mCyl.SetTgType(0xD8FBFDFF);
    passer->mCyl.OnTgSetBit();
    passer->mCyl.SetTgHitMark(CcG_Tg_UNK_MARK_1);
    passer->mCyl.SetTgSe(dCcD_SE_SOFT_BODY);
}

void setTalkAnimation(daNpcPasser_c* passer) {
    auto* animation = static_cast<J3DAnmTransformKey*>(passer->getAnmP(kTalkAnimation,
                                                                       passer->getObjNum()));
    if (animation != nullptr) {
        passer->setAnm(animation, 1.0f, 12.0f, J3DFrameCtrl::EMode_LOOP, 0, -1);
    }
}

void beginDeath(daNpcPasser_c* passer, PasserState& state) {
    state.dead = true;
    state.deathFrame = 0;
    state.grounded = false;
    state.talking = false;
    state.knockbackY = 8.0f;
    passer->health = 0;
    passer->attention_info.flags = 0;
    passer->mCyl.OffTgSetBit();
    passer->speedF = 0.0f;
    passer->speed.set(0.0f, 0.0f, 0.0f);

    if (auto* enemy = nearestEnemy(passer, kThreatRadius)) {
        f32 dx = passer->current.pos.x - enemy->current.pos.x;
        f32 dz = passer->current.pos.z - enemy->current.pos.z;
        const f32 length = std::sqrt(dx * dx + dz * dz);
        if (length > 0.001f) {
            state.knockbackX = (dx / length) * 9.0f;
            state.knockbackZ = (dz / length) * 9.0f;
        }
    }

    const cXyz scale(1.0f, 1.0f, 1.0f);
    dComIfGp_particle_set(static_cast<u16>(dPa_RM(ID_ZF_S_PODEATH00SMK)),
                          &passer->current.pos, &passer->shape_angle, &scale);
    dComIfGp_particle_set(static_cast<u16>(dPa_RM(ID_ZF_S_PODEATH02SP)),
                          &passer->current.pos, &passer->shape_angle, &scale);
}

DEFINE_HOOK(&daNpcPasser_c::create_init, PasserCreateInit);
DEFINE_HOOK(&daNpcPasser_c::callExecute, PasserCallExecute);
DEFINE_HOOK(&daNpcPasser_c::setCollision, PasserSetCollision);
DEFINE_HOOK(&daNpcPasser_c::setBaseMtx, PasserSetBaseMtx);
DEFINE_HOOK(&daNpcPasser_c::execute, PasserExecute);
DEFINE_HOOK(&daNpcCd2_c::checkFearSituation, PasserFearCheck);
DEFINE_HOOK(&daTag_KMsg_c::Execute, TalkTagExecute);
DEFINE_HOOK(&fopAcM_searchActorAngleY, EnemyTargetAngleY);
DEFINE_HOOK(&fopAcM_searchActorAngleX, EnemyTargetAngleX);
DEFINE_HOOK(&fopAcM_seenActorAngleY, EnemyTargetSeenAngle);
DEFINE_HOOK(&fopAcM_searchActorDistance, EnemyTargetDistance);
DEFINE_HOOK(&fopAcM_searchActorDistance2, EnemyTargetDistanceSquared);
DEFINE_HOOK(&fopAcM_searchActorDistanceXZ, EnemyTargetDistanceXZ);
DEFINE_HOOK(&fopAcM_searchActorDistanceXZ2, EnemyTargetDistanceXZSquared);

void onCreateInitPost(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    if (!isPrototypePasser(passer)) {
        return;
    }
    auto* state = findState(passer, true);
    if (state == nullptr) {
        return;
    }

    passer->health = kStartingHealth;
    // Battle attention makes this prototype passer lock-on targetable by Link.
    passer->attention_info.flags |= fopAc_AttnFlag_BATTLE_e;
    passer->attention_info.distances[fopAc_attn_BATTLE_e] = 180;
    setDamageable(passer);
}

HookAction onCallExecutePre(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    const auto* state = findState(passer, false);
    if (isPrototypePasser(passer) && state != nullptr && (state->dead || state->talking)) {
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

void onSetCollisionPost(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    if (!isPrototypePasser(passer)) {
        return;
    }
    if (auto* state = findState(passer, false); state != nullptr && !state->dead) {
        setDamageable(passer);
    }
}

void onBaseMtxPost(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    auto* state = findState(passer, false);
    if (!isPrototypePasser(passer) || state == nullptr || !state->dead || passer->mpMorf == nullptr) {
        return;
    }

    const int progress = state->deathFrame < 32 ? state->deathFrame : 32;
    const s16 fallAngle = static_cast<s16>(progress * (0x4000 / 32));
    mDoMtx_stack_c::transS(passer->current.pos.x, passer->current.pos.y, passer->current.pos.z);
    mDoMtx_stack_c::YrotM(passer->shape_angle.y);
    mDoMtx_stack_c::XrotM(fallAngle);
    passer->mpMorf->getModel()->setBaseTRMtx(mDoMtx_stack_c::get());
    passer->mpMorf->modelCalc();
}

HookAction onExecutePre(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    auto* state = findState(passer, false);
    if (!isPrototypePasser(passer) || state == nullptr) {
        return HOOK_CONTINUE;
    }

    if (state->dead) {
        ++state->deathFrame;
        if (!state->grounded) {
            passer->current.pos.x += state->knockbackX;
            passer->current.pos.y += state->knockbackY;
            passer->current.pos.z += state->knockbackZ;
            state->knockbackX *= 0.94f;
            state->knockbackZ *= 0.94f;
            state->knockbackY -= 0.65f;
            passer->mAcch.CrrPos(dComIfG_Bgsp());
            if (passer->mAcch.ChkGroundHit()) {
                state->grounded = true;
                state->knockbackY = 0.0f;
            }
        }
        if (state->deathFrame >= kDeathDuration) {
            fopAcM_delete(passer);
            state->actor = nullptr;
            return HOOK_SKIP_ORIGINAL;
        }
        return HOOK_CONTINUE;
    }

    if (passer->mCyl.ChkTgHit()) {
        passer->mCyl.ResetTgHit();
        --state->health;
        passer->health = static_cast<s16>(state->health);
        if (state->health <= 0) {
            beginDeath(passer, *state);
        } else if (nearestEnemy(passer, kThreatRadius) != nullptr) {
            passer->setAction(daNpcPasser_c::MODE_1);
        }
    }
    return HOOK_CONTINUE;
}

void onFearCheckPost(ModContext*, void* args, void* retval, void*) {
    auto* passer = static_cast<daNpcPasser_c*>(mods::arg<daNpcCd2_c*>(args, 0));
    if (retval != nullptr && isPrototypePasser(passer) &&
        nearestEnemy(passer, kThreatRadius) != nullptr) {
        *static_cast<bool*>(retval) = true;
    }
}

HookAction onTalkTagExecutePre(ModContext*, void* args, void*, void*) {
    auto* tag = mods::arg<daTag_KMsg_c*>(args, 0);
    if (!inFieldRoom(tag) || tag->getType() != daTag_KMsg_c::KMSG_TYPE_0 ||
        g_talkNode == 0xFFFF) {
        return HOOK_CONTINUE;
    }
    auto* passer = nearestPasser(tag);
    if (passer != nullptr) {
        tag->mFlowNodeNo = g_talkNode;
        if (tag->eventInfo.checkCommandTalk()) {
            if (auto* state = findState(passer, false); state != nullptr && !state->talking) {
                state->talking = true;
                setTalkAnimation(passer);
            }
        }
    }
    return HOOK_CONTINUE;
}

void onTalkTagExecutePost(ModContext*, void* args, void*, void*) {
    auto* tag = mods::arg<daTag_KMsg_c*>(args, 0);
    if (!inFieldRoom(tag) || tag->getType() != daTag_KMsg_c::KMSG_TYPE_0) {
        return;
    }
    if (auto* passer = nearestPasser(tag)) {
        if (auto* state = findState(passer, false); state != nullptr && state->talking &&
            !tag->eventInfo.checkCommandTalk()) {
            state->talking = false;
        }
    }
}

void onEnemyTargetAngleYPost(ModContext*, void* args, void* retval, void*) {
    redirectEnemyAngleY(args, retval);
}

void onEnemyTargetAngleXPost(ModContext*, void* args, void* retval, void*) {
    redirectEnemyAngleX(args, retval);
}

void onEnemyTargetSeenAnglePost(ModContext*, void* args, void* retval, void*) {
    redirectEnemySeenAngle(args, retval);
}

void onEnemyTargetDistancePost(ModContext*, void* args, void* retval, void*) {
    redirectEnemyDistance(args, retval, false, false);
}

void onEnemyTargetDistanceSquaredPost(ModContext*, void* args, void* retval, void*) {
    redirectEnemyDistance(args, retval, true, false);
}

void onEnemyTargetDistanceXZPost(ModContext*, void* args, void* retval, void*) {
    redirectEnemyDistance(args, retval, false, true);
}

void onEnemyTargetDistanceXZSquaredPost(ModContext*, void* args, void* retval, void*) {
    redirectEnemyDistance(args, retval, true, true);
}

ModResult registerGreetingFlow() {
    mods::flow::MessageBuilder builder;
    builder.box_kind(MESSAGE_BOX_TALK)
        .text("Road's quiet for now. I'm heading west before nightfall.");

    std::vector<mods::flow::MessageVariant> variants;
    variants.reserve(kLanguages.size());
    for (const auto language : kLanguages) {
        variants.push_back(builder.build(language));
    }
    g_greeting = mods::flow::register_message(kTalkFlowGroup, variants);
    if (!g_greeting) {
        return g_greeting.result();
    }

    mods::flow::GraphBuilder graph{kTalkFlowGroup};
    auto line = graph.add_message(g_greeting.id());
    g_talkNode = line.id();
    line.next(mods::flow::kEnd);
    g_talkGraph = graph.commit();
    return g_talkGraph ? MOD_OK : g_talkGraph.result();
}

}  // namespace

extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError* error) {
    const ModResult flowResult = registerGreetingFlow();
    if (flowResult != MOD_OK) {
        return mods::set_error(error, flowResult, "failed to register the passer dialogue flow");
    }

    if (mods::hook::add_post<PasserCreateInit>(onCreateInitPost) != MOD_OK ||
        mods::hook::add_pre<PasserCallExecute>(onCallExecutePre) != MOD_OK ||
        mods::hook::add_post<PasserSetCollision>(onSetCollisionPost) != MOD_OK ||
        mods::hook::add_post<PasserSetBaseMtx>(onBaseMtxPost) != MOD_OK ||
        mods::hook::add_pre<PasserExecute>(onExecutePre) != MOD_OK ||
        mods::hook::add_post<PasserFearCheck>(onFearCheckPost) != MOD_OK ||
        mods::hook::add_pre<TalkTagExecute>(onTalkTagExecutePre) != MOD_OK ||
        mods::hook::add_post<TalkTagExecute>(onTalkTagExecutePost) != MOD_OK ||
        mods::hook::add_post<EnemyTargetAngleY>(onEnemyTargetAngleYPost) != MOD_OK ||
        mods::hook::add_post<EnemyTargetAngleX>(onEnemyTargetAngleXPost) != MOD_OK ||
        mods::hook::add_post<EnemyTargetSeenAngle>(onEnemyTargetSeenAnglePost) != MOD_OK ||
        mods::hook::add_post<EnemyTargetDistance>(onEnemyTargetDistancePost) != MOD_OK ||
        mods::hook::add_post<EnemyTargetDistanceSquared>(onEnemyTargetDistanceSquaredPost) != MOD_OK ||
        mods::hook::add_post<EnemyTargetDistanceXZ>(onEnemyTargetDistanceXZPost) != MOD_OK ||
        mods::hook::add_post<EnemyTargetDistanceXZSquared>(onEnemyTargetDistanceXZSquaredPost) != MOD_OK) {
        return mods::set_error(error, MOD_UNAVAILABLE, "failed to install passer prototype hooks");
    }

    svc_log->info(mod_ctx,
        "Hyrule Field room 5 passer prototype initialized (enemy pursuit, damage, talk, escape, fall, Poe effect)");
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) { return MOD_OK; }

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    g_talkGraph.reset();
    g_greeting = {};
    g_talkNode = 0xFFFF;
    return MOD_OK;
}
}

