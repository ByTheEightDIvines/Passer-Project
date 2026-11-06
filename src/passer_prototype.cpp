#include "global.h"

#include "d/actor/d_a_npc_passer.h"
#include "d/actor/d_a_tag_kmsg.h"
#include "d/d_com_inf_game.h"
#include "d/d_particle_name.h"
#include "d/d_path.h"
#include "d/d_stage.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_name.h"
#include "m_Do/m_Do_mtx.h"
#include "mods/service.hpp"
#include "mods/svc/flow.hpp"
#include "mods/svc/hook.hpp"
#include "mods/svc/log.h"
#include "mods/svc/stage.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <vector>

DEFINE_MOD();
IMPORT_SERVICE(HookService, svc_hook);
IMPORT_SERVICE(FlowService, svc_flow);
IMPORT_SERVICE(MessageService, svc_message);
IMPORT_SERVICE(LogService, svc_log);
IMPORT_SERVICE(StageService, svc_stage);

namespace {

constexpr const char* kFieldStage = "F_SP121";
constexpr s8 kFieldRoom = 5;
constexpr int kPasserType = 8;  // MAN_a2

// Path IDs answered by this mod. Stock room 5 only has path 0, so these never collide.
constexpr int kRoutePathId = 0x40;    // Link's entrance -> top-right exit (walking route)
constexpr int kReversePathId = 0x41;  // top-right exit -> Link's entrance (escape route)

// Middle of the road in F_SP121 room 5, traced from the room's collision, listed from the
// enemy field past the bridge to Link's entrance. The walking route runs it backwards.
constexpr std::array<std::array<f32, 3>, 29> kRoadPoints{{
    // Room 0: into the field past the mailbox, where the Leevers, Bulblins and Kargaroks are.
    {-11500.0f, 6.0f, 13800.0f}, {-11700.0f, -111.0f, 15000.0f},
    {-11450.0f, -417.0f, 16500.0f}, {-11450.0f, -724.0f, 18000.0f},
    {-11500.0f, -892.0f, 19400.0f},
    // Room 0: the bridge, flat at -1034.
    {-11500.0f, -1024.0f, 20600.0f}, {-11500.0f, -1034.0f, 21800.0f},
    {-11500.0f, -1034.0f, 23000.0f}, {-11500.0f, -1034.0f, 24100.0f},
    // Room 5: the canyon road down to Link's entrance.
    {-11400.0f, -1075.0f, 24900.0f}, {-11167.0f, -1075.0f, 25784.0f},
    {-11673.0f, -1080.0f, 26281.0f}, {-12138.0f, -1147.0f, 26815.0f},
    {-12653.0f, -1218.0f, 27302.0f}, {-13283.0f, -1258.0f, 27614.0f},
    {-13988.0f, -1318.0f, 27660.0f}, {-14697.0f, -1438.0f, 27660.0f},
    {-15405.0f, -1564.0f, 27627.0f}, {-16104.0f, -1610.0f, 27515.0f},
    {-16807.0f, -1658.0f, 27429.0f}, {-17516.0f, -1700.0f, 27420.0f},
    {-18221.0f, -1779.0f, 27479.0f}, {-18906.0f, -1868.0f, 27655.0f},
    {-19520.0f, -1967.0f, 28007.0f}, {-20065.0f, -2026.0f, 28456.0f},
    {-20480.0f, -2032.0f, 29030.0f}, {-20773.0f, -2038.0f, 29672.0f},
    {-20950.0f, -2045.0f, 30358.0f}, {-20980.0f, -2121.0f, 31065.0f},
}};

// A path built in memory, laid out exactly like a room's RPAT/RPPN entry.
struct RouteStore {
    dPath path;
    std::array<dPnt, kRoadPoints.size()> points;
};
RouteStore g_route{};
RouteStore g_reverseRoute{};

void buildRoute(RouteStore& store, bool reverse) {
    const size_t count = kRoadPoints.size();
    for (size_t i = 0; i < count; ++i) {
        const auto& src = kRoadPoints[reverse ? count - 1 - i : i];
        dPnt& point = store.points[i];
        point.mArg0 = 0xFF;  // no special action at the point; just keep walking
        point.mArg1 = 0xFF;
        point.mArg2 = 0xFF;
        point.mArg3 = 0xFF;
        point.m_position = Vec{src[0], src[1], src[2]};
    }
    store.path.m_num = static_cast<u16>(count);
    store.path.m_nextID = 0xFFFF;
    store.path.field_0x4 = 0;
    store.path.m_closed = false;
    store.path.swbit = 0xFF;
    store.path.field_0x7 = 0;
    // The point pointer is an offset from the pointer field itself; the top bit marks it as
    // already relocated so the game never tries to relocate it again.
    const auto offset = static_cast<s32>(reinterpret_cast<char*>(store.points.data()) -
                                         reinterpret_cast<char*>(&store.path.m_points));
    store.path.m_points.value.value = static_cast<s32>(static_cast<u32>(offset) | 0x80000000u);
}

bool inFieldStage() {
    const char* stage = dComIfGp_getStartStageName();
    return stage != nullptr && std::strcmp(stage, kFieldStage) == 0;
}
constexpr f32 kThreatRadius = 900.0f;
constexpr int kSafeFramesToResume = 90;  // three seconds with nothing scary nearby
constexpr f32 kWallCheckHeight = 55.0f;
constexpr f32 kTalkTagRadius = 180.0f;
constexpr int kStartingHealth = 3;
constexpr int kDeathDuration = 84;
constexpr u16 kTalkAnimation = 6;
// The talk marker runs its flow in the stage's message file. Hyrule Field (F_SP121) uses group 8;
// registering in group 0 left the node unresolved, so the talk opened with no text and never ended.
constexpr u16 kTalkFlowGroup = 8;
constexpr std::array kLanguages{
    MESSAGE_LANGUAGE_ENGLISH, MESSAGE_LANGUAGE_GERMAN, MESSAGE_LANGUAGE_FRENCH,
    MESSAGE_LANGUAGE_SPANISH, MESSAGE_LANGUAGE_ITALIAN, MESSAGE_LANGUAGE_JAPANESE,
};

struct PasserState {
    daNpcPasser_c* actor = nullptr;
    fpc_ProcID id = 0;
    bool registered = false;  // seen by process ID at least once (create may not be registered yet)
    int health = kStartingHealth;
    int deathFrame = 0;
    f32 knockbackX = 0.0f;
    f32 knockbackY = 0.0f;
    f32 knockbackZ = 0.0f;
    bool dead = false;
    bool grounded = false;
    bool talking = false;
    bool arrived = false;   // reached the end of the road; waits there until out of view
    bool cowering = false;  // finished running away; waits until it is safe, then walks on
    int safeFrames = 0;
    int reliefFrames = 0;   // frames spent in the vanilla wall-bump pause
};

std::array<PasserState, 16> g_states{};
mods::flow::RegisteredMessage g_greeting;
mods::flow::Graph g_talkGraph;
uint16_t g_talkNode = 0xFFFF;

// Rooms 0 (the field past the bridge), 4 and 5 are loaded together, and the passer walks
// through them, so enemies from any of them count.
bool inPasserArea(const fopAc_ac_c* actor) {
    if (actor == nullptr || !inFieldStage()) {
        return false;
    }
    const int room = fopAcM_GetRoomNo(actor);
    return room == 0 || room == 4 || room == kFieldRoom;
}

bool inFieldRoom(const fopAc_ac_c* actor) {
    return actor != nullptr && inFieldStage() && fopAcM_GetRoomNo(actor) == kFieldRoom;
}

// Only the passer this mod spawns: the right model type and our walking route.
bool isPrototypePasser(const fopAc_ac_c* actor) {
    return inFieldRoom(actor) &&
           fopAcM_GetName(const_cast<fopAc_ac_c*>(actor)) == fpcNm_NPC_PASSER_e &&
           (fopAcM_GetParam(actor) & 0xFF) == kPasserType &&
           ((fopAcM_GetParam(actor) >> 16) & 0xFF) == kRoutePathId;
}

// Drop state for passers that were unloaded (room change, stage load) without dying, so a
// stale pointer is never dereferenced or matched to a new actor allocated at the same address.
void pruneStates() {
    for (auto& state : g_states) {
        if (state.actor == nullptr) {
            continue;
        }
        fopAc_ac_c* found = fopAcM_SearchByID(state.id);
        if (found == state.actor) {
            state.registered = true;
        } else if (state.registered || found != nullptr) {
            state = PasserState{};
        }
    }
}

PasserState* findState(daNpcPasser_c* actor, bool create) {
    pruneStates();
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
                state.id = fopAcM_GetID(actor);
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
        if (fopAcM_GetGroup(enemy) != fopAc_ENEMY_e || !inPasserArea(enemy)) {
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
    pruneStates();
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

// Back on the walking route from the nearest point, heading on toward the top-right exit.
void resumeRoute(daNpcPasser_c* passer, PasserState& state) {
    state.cowering = false;
    state.safeFrames = 0;
    state.reliefFrames = 0;
    passer->m_path.setPath(kRoutePathId, fopAcM_GetRoomNo(passer), 1, &passer->current.pos, true);
    passer->m_path.setNextPoint(passer->current.pos);
    passer->setAction(daNpcPasser_c::MODE_0);
}

// The talk marker follows the one live test passer around the room.
daNpcPasser_c* livePasser() {
    pruneStates();
    for (auto& state : g_states) {
        if (state.actor != nullptr && !state.dead && isPrototypePasser(state.actor)) {
            return state.actor;
        }
    }
    return nullptr;
}

daNpcPasser_c* nearestThreatTarget(const fopAc_ac_c* enemy) {
    if (enemy == nullptr || fopAcM_GetGroup(enemy) != fopAc_ENEMY_e || !inPasserArea(enemy)) {
        return nullptr;
    }
    pruneStates();
    daNpcPasser_c* result = nullptr;
    f32 nearestDistanceSq = kThreatRadius * kThreatRadius;
    for (auto& state : g_states) {
        if (state.actor == nullptr || state.dead || !isPrototypePasser(state.actor)) {
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
DEFINE_HOOK(&daNpcPasser_c::executePath, PasserExecutePath);
DEFINE_HOOK(&daNpcPasser_c::executeEscape, PasserExecuteEscape);
DEFINE_HOOK(&daNpcPasser_c::executeFear, PasserExecuteFear);
DEFINE_HOOK(&daNpcPasser_c::executeRelief, PasserExecuteRelief);
DEFINE_HOOK(&dPath_GetRoomPath, RoomPathLookup);
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
    if (isPrototypePasser(passer)) {
        // execute() resets the wall check height every frame just before this. The vanilla
        // value is lower than the 41-unit step onto the bridge, so the passer walked into it as
        // a wall. Link's ground probe reaches 60 units, so a 55-unit wall check lets him step up.
        passer->mAcchCir.SetWallH(kWallCheckHeight);
    }
    if (isPrototypePasser(passer) && state != nullptr &&
        (state->dead || state->talking || state->arrived)) {
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

// Vanilla deletes a passer the instant it reaches the end of its path, even on screen.
// Instead, stop at the end of the road and wait there until the camera can't see it.
HookAction onExecutePathPre(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    if (!isPrototypePasser(passer) || passer->mPathMode != 2 || !passer->m_path.isPath()) {
        return HOOK_CONTINUE;
    }
    const int last = passer->m_path.mPath->m_num - 1;
    if (passer->m_path.field_0x20 < last) {
        return HOOK_CONTINUE;
    }
    auto* state = findState(passer, false);
    if (state == nullptr) {
        return HOOK_CONTINUE;
    }
    state->arrived = true;
    passer->speedF = 0.0f;
    auto* idle = static_cast<J3DAnmTransformKey*>(passer->getAnmP(0, passer->getObjNum()));
    if (idle != nullptr) {
        passer->setAnm(idle, 1.0f, 12.0f, J3DFrameCtrl::EMode_LOOP, 0, -1);
    }
    return HOOK_SKIP_ORIGINAL;
}

// Vanilla deletes a fleeing passer when it reaches the end of its escape path or leaves the
// camera's view. Instead it keeps running out of view, and at the end of the path it stops,
// turns away from the danger and cowers until it is safe to walk on.
HookAction onExecuteEscapePre(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    auto* state = findState(passer, false);
    if (!isPrototypePasser(passer) || state == nullptr) {
        return HOOK_CONTINUE;
    }
    fopAcM_OffCondition(passer, fopAcCnd_NODRAW_e);
    if (passer->field_0xb1c != 1 || !passer->m_path.isPath()) {
        return HOOK_CONTINUE;
    }
    const int last = passer->m_path.mPath->m_num - 1;
    if (passer->m_path.field_0x20 < last) {
        return HOOK_CONTINUE;
    }
    const dPnt* points = passer->m_path.mPath->m_points;
    const Vec end = points[last].m_position;
    const f32 dx = passer->current.pos.x - end.x;
    const f32 dz = passer->current.pos.z - end.z;
    const f32 reach = std::max(passer->speedF * 2.0f, 30.0f);
    if (dx * dx + dz * dz > reach * reach) {
        return HOOK_CONTINUE;
    }
    state->cowering = true;
    state->safeFrames = 0;
    passer->setAction(daNpcPasser_c::MODE_2);  // vanilla cower: stops and turns away from Link
    return HOOK_SKIP_ORIGINAL;
}

// Cowering also deletes the passer once it is off screen; keep it.
HookAction onKeepWhenOffscreenPre(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    if (isPrototypePasser(passer)) {
        fopAcM_OffCondition(passer, fopAcCnd_NODRAW_e);
    }
    return HOOK_CONTINUE;
}

// The wall-bump pause only ends in vanilla by deleting the passer off screen. Keep it, and
// after three seconds put it back on its walking route instead of leaving it frozen.
HookAction onReliefPre(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    auto* state = findState(passer, false);
    if (!isPrototypePasser(passer) || state == nullptr) {
        return HOOK_CONTINUE;
    }
    fopAcM_OffCondition(passer, fopAcCnd_NODRAW_e);
    if (++state->reliefFrames >= kSafeFramesToResume && !passer->checkFearSituation()) {
        state->reliefFrames = 0;
        resumeRoute(passer, *state);
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

void onRoomPathPost(ModContext*, void* args, void* retval, void*) {
    if (retval == nullptr || !inFieldStage()) {
        return;
    }
    const int pathId = mods::arg<int>(args, 0);
    const int room = mods::arg<int>(args, 1);
    if (room != kFieldRoom) {
        return;
    }
    if (pathId == kRoutePathId) {
        *static_cast<dPath**>(retval) = &g_route.path;
    } else if (pathId == kReversePathId) {
        *static_cast<dPath**>(retval) = &g_reverseRoute.path;
    }
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

    if (state->cowering && !state->talking) {
        passer->speedF = 0.0f;
        // Wait until nothing scary has been near for a few seconds, then walk on.
        if (passer->checkFearSituation()) {
            state->safeFrames = 0;
        } else if (++state->safeFrames >= kSafeFramesToResume) {
            resumeRoute(passer, *state);
        }
    }

    if (state->arrived && !state->talking) {
        passer->speedF = 0.0f;
        if (fopAcM_CheckCondition(passer, fopAcCnd_NODRAW_e)) {
            // Out of the camera's view at the end of the road: leave quietly.
            fopAcM_delete(passer);
            state->actor = nullptr;
            return HOOK_SKIP_ORIGINAL;
        }
    }

    if (passer->mCyl.ChkTgHit()) {
        // Only enemies may hurt the passer. Link's sword, arrows, bombs, boomerang and
        // other player-owned actors all report a non-enemy attacker and are ignored.
        auto* attacker = passer->mCyl.GetTgHitAc();
        passer->mCyl.ResetTgHit();
        if (attacker == nullptr || !fopAcM_IsActor(attacker) ||
            fopAcM_GetGroup(attacker) != fopAc_ENEMY_e) {
            return HOOK_CONTINUE;
        }
        --state->health;
        passer->health = static_cast<s16>(state->health);
        if (state->health <= 0) {
            beginDeath(passer, *state);
        } else if (nearestEnemy(passer, kThreatRadius) != nullptr) {
            state->arrived = false;  // running away replaces waiting at the end of the road
            state->cowering = false;
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
    // The talk marker is spawned once at the start of the road; keep it on the walking passer.
    if (auto* walker = livePasser()) {
        tag->current.pos = walker->current.pos;
        tag->home.pos = walker->current.pos;
    }
    // The marker only offers a talk while Link is in its home room. Follow Link between the
    // rooms the passer walks through (room 5, the bridge and the field in room 0).
    const int stay = dComIfGp_roomControl_getStayNo();
    if (stay == 0 || stay == 4 || stay == kFieldRoom) {
        tag->home.roomNo = static_cast<s8>(stay);
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
    auto* passer = nearestPasser(tag);
    if (passer == nullptr) {
        // No passer to talk to (not spawned yet, dead or gone): no talk prompt.
        tag->attention_info.flags = 0;
        return;
    }
    if (auto* state = findState(passer, false); state != nullptr && state->talking &&
        !tag->eventInfo.checkCommandTalk()) {
        state->talking = false;
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

stage_actor_data_class makeRecord(const char* name, u32 params, const std::array<f32, 3>& pos,
                                  s16 angleX, s16 angleY, s16 angleZ) {
    stage_actor_data_class record{};
    std::strncpy(record.name, name, sizeof(record.name));
    record.base.parameters = params;
    record.base.position = cXyz(pos[0], pos[1], pos[2]);
    record.base.angle = csXyz(angleX, angleY, angleZ);
    record.base.setID = 0xFFFF;
    return record;
}

// Adds the passer, its talk marker and two escape markers to room 5 as the room loads.
// Nothing on the disc is changed; the game creates them like any other placed actor.
ModResult addRoomActors() {
    // The passer starts at Link's entrance and walks toward the top-right exit.
    const auto& first = kRoadPoints.back();
    const auto& second = kRoadPoints[kRoadPoints.size() - 2];
    const auto& last = kRoadPoints.front();
    const cXyz from(first[0], first[1], first[2]);
    const cXyz to(second[0], second[1], second[2]);
    const s16 facing = cLib_targetAngleY(&from, &to);

    // Passer: MAN_a2 (type 8), no carried object, our route, walk (move type 1).
    // angle.x = 0xFF turns off the vanilla "leave at a set time of day" timer.
    const u32 passerParams = kPasserType | (0u << 8) | (static_cast<u32>(kRoutePathId) << 16) |
                             (1u << 24);
    // KMsg type 0: attention offset 0x80, eye offset 0xA0, talk distance 3 (200 units) and talk
    // angle 6 (any side). Distance 0 is only 50 units, closer than the passer's body lets Link get.
    constexpr u32 kTalkTagParams = 0x0080A000u | (6u << 5) | 3u;
    // angle.x = 0xFF means no switch; angle.z = 0xFFFF means no flow node (set at runtime).
    const std::array records{
        makeRecord("Passer", passerParams, first, 0xFF, facing, 0),
        makeRecord("KMsg", kTalkTagParams, first, 0xFF, 0, static_cast<s16>(0xFFFF)),
        // Each escape marker sits at the end its path runs to: fleeing toward the top-right exit
        // follows the walking route, fleeing toward Link's entrance follows it in reverse.
        makeRecord("TagEsc", 0xFFFFFF00u | kRoutePathId, last, 0, 0, 0),
        makeRecord("TagEsc", 0xFFFFFF00u | kReversePathId, first, 0, 0, 0),
    };
    for (const auto& record : records) {
        const ModResult result = svc_stage->add_actor(mod_ctx, kFieldStage, kFieldRoom, -1,
                                                      &record, sizeof(record), nullptr);
        if (result != MOD_OK) {
            return result;
        }
    }
    return MOD_OK;
}

}  // namespace

extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError* error) {
    const ModResult flowResult = registerGreetingFlow();
    if (flowResult != MOD_OK) {
        return mods::set_error(error, flowResult, "failed to register the passer dialogue flow");
    }

    buildRoute(g_route, true);
    buildRoute(g_reverseRoute, false);
    if (const ModResult stageResult = addRoomActors(); stageResult != MOD_OK) {
        return mods::set_error(error, stageResult, "failed to add the passer to Hyrule Field room 5");
    }

    if (mods::hook::add_post<RoomPathLookup>(onRoomPathPost) != MOD_OK ||
        mods::hook::add_pre<PasserExecutePath>(onExecutePathPre) != MOD_OK ||
        mods::hook::add_pre<PasserExecuteEscape>(onExecuteEscapePre) != MOD_OK ||
        mods::hook::add_pre<PasserExecuteFear>(onKeepWhenOffscreenPre) != MOD_OK ||
        mods::hook::add_pre<PasserExecuteRelief>(onReliefPre) != MOD_OK ||
        mods::hook::add_post<PasserCreateInit>(onCreateInitPost) != MOD_OK ||
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
        "Hyrule Field room 5 test passer ready: spawned by code on a code-built road route");
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

