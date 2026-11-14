#include "global.h"

#include "d/actor/d_a_npc_passer.h"
#include "d/actor/d_a_e_rd.h"
#include "d/actor/d_a_player.h"
#include "d/actor/d_a_tag_kmsg.h"
#include "d/d_com_inf_game.h"
#include "d/d_particle_name.h"
#include "d/d_path.h"
#include "d/d_stage.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_searcher.h"
#include "f_op/f_op_actor_iter.h"
#include "f_pc/f_pc_name.h"
#include "m_Do/m_Do_mtx.h"
#include "SSystem/SComponent/c_math.h"
#include "mods/service.hpp"
#include "mods/svc/flow.hpp"
#include "mods/svc/hook.hpp"
#include "mods/svc/log.h"
#include "mods/svc/stage.h"
#include "spawner_actor.hpp"
#include "spot_picker.hpp"
#include "tent_actor.hpp"
#include "soul_actor.hpp"
#include "traveller_gear.hpp"
#include "fighter_anim.hpp"
#include "camp_fire.hpp"
#include "camp_cook.hpp"
#include "travellers.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>
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
// A traveller with an enemy this close stays in the world even when the camera can't see it, so
// the enemy can actually chase it down. Once it has gotten away, the game may tidy it up again.
constexpr f32 kChasedRadius = 3000.0f;
// Old women run at this share of a normal traveller's flight speed.
constexpr f32 kOldWomanRunScale = 0.6f;
constexpr int kSafeFramesToResume = 90;  // three seconds with nothing scary nearby
constexpr f32 kWallCheckHeight = 55.0f;
constexpr std::array<f32, 3> kBridgeBulblinPos{-11500.0f, -1034.0f, 22400.0f};
constexpr f32 kTalkTagRadius = 180.0f;
constexpr int kStartingHealth = 3;
constexpr int kQuartersPerHeart = 4;
// The death, borrowed from the Bokoblins: the final hit throws him back and away in the Bokoblin's
// fly-back pose, he lands in its sprawl, lies there a moment, then goes up in the game's smoke puff
// and his soul (soul_actor.cpp) floats out of it.
constexpr int kDeathDuration = 90;          // safety limit if he never finds the ground
constexpr int kDeathLie = 45;               // a second and a half on the ground after the sprawl
constexpr int kDeathLimit = 240;            // never more than eight seconds dying, whatever happens
constexpr f32 kLaunchSpeedXZ = 14.0f;       // away from the enemy that landed the hit
constexpr f32 kLaunchSpeedY = 26.0f;
constexpr f32 kLaunchGravity = 1.6f;
constexpr f32 kLaunchDrag = 0.985f;
constexpr f32 kTumblePivotY = 90.0f;        // spin around his middle, not his feet
constexpr u16 kLaunchAnimation = 0x29;      // the startled flinch: only if the body has no fly-back pose
constexpr u16 kTalkAnimation = 6;
// The talk marker runs its flow in the stage's message file. Hyrule Field (F_SP121) uses group 8;
// registering in group 0 left the node unresolved, so the talk opened with no text and never ended.
constexpr u16 kTalkFlowGroup = 8;
constexpr int kHurtInvulnFrames = 30;
constexpr u8 kStrikeLinkPower = 2;       // a fighter's blow on Link (wolf): half a heart
constexpr int kStuckFrames = 10;
constexpr int kFoeLostFrames = 30;       // a fighter stays on guard this long after losing sight of its enemy
constexpr f32 kGravity = 5.0f, kMaxFall = 60.0f;         // fleeing and pinned this long: step sideways
constexpr int kSidestepFrames = 20;
constexpr f32 kReachAbove = 180.0f;      // fighters only square up to enemies within reach of the ground   // half a second untouchable after a hit, like Link
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
    s16 tumble = 0;  // (unused now: no backflip)
    bool deathLanded = false;
    int deathLie = 0;
    bool talking = false;
    bool arrived = false;   // reached the end of the road; waits there until out of view
    bool cowering = false;  // finished running away; waits until it is safe, then walks on
    int safeFrames = 0;
    int reliefFrames = 0;
    int logFrames = 0;   // frames spent in the vanilla wall-bump pause
    int unseenFrames = 0;  // frames since created without the game finding it by ID
    bool standing = false;  // a fighter holding its ground against a nearby enemy
    bool defending = false; // a camp fighter up and going after an enemy near the fire
    bool campCower = false; // a camper cowering in place while an enemy is near the fire
    int calmFrames = 0;
    int strikeTimer = 0;
    u8 anim = 0xFF;         // animation the camp guard last set (so it isn't restarted every frame)
    int invuln = 0;         // frames left of the short invincible moment after being hit (like Link's)
    bool chasing = false;   // a camp defender running after its enemy (not yet in reach)
    bool swingReq = false;  // a fighter in reach and due a swing (the blow lands on the swing's frames)
    bool onGuard = false;   // this frame: a fighter with an enemy to face (draws and squares up)
    bool turning = false;   // this frame: still turning to face it (shuffles round instead of spinning)
    int stuck = 0;          // fleeing: frames pinned against something (a tent, a rock)
    int sidestep = 0;       // fleeing / walking: frames left of stepping sideways off something
    s16 sideYaw = 0;        // the way it steps (along the thing it ran into)
    int foeLost = 0;        // frames since a standing fighter last saw its enemy
    bool blowPending = false;                          // a blow was thrown this frame: did it land?
    fpc_ProcID blowTarget = fpcM_ERROR_PROCESS_ID_e;   // what the last landed blow hit
    int blowCheck = 0;                                 // frames until we look at it again
    int blowHpBefore = 0;
    bool vsWolf = false;    // the one it's facing is wolf Link (its blows are aimed at him)
};

std::array<PasserState, 32> g_states{};
void blowTick(daNpcPasser_c* passer, PasserState& state);   // further down: the diary of blows
// Fighters the wolf has bitten (by roster slot): for five minutes they're as scared of wolf Link as
// any unarmed traveller and never go for him. Kept apart from the actor's state so it lasts when the
// traveller walks out of range and back; real time, so it runs out while they're away too.
constexpr auto kWolfShyTime = std::chrono::minutes(5);
std::array<std::chrono::steady_clock::time_point, travellers::kSlots> g_wolfShyUntil{};
bool wolfShy(const daNpcPasser_c* passer) {
    const int slot = travellers::slotIndex(passer);
    return slot >= 0 && slot < travellers::kSlots && std::chrono::steady_clock::now() < g_wolfShyUntil[slot];
}
mods::flow::RegisteredMessage g_greeting;
mods::flow::Graph g_talkGraph;
uint16_t g_talkNode = 0xFFFF;
constexpr int kCookLineCount = 4;
std::array<mods::flow::RegisteredMessage, kCookLineCount> g_cookLines;
std::array<uint16_t, kCookLineCount> g_cookNodes{0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF};

// Rooms 0 (the field past the bridge), 4 and 5 are loaded together, and the passer walks
// through them, so enemies from any of them count.
bool inPasserArea(const fopAc_ac_c* actor) {
    return actor != nullptr && travellers::inNetworkStage();
}

bool inFieldRoom(const fopAc_ac_c* actor) {
    return actor != nullptr && inFieldStage() && fopAcM_GetRoomNo(actor) == kFieldRoom;
}

// Only the passer this mod spawns: the right model type and our walking route.
bool isPrototypePasser(const fopAc_ac_c* actor) {
    return travellers::isTraveller(actor);
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
            state.unseenFrames = 0;
        } else if (state.registered || found != nullptr || ++state.unseenFrames > 30) {
            // Gone (deleted, or its room unloaded). With many travellers coming and going this
            // can happen before it was ever seen here, so time out instead of keeping a pointer.
            state = PasserState{};
        }
    }
}

// The live actor for a state, looked up by its ID; nullptr if it's gone. Never dereferences
// the stored pointer itself.
daNpcPasser_c* liveActor(const PasserState& state) {
    if (state.actor == nullptr) {
        return nullptr;
    }
    fopAc_ac_c* found = fopAcM_SearchByID(state.id);
    return found == state.actor ? state.actor : nullptr;
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

// Projectiles count as enemies to the game; to a traveller they're not someone to face or chase.
bool isProjectile(fopAc_ac_c* actor) {
    const s16 name = fopAcM_GetName(actor);
    return name == fpcNm_E_ARROW_e || name == fpcNm_E_TK_BALL_e || name == fpcNm_E_TH_BALL_e;
}

// maxAbove: skip enemies flying higher than this above the spot (birds circling overhead).
fopAc_ac_c* nearestEnemyAt(const cXyz& at, f32 maxDistance, f32 maxAbove = 1.0e9f) {
    struct Search {
        const cXyz* at;
        f32 maxDistanceSq;
        f32 nearestDistanceSq;
        fopAc_ac_c* result;
        f32 maxAbove;
    } search{&at, maxDistance * maxDistance, maxDistance * maxDistance, nullptr, maxAbove};

    auto findEnemy = [](void* candidate, void* context) -> void* {
        auto* data = static_cast<Search*>(context);
        if (data == nullptr || !fopAcM_IsActor(candidate)) {
            return nullptr;
        }
        auto* enemy = static_cast<fopAc_ac_c*>(candidate);
        if (fopAcM_GetGroup(enemy) != fopAc_ENEMY_e || !inPasserArea(enemy) || isProjectile(enemy)) {
            return nullptr;
        }
        if (enemy->current.pos.y - data->at->y > data->maxAbove) {
            return nullptr;
        }
        const f32 dx = enemy->current.pos.x - data->at->x;
        const f32 dz = enemy->current.pos.z - data->at->z;
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

fopAc_ac_c* nearestEnemy(const daNpcPasser_c* passer, f32 maxDistance) {
    return nearestEnemyAt(passer->current.pos, maxDistance);
}

// Wolf Link counts as a foe for the fighters (as a man he's a friend): returns him if he is a wolf
// within maxDistance of the spot, or nullptr.
fopAc_ac_c* wolfLinkNear(const cXyz& at, f32 maxDistance) {
    if (!daPy_py_c::checkNowWolf()) return nullptr;
    fopAc_ac_c* link = dComIfGp_getPlayer(0);
    if (link == nullptr) return nullptr;
    const f32 dx = link->current.pos.x - at.x, dz = link->current.pos.z - at.z;
    return dx * dx + dz * dz <= maxDistance * maxDistance ? link : nullptr;
}

// The nearer of a monster and wolf Link (either may be nullptr).
fopAc_ac_c* nearerFoe(const cXyz& at, fopAc_ac_c* a, fopAc_ac_c* b) {
    if (a == nullptr) return b;
    if (b == nullptr) return a;
    auto d2 = [&](fopAc_ac_c* f) {
        const f32 dx = f->current.pos.x - at.x, dz = f->current.pos.z - at.z;
        return dx * dx + dz * dz;
    };
    return d2(a) <= d2(b) ? a : b;
}

daNpcPasser_c* nearestPasser(const daTag_KMsg_c* tag) {
    pruneStates();
    daNpcPasser_c* result = nullptr;
    f32 nearestDistanceSq = kTalkTagRadius * kTalkTagRadius;
    for (auto& state : g_states) {
        if (state.dead || liveActor(state) == nullptr || !isPrototypePasser(state.actor)) {
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
    travellers::resumeWalk(passer);
}

// Test logging: once a second, where the passer is and what it is doing.
void logProgress(daNpcPasser_c* passer, PasserState& state) {
    if (++state.logFrames < 30) {
        return;
    }
    state.logFrames = 0;
    const int point = passer->m_path.isPath() ? passer->m_path.field_0x20 : -1;
    char line[256];
    std::snprintf(line, sizeof(line),
                  "passer pos=(%.0f,%.0f,%.0f) mode=%d point=%d speed=%.1f wall=%d stuck=%d "
                  "talk=%d cower=%d arrived=%d",
                  passer->current.pos.x, passer->current.pos.y, passer->current.pos.z,
                  static_cast<int>(passer->mActionIdx), point, passer->speedF,
                  passer->mAcch.ChkWallHit() ? 1 : 0, static_cast<int>(passer->field_0xb25),
                  state.talking ? 1 : 0, state.cowering ? 1 : 0, state.arrived ? 1 : 0);
    svc_log->info(mod_ctx, line);
}

daNpcPasser_c* livePasser();
bool lockedOnPasser(const fopAc_ac_c* enemy, const daNpcPasser_c* passer);
bool holdsGround(const daNpcPasser_c* passer);
void strike(daNpcPasser_c* passer, PasserState& state);
void keepGrounded(daNpcPasser_c* passer, PasserState& state);
bool startSidestep(daNpcPasser_c* passer, PasserState& state);
void sidestepTick(daNpcPasser_c* passer, PasserState& state, f32 speed);
bool swingOrStrike(daNpcPasser_c* passer, PasserState& state);
void fighterAnim(daNpcPasser_c* passer, PasserState& state);

// The Bulblin currently running its update, while a live passer is close enough to target.
fopAc_ac_c* g_bulblinNearPasser = nullptr;
daNpcPasser_c* g_bulblinTarget = nullptr;   // the traveller nearest the Bulblin updating right now
int g_coachSwaps = 0;  // wagon lookups answered with the passer during this Bulblin update
constexpr f32 kBulblinTargetRadius = 2500.0f;

HookAction onBulblinExecutePre(ModContext*, void* args, void*, void*) {
    g_bulblinNearPasser = nullptr;
    auto* bulblin = mods::arg<fopAc_ac_c*>(args, 0);
    if (!inPasserArea(bulblin) || fopAcM_SearchByName(fpcNm_NPC_COACH_e) != nullptr) {
        return HOOK_CONTINUE;  // the real escort wagon (if any) keeps priority
    }
    g_bulblinTarget = travellers::nearestActor(bulblin->current.pos, 3000.0f);
    if (auto* passer = g_bulblinTarget) {
        const f32 dx = passer->current.pos.x - bulblin->current.pos.x;
        const f32 dz = passer->current.pos.z - bulblin->current.pos.z;
        if (dx * dx + dz * dz <= kBulblinTargetRadius * kBulblinTargetRadius ||
            lockedOnPasser(bulblin, passer)) {
            g_bulblinNearPasser = bulblin;
        }
    }
    return HOOK_CONTINUE;
}

// Test logging: once a second per nearby Bulblin, what it is aiming at and doing.
// Bulblins that have picked the passer keep chasing him (get_pla otherwise switches to Link as
// soon as he runs and Link becomes the closer one). Keyed by process ID, a few at a time.
struct BulblinLock {
    fpc_ProcID id = fpcM_ERROR_PROCESS_ID_e;
    int frames = 0;
};
std::array<BulblinLock, 8> g_bulblinLocks{};
constexpr f32 kBulblinChaseRadius = 5000.0f;
constexpr int kBulblinLockFrames = 900;  // forget him after 30 seconds without seeing him

BulblinLock* findLock(fpc_ProcID id, bool create) {
    BulblinLock* empty = nullptr;
    for (auto& lock : g_bulblinLocks) {
        if (lock.id == id) {
            return &lock;
        }
        if (empty == nullptr && (lock.id == fpcM_ERROR_PROCESS_ID_e || lock.frames <= 0)) {
            empty = &lock;
        }
    }
    if (create && empty != nullptr) {
        empty->id = id;
        empty->frames = 0;
        return empty;
    }
    return nullptr;
}

bool lockedOnPasser(const fopAc_ac_c* enemy, const daNpcPasser_c* passer) {
    if (enemy == nullptr || passer == nullptr) {
        return false;
    }
    auto* lock = findLock(fopAcM_GetID(const_cast<fopAc_ac_c*>(enemy)), false);
    return lock != nullptr && lock->frames > 0 &&
           enemy->current.pos.abs(passer->current.pos) <= kBulblinChaseRadius;
}

void onGetPlaPost(ModContext*, void* args, void* retval, void*) {
    auto* bulblin = mods::arg<fopAc_ac_c*>(args, 0);
    auto** target = static_cast<fopAc_ac_c**>(retval);
    if (target == nullptr || bulblin == nullptr || !inPasserArea(bulblin)) {
        return;
    }
    auto* passer = travellers::nearestActor(bulblin->current.pos, kBulblinChaseRadius);
    if (passer == nullptr) {
        return;
    }
    const fpc_ProcID id = fopAcM_GetID(bulblin);
    // Like the escort mission: go for the traveller when he's closer than Link.
    if (*target == dComIfGp_getPlayer(0)) {
        const f32 tx = passer->current.pos.x - bulblin->current.pos.x;
        const f32 tz = passer->current.pos.z - bulblin->current.pos.z;
        const f32 lx = (*target)->current.pos.x - bulblin->current.pos.x;
        const f32 lz = (*target)->current.pos.z - bulblin->current.pos.z;
        if (tx * tx + tz * tz < lx * lx + lz * lz && tx * tx + tz * tz < kBulblinTargetRadius * kBulblinTargetRadius) {
            *target = passer;
            travellers::noteEnemyRedirect();
        }
    }
    if (*target == passer) {
        if (auto* lock = findLock(id, true)) {
            lock->frames = kBulblinLockFrames;
        }
        return;
    }
    auto* lock = findLock(id, false);
    if (lock != nullptr && lock->frames > 0 &&
        bulblin->current.pos.abs(passer->current.pos) <= kBulblinChaseRadius) {
        *target = passer;
        travellers::noteEnemyRedirect();
    }
}

void onBulblinExecutePost(ModContext*, void* args, void*, void*) {
    auto* self = mods::arg<e_rd_class*>(args, 0);
    auto* selfActor = reinterpret_cast<fopAc_ac_c*>(self);
    if (auto* lock = findLock(fopAcM_GetID(selfActor), false);
        lock != nullptr && lock->frames > 0) {
        --lock->frames;
    }
    // It aimed at the passer this frame (its target distance is his distance): lock on.
    if (auto* passer = livePasser(); passer != nullptr && g_coachSwaps > 0) {
        const f32 toPasser = selfActor->current.pos.abs(passer->current.pos);
        if (std::fabs(self->dis - toPasser) < 150.0f) {
            if (auto* lock = findLock(fopAcM_GetID(selfActor), true)) {
                lock->frames = kBulblinLockFrames;
            }
        }
    }
    auto* bulblin = mods::arg<e_rd_class*>(args, 0);
    auto* actor = reinterpret_cast<fopAc_ac_c*>(bulblin);
    if (g_bulblinNearPasser == actor && (bulblin->counter % 30) == 0) {
        auto* passer = livePasser();
        auto* player = dComIfGp_getPlayer(0);
        if (passer != nullptr && player != nullptr) {
            char line[256];
            std::snprintf(line, sizeof(line),
                          "bulblin %u: toPasser=%.0f toLink=%.0f targetDis=%.0f range=%.0f "
                          "action=%d mode=%d swaps=%d",
                          static_cast<unsigned>(fopAcM_GetID(actor)),
                          actor->current.pos.abs(passer->current.pos),
                          actor->current.pos.abs(player->current.pos), bulblin->dis,
                          bulblin->attack_range, bulblin->action, bulblin->mode, g_coachSwaps);
            svc_log->info(mod_ctx, line);
        }
    }
    g_bulblinNearPasser = nullptr;
    g_bulblinTarget = nullptr;
    g_coachSwaps = 0;
}

// fopAcM_SearchByName(NPC_COACH) is an inline wrapper over fopAcIt_Judge(fpcSch_JudgeForPName).
void onActorJudgePost(ModContext*, void* args, void* retval, void*) {
    // Disabled: answering the escort-wagon lookup with a traveller also reached the Bulblin's
    // death/drop code (rd_disappear asks for the wagon too) and crashed when one was killed.
    // Targeting is done in get_pla instead (onGetPlaPost).
    (void)args; (void)retval;
    return;
    if (g_bulblinNearPasser == nullptr || retval == nullptr) {
        return;
    }
    // Match on the name searched for. The judge function's address can't be compared: inside a
    // mod it resolves to an import stub, never the game's own address.
    const auto* name = mods::arg<const s16*>(args, 1);
    if (name == nullptr || *name != fpcNm_NPC_COACH_e || *static_cast<void**>(retval) != nullptr) {
        return;
    }
    if (auto* passer = livePasser()) {
        *static_cast<void**>(retval) = passer;
        ++g_coachSwaps;
    }
}

// The traveller the Bulblin being updated is dealing with (set in its pre-update).
daNpcPasser_c* livePasser() {
    return g_bulblinTarget;
}

bool lockedOnPasser(const fopAc_ac_c* enemy, const daNpcPasser_c* passer);

// Enemies hunt travellers. Any enemy notices a traveller within kNoticeRadius that is closer to it
// than Link, and once it has picked one it stays on it (a "hunt") until the traveller is dead, more
// than kHuntRadius away, or it has gone kHuntFrames without seeing it close. While hunting, the
// enemy's home moves with it, so the usual "too far from home, walk back" leash doesn't pull it off
// the chase; when the hunt ends its real home is put back and it wanders home as normal.
constexpr f32 kNoticeRadius = 2500.0f;
constexpr f32 kHuntRadius = 5000.0f;
constexpr int kHuntFrames = 900;    // 30 s

struct Hunt {
    fpc_ProcID enemy = fpcM_ERROR_PROCESS_ID_e;
    fpc_ProcID prey = fpcM_ERROR_PROCESS_ID_e;
    int frames = 0;
    cXyz home{0.0f, 0.0f, 0.0f};
    s8 homeRoom = -1;
};
std::array<Hunt, 16> g_hunts{};

Hunt* findHunt(fpc_ProcID enemy) {
    for (auto& h : g_hunts) if (h.enemy == enemy && h.enemy != fpcM_ERROR_PROCESS_ID_e) return &h;
    return nullptr;
}

// Settled campers are left alone by enemies (a fighter who gets up to defend the fire is fair game).
bool passiveCamper(daNpcPasser_c* actor) {
    auto* state = findState(actor, false);
    return (state == nullptr || !state->defending) &&
           travellers::campInfo(actor, nullptr, nullptr, nullptr);
}

daNpcPasser_c* livePrey(fpc_ProcID id) {
    auto* actor = static_cast<daNpcPasser_c*>(fopAcM_SearchByID(id));
    if (actor == nullptr || !isPrototypePasser(actor) || passiveCamper(actor)) return nullptr;
    auto* state = findState(actor, false);
    return state != nullptr && state->dead ? nullptr : actor;
}

void endHunt(Hunt& h) {
    if (auto* enemy = fopAcM_SearchByID(h.enemy)) {
        enemy->home.pos = h.home;   // its real home again: it heads back there
    }
    h = Hunt{};
}

void startHunt(fopAc_ac_c* enemy, daNpcPasser_c* prey) {
    const fpc_ProcID id = fopAcM_GetID(enemy);
    if (Hunt* h = findHunt(id)) {
        h->prey = fopAcM_GetID(prey);
        h->frames = kHuntFrames;
        return;
    }
    for (auto& h : g_hunts) {
        if (h.enemy != fpcM_ERROR_PROCESS_ID_e) continue;
        h.enemy = id;
        h.prey = fopAcM_GetID(prey);
        h.frames = kHuntFrames;
        h.home = enemy->home.pos;
        travellers::noteHunt(enemy, prey, true);
        return;
    }
}

// Once a frame (mod_update): count hunts down, keep hunters' homes on them, end finished hunts.
void updateHunts() {
    for (auto& h : g_hunts) {
        if (h.enemy == fpcM_ERROR_PROCESS_ID_e) continue;
        auto* enemy = fopAcM_SearchByID(h.enemy);
        if (enemy == nullptr) { h = Hunt{}; continue; }   // the enemy itself is gone
        auto* prey = livePrey(h.prey);
        const f32 dist = prey != nullptr ? enemy->current.pos.abs(prey->current.pos) : 1e9f;
        if (prey != nullptr && dist <= kNoticeRadius) h.frames = kHuntFrames;   // still in sight
        if (prey == nullptr || dist > kHuntRadius || --h.frames <= 0) {
            travellers::noteHunt(enemy, prey, false);
            endHunt(h);
            continue;
        }
        enemy->home.pos = enemy->current.pos;
    }
}

// Is any enemy hunting this traveller?
bool isHunted(const daNpcPasser_c* passer) {
    const fpc_ProcID id = fopAcM_GetID(const_cast<daNpcPasser_c*>(passer));
    for (const auto& h : g_hunts) if (h.enemy != fpcM_ERROR_PROCESS_ID_e && h.prey == id) return true;
    return false;
}

daNpcPasser_c* nearestThreatTarget(const fopAc_ac_c* enemy) {
    if (enemy == nullptr || fopAcM_GetGroup(enemy) != fopAc_ENEMY_e || !inPasserArea(enemy)) {
        return nullptr;
    }
    auto* self = const_cast<fopAc_ac_c*>(enemy);
    if (Hunt* h = findHunt(fopAcM_GetID(self))) {
        if (auto* prey = livePrey(h->prey); prey != nullptr &&
            enemy->current.pos.abs(prey->current.pos) <= kHuntRadius) {
            return prey;
        }
    }
    pruneStates();
    daNpcPasser_c* result = nullptr;
    f32 nearestDistanceSq = kNoticeRadius * kNoticeRadius;
    for (auto& state : g_states) {
        daNpcPasser_c* actor = state.dead ? nullptr : liveActor(state);
        if (actor == nullptr || !isPrototypePasser(actor) || passiveCamper(actor)) {
            continue;
        }
        const f32 dx = actor->current.pos.x - enemy->current.pos.x;
        const f32 dz = actor->current.pos.z - enemy->current.pos.z;
        const f32 distanceSq = dx * dx + dz * dz;
        if (distanceSq <= nearestDistanceSq) {
            nearestDistanceSq = distanceSq;
            result = actor;
        }
    }
    if (result == nullptr) {
        // A Bulblin that has already gone for a traveller keeps reading it as "Link".
        if (auto* passer = travellers::nearestActor(enemy->current.pos, kBulblinChaseRadius);
            passer != nullptr && lockedOnPasser(enemy, passer)) {
            return passer;
        }
        return nullptr;
    }
    // Go for the traveller when it is closer than Link (Link still draws enemies off it).
    if (auto* player = dComIfGp_getPlayer(0)) {
        const f32 lx = player->current.pos.x - enemy->current.pos.x;
        const f32 lz = player->current.pos.z - enemy->current.pos.z;
        if (lx * lx + lz * lz < nearestDistanceSq) {
            return nullptr;
        }
    }
    startHunt(self, result);
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
    travellers::noteEnemyRedirect();
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
    // Target group 0x4 (plus the "set" bit), the same group as Link's own body. A hit only lands
    // when the attack's groups overlap the target's. Enemy attacks aim at group 0x4 (melee 0xD,
    // arrows 0x1D), so they all land; Link's attacks aim at 0x1A, so his never do. (Group 0x10,
    // the escort wagon's, only caught Bulblin arrows and Link's own sword.)
    // While Link is a wolf, fighters also take his bites (group 0x2, one of the groups his attacks
    // aim at): he can hurt them, never kill them, and they run (see the hit handler).
    passer->mCyl.SetTgSPrm(daPy_py_c::checkNowWolf() && travellers::isFighter(passer) ? 0x7 : 0x5);
    passer->mCyl.SetTgHitMark(CcG_Tg_UNK_MARK_1);
    passer->mCyl.SetTgSe(dCcD_SE_SOFT_BODY);
}

// Stop, face Link and blend quickly into the talk loop.
void setTalkAnimation(daNpcPasser_c* passer) {
    passer->speedF = 0.0f;
    if (auto* player = dComIfGp_getPlayer(0)) {
        const s16 toPlayer = cLib_targetAngleY(&passer->current.pos, &player->current.pos);
        passer->current.angle.y = toPlayer;
        passer->shape_angle.y = toPlayer;
    }
    auto* animation = static_cast<J3DAnmTransformKey*>(passer->getAnmP(kTalkAnimation,
                                                                       passer->getObjNum()));
    if (animation != nullptr) {
        passer->setAnm(animation, 1.0f, 3.0f, J3DFrameCtrl::EMode_LOOP, 0, -1);
    }
}

void beginDeath(daNpcPasser_c* passer, PasserState& state) {
    state.dead = true;
    state.deathFrame = 0;
    state.grounded = false;
    state.talking = false;
    state.tumble = 0;
    state.deathLanded = false;
    state.deathLie = 0;
    passer->health = 0;
    passer->attention_info.flags = 0;
    passer->mCyl.OffTgSetBit();
    passer->speedF = 0.0f;
    passer->speed.set(0.0f, 0.0f, 0.0f);

    // Launch away from the enemy that hit him, facing it, so the backflip carries him away.
    f32 dirX = -cM_ssin(passer->shape_angle.y);
    f32 dirZ = -cM_scos(passer->shape_angle.y);
    if (auto* enemy = nearestEnemy(passer, kThreatRadius)) {
        const f32 dx = passer->current.pos.x - enemy->current.pos.x;
        const f32 dz = passer->current.pos.z - enemy->current.pos.z;
        const f32 length = std::sqrt(dx * dx + dz * dz);
        if (length > 0.001f) {
            dirX = dx / length;
            dirZ = dz / length;
        }
    }
    state.knockbackX = dirX * kLaunchSpeedXZ;
    state.knockbackY = kLaunchSpeedY;
    state.knockbackZ = dirZ * kLaunchSpeedXZ;
    const s16 facing = cM_atan2s(-dirX, -dirZ);
    passer->shape_angle.y = facing;
    passer->current.angle.y = facing;

    if (!fighter_anim::fall(passer, false)) {
        auto* flinch = static_cast<J3DAnmTransformKey*>(passer->getAnmP(kLaunchAnimation, passer->getObjNum()));
        if (flinch != nullptr) {
            passer->setAnm(flinch, 1.0f, 3.0f, J3DFrameCtrl::EMode_NONE, 0, -1);
        }
    }
}

DEFINE_HOOK(&daNpcPasser_c::create_init, PasserCreateInit);
// execute(), callExecute(), setCollision() and setBaseMtx() are small and get inlined into the
// passer's profile entry in release builds, so hooks on them never fire. Hook the profile entry
// (called through the actor method table) and the action functions (called through a member
// function pointer table) instead; neither can be inlined away.
DEFINE_HOOK_SYMBOL("daNpcPasser_Execute", int(void*), PasserProfileExecute);
DEFINE_HOOK_SYMBOL("daNpcPasser_Delete", int(void*), PasserProfileDelete);
DEFINE_HOOK(&daNpcPasser_c::executePath, PasserExecutePath);
DEFINE_HOOK(&daNpcPasser_c::pathMoveF, PasserPathMove);
DEFINE_HOOK(&daNpcPasser_c::executeEscape, PasserExecuteEscape);
DEFINE_HOOK(&daNpcPasser_c::executeFear, PasserExecuteFear);
DEFINE_HOOK(&daNpcPasser_c::executeRelief, PasserExecuteRelief);
DEFINE_HOOK(&daNpcPasser_c::initEscape, PasserInitEscape);
DEFINE_HOOK(&dPath_GetRoomPath, RoomPathLookup);
// Bulblins (E_rd) already know how to fight something other than Link: in the escort mission
// get_pla() targets the wagon (NPC_COACH) whenever it is closer than Link. While a Bulblin is
// updating near the passer, answer its wagon lookup with the passer so it attacks him instead.
DEFINE_HOOK_SYMBOL("daE_RD_Execute", int(void*), BulblinExecute);
DEFINE_HOOK(&fopAcIt_Judge, ActorJudge);
DEFINE_HOOK_SYMBOL("src/d/actor/d_a_e_rd.cpp#get_pla", fopAc_ac_c*(fopAc_ac_c*), BulblinGetPla);
DEFINE_HOOK(&daNpcCd2_c::checkFearSituation, PasserFearCheck);
DEFINE_HOOK(&daTag_KMsg_c::Execute, TalkTagExecute);
DEFINE_HOOK(&fopAcM_searchActorAngleY, EnemyTargetAngleY);
DEFINE_HOOK(&fopAcM_searchActorAngleX, EnemyTargetAngleX);
DEFINE_HOOK(&fopAcM_seenActorAngleY, EnemyTargetSeenAngle);
DEFINE_HOOK(&fopAcM_searchActorDistance, EnemyTargetDistance);
DEFINE_HOOK(&fopAcM_searchActorDistance2, EnemyTargetDistanceSquared);
DEFINE_HOOK(&fopAcM_searchActorDistanceXZ, EnemyTargetDistanceXZ);
DEFINE_HOOK(&fopAcM_searchActorDistanceXZ2, EnemyTargetDistanceXZSquared);

// A traveller sets its walk path up inside create_init: open the path lookup just for that.
HookAction onCreateInitPre(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    if (isPrototypePasser(passer) || cook::isBody(passer)) {
        travellers::setPathLookupOpen(true);
    }
    return HOOK_CONTINUE;
}

void onCreateInitPost(ModContext*, void* args, void*, void*) {
    travellers::setPathLookupOpen(false);
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    if (!isPrototypePasser(passer)) {
        return;
    }
    auto* state = findState(passer, true);
    if (state == nullptr) {
        return;
    }

    passer->health = kStartingHealth;
    // Room 5 has invisible "ClearB" walls across the road that keep enemies from following Link
    // out of the field. Their polygons let Link through but block objects, so check walls the way
    // Link does and walk through them like he can.
    passer->mAcch.ClrObj();
    passer->mAcch.SetLink();
    // Battle attention makes this prototype passer lock-on targetable by Link.
    passer->attention_info.flags |= fopAc_AttnFlag_BATTLE_e;
    passer->attention_info.distances[fopAc_attn_BATTLE_e] = 180;
    setDamageable(passer);
}

// Runs at the start of every action function, after execute() has set this frame's wall check.
// Returns true when the action should be skipped (dead, talking or waiting at the road's end).
bool gateAction(daNpcPasser_c* passer) {
    if (cook::isBody(passer)) {
        return true;
    }
    if (!isPrototypePasser(passer)) {
        return false;
    }
    // The vanilla wall check is lower than the 41-unit step onto the bridge, so the passer
    // walked into it as a wall. The ground probe reaches 60 units; a 55-unit wall check lets
    // him step up like Link.
    passer->mAcchCir.SetWallH(kWallCheckHeight);
    const auto* state = findState(passer, false);
    return state != nullptr && (state->dead || state->talking || state->arrived);
}

bool isOldWoman(const daNpcPasser_c* passer) {
    return passer->m_type == 20 || passer->m_type == 27;   // WON_a, WON_a2
}

// Off screen, vanilla deletes a scared passer. Travellers never vanish just because the camera
// looked away: they only leave through a gate or loading zone, when their room unloads, or by dying.
void keepIfChased(daNpcPasser_c* passer) {
    if (isPrototypePasser(passer) && fopAcM_CheckCondition(passer, fopAcCnd_NODRAW_e)) {
        fopAcM_OffCondition(passer, fopAcCnd_NODRAW_e);
    }
}

// Vanilla deletes a passer the instant it reaches the end of its path, even on screen.
// Instead, stop at the end of the road and wait there until the camera can't see it.
HookAction onExecutePathPre(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    if (gateAction(passer)) {
        return HOOK_SKIP_ORIGINAL;
    }
    if (const auto* state = findState(passer, false); state != nullptr && state->standing) {
        return HOOK_SKIP_ORIGINAL;
    }
    if (!isPrototypePasser(passer) || passer->mPathMode != 2 || !passer->m_path.isPath()) {
        return HOOK_CONTINUE;
    }
    const int last = passer->m_path.mPath->m_num - 1;
    if (passer->m_path.field_0x20 < last) {
        return HOOK_CONTINUE;
    }
    // End of the stretch of road it was given: the traveller system decides what's next
    // (keep walking, rest at camp, or walk through a gate / loading screen and vanish).
    return travellers::onPathEnd(passer) ? HOOK_SKIP_ORIGINAL : HOOK_CONTINUE;
}

HookAction onInitEscapePre(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    if (cook::isBody(passer)) {
        return HOOK_SKIP_ORIGINAL;   // he runs from monsters his own way
    }
    // Run from the nearest enemy (or from Link, if it's him it's scared of).
    cXyz threat = passer->current.pos;
    if (auto* enemy = nearestEnemy(passer, kChasedRadius)) {
        threat = enemy->current.pos;
    } else if (auto* player = dComIfGp_getPlayer(0)) {
        threat = player->current.pos;
    }
    return travellers::startFlee(passer, threat) ? HOOK_SKIP_ORIGINAL : HOOK_CONTINUE;
}

// Vanilla deletes a fleeing passer when it reaches the end of its escape path or leaves the
// camera's view. Instead it keeps running out of view, and at the end of the path it stops,
// turns away from the danger and cowers until it is safe to walk on.
HookAction onExecuteEscapePre(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    if (gateAction(passer)) {
        return HOOK_SKIP_ORIGINAL;
    }
    auto* state = findState(passer, false);
    if (!isPrototypePasser(passer) || state == nullptr) {
        return HOOK_CONTINUE;
    }
    // Off screen, vanilla deletes a fleeing passer; let it (the roster keeps the traveller),
    // unless an enemy is chasing it.
    keepIfChased(passer);
    if (fopAcM_CheckCondition(passer, fopAcCnd_NODRAW_e)) {
        return HOOK_CONTINUE;
    }
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
    // Vanilla's checkPoint() also counts a point as reached once the distance budget it set
    // when heading for it (mNextPoint, reduced by speedF each frame) runs out, even if the
    // passer is still far away because obstacle avoidance or a wall slowed him down. On the
    // last point that deletes him on the spot, so catch that case here too.
    const bool budgetSpent = passer->m_path.mNextPoint - passer->speedF < 0.0f;
    if (dx * dx + dz * dz > reach * reach && !budgetSpent) {
        return HOOK_CONTINUE;
    }
    state->cowering = true;
    state->safeFrames = 0;
    passer->setAction(daNpcPasser_c::MODE_2);  // vanilla cower: stops and turns away from Link
    return HOOK_SKIP_ORIGINAL;
}

// Old women can't sprint: cap their flight speed (the run animation slows with it).
void onExecuteEscapePost(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    if (!isPrototypePasser(passer) || passer->field_0xb1c != 1) {
        return;
    }
    // Running away straight into a tent (or a rock) pinned them there until they died: when it is
    // stuck against something, it steps sideways off it, then runs on.
    if (auto* state = findState(passer, false)) {
        const bool pinned = passer->mAcchCir.ChkWallHit() && passer->current.pos.abs(passer->old.pos) < 3.0f;
        if (state->sidestep > 0) {
            sidestepTick(passer, *state, std::max(passer->speedF, Cd2_HIO_maxSpeed(passer->m_type)) * 0.8f);
        } else if (pinned) {
            if (++state->stuck >= kStuckFrames && startSidestep(passer, *state)) {
                travellers::noteAct(passer, "stepping off something it ran into");
            }
        } else if (state->stuck > 0) {
            --state->stuck;
        }
    }
    if (!isOldWoman(passer)) {
        return;
    }
    const f32 cap = Cd2_HIO_maxSpeed(passer->m_type) * 1.5f * kOldWomanRunScale;
    passer->speedF = std::min(passer->speedF, cap);
    passer->field_0xb14 = std::min(passer->field_0xb14, cap);
}

// The walk code sets the animation speed from how fast it's walking every frame; the fighters' own
// stances, draws and swings play at their own speed (running and limping keep the walk's).
void onPathMovePost(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    if (isPrototypePasser(passer) && fighter_anim::ownSpeed(passer)) {
        passer->mpMorf->setPlaySpeed(1.0f);
    }
}

HookAction onProfileDeletePre(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    if (isPrototypePasser(passer)) travellers::noteDelete(passer);
    return HOOK_CONTINUE;
}

// Cowering also deletes the passer once it is off screen; keep it.
HookAction onKeepWhenOffscreenPre(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    if (gateAction(passer)) {
        return HOOK_SKIP_ORIGINAL;
    }
    // Off screen, vanilla deletes a cowering passer; let it (the roster keeps the traveller),
    // unless an enemy is chasing it.
    if (isPrototypePasser(passer)) keepIfChased(passer);
    travellers::noteAct(passer, "scared");
    return HOOK_CONTINUE;
}

// The wall-bump pause only ends in vanilla by deleting the passer off screen. Keep it, and
// after three seconds put it back on its walking route instead of leaving it frozen.
HookAction onReliefPre(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    if (gateAction(passer)) {
        return HOOK_SKIP_ORIGINAL;
    }
    auto* state = findState(passer, false);
    if (!isPrototypePasser(passer) || state == nullptr) {
        return HOOK_CONTINUE;
    }
    // Off screen, vanilla deletes it from the wall-bump pause; let it (the roster keeps the
    // traveller), unless an enemy is chasing it.
    keepIfChased(passer);
    if (state->sidestep > 0) {
        // walking round it: along the thing it bumped into, then back on its road
        if (auto* walk = static_cast<J3DAnmTransformKey*>(passer->getAnmP(2, passer->getObjNum()))) {
            passer->setAnm(walk, 1.0f, 6.0f, J3DFrameCtrl::EMode_LOOP, 0, -1);
        }
        sidestepTick(passer, *state, Cd2_HIO_walkMaxSpeed(passer->m_type));
        if (state->sidestep == 0) {
            state->reliefFrames = 0;
            resumeRoute(passer, *state);
        }
        return HOOK_SKIP_ORIGINAL;
    }
    if (state->reliefFrames == 0 && !passer->checkFearSituation() && startSidestep(passer, *state)) {
        travellers::noteAct(passer, "walking round something it bumped into");
        return HOOK_SKIP_ORIGINAL;
    }
    travellers::noteAct(passer, "stuck on a wall");
    if (++state->reliefFrames >= kSafeFramesToResume && !passer->checkFearSituation()) {
        state->reliefFrames = 0;
        resumeRoute(passer, *state);
        return HOOK_SKIP_ORIGINAL;
    }
    return HOOK_CONTINUE;
}

void onRoomPathPost(ModContext*, void* args, void* retval, void*) {
    // Only our own walkers get a traveller path: the lookup is opened just while one of them sets
    // its path up. Every other actor gets the game's normal answer.
    if (retval == nullptr || !travellers::pathLookupOpen() || !travellers::inNetworkStage()) {
        return;
    }
    const int pathId = mods::arg<int>(args, 0);
    if (dPath* path = travellers::pathFor(pathId)) {
        *static_cast<dPath**>(retval) = path;
    }
}

// After the whole frame (setCollision and setBaseMtx have run): keep the passer hittable by
// enemies only, and tilt the model while it falls.
void onProfileExecutePost(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    auto* state = findState(passer, false);
    if (!isPrototypePasser(passer) || state == nullptr) {
        return;
    }
    if (!state->dead) {
        setDamageable(passer);
        keepGrounded(passer, *state);
        if (psr_tent::pushOff(passer, passer->mAcch)) {
            travellers::noteAct(passer, "stepped off a tent it had climbed");
        }
        fighter_anim::forcedTick(passer);   // a hit reaction beats the fight moves
        fighterAnim(passer, *state);
        blowTick(passer, *state);
        return;
    }
    if (passer->mpMorf == nullptr) {
        return;
    }

    fighter_anim::forcedTick(passer);   // the fly-back pose, then the sprawl
    // (No backflip any more: the fly-back pose does the work. tumble stays 0.)
    mDoMtx_stack_c::transS(passer->current.pos.x, passer->current.pos.y + kTumblePivotY,
                           passer->current.pos.z);
    mDoMtx_stack_c::YrotM(passer->shape_angle.y);
    mDoMtx_stack_c::XrotM(static_cast<s16>(-state->tumble));
    mDoMtx_stack_c::ZrotM(static_cast<s16>(state->tumble / 5));
    mDoMtx_stack_c::transM(0.0f, -kTumblePivotY, 0.0f);
    passer->mpMorf->getModel()->setBaseTRMtx(mDoMtx_stack_c::get());
    passer->mpMorf->modelCalc();
}

// --- camp guard --------------------------------------------------------------------------------
// Only for travellers settled at a camp for the night. Enemies leave them alone; when an enemy
// comes near the fire, fighters get up and go after it (and come back to their seat once it's
// dead or gone), everyone else cowers where they sit until it's been quiet for a few seconds.
constexpr f32 kCampGuardRadius = 1500.0f;   // an enemy this close to the fire wakes the camp
constexpr f32 kCampLeashRadius = 2200.0f;   // a defender chases it this far from the fire
constexpr f32 kStrikeReach = 150.0f;
constexpr f32 kLimpSpeed = 0.6f;
constexpr f32 kHurtFleeSpeed = 0.65f;       // fleeing on the last heart: this much of a full run
constexpr s16 kGuardTurn = 0x300;           // turning on guard: about 4 degrees a frame, shuffling
constexpr s16 kTurnShuffle = 0x800;         // more than ~11 degrees off: shuffle round to face it            // limping on the last heart, part of a walk
constexpr int kStrikeEvery = 75;            // frames between swings (each swing is two blows)
constexpr u8 kStrikePower = 2;              // a sword hit (10 damage to an enemy: a Bokoblin takes 4)
constexpr u8 kAnmIdle = 0, kAnmWalk = 2, kAnmRun = 4, kAnmCower = 0x26;

std::array<dCcD_Sph, 32> g_strike{};
const dCcD_SrcSph kStrikeLinkSrc = {
    {
        {0x0, {{AT_TYPE_NORMAL_SWORD, kStrikeLinkPower, 0xD}, {0x0, 0x0}, 0x0}},   // aimed like a monster's blow
        {dCcD_SE_SWORD, 0x0, 0x0, 0x0, 0x0},
        {0x0, 0x0, 0x0, 0x0, 0x0},
        {0x0},
    },
    {{{0.0f, 0.0f, 0.0f}, 70.0f}},
};
const dCcD_SrcSph kStrikeSrc = {
    {
        {0x0, {{AT_TYPE_NORMAL_SWORD, kStrikePower, 0x3}, {0x0, 0x0}, 0x0}},   // hits enemies only
        {dCcD_SE_SWORD, 0x0, 0x0, 0x0, 0x0},
        {0x0, 0x0, 0x0, 0x0, 0x0},
        {0x0},
    },
    {{{0.0f, 0.0f, 0.0f}, 70.0f}},
};

void guardAnim(daNpcPasser_c* passer, PasserState& state, u8 anm, f32 morf = 8.0f) {
    if (state.anim == anm) return;
    state.anim = anm;
    traveller_gear::sit(passer, false);   // up off its log
    if (auto* a = static_cast<J3DAnmTransformKey*>(passer->getAnmP(anm, passer->getObjNum()))) {
        passer->setAnm(a, 1.0f, morf, J3DFrameCtrl::EMode_LOOP, 0, -1);
    }
}

void stepToward(daNpcPasser_c* passer, const cXyz& to, f32 speed) {
    const s16 yaw = cLib_targetAngleY(&passer->current.pos, &to);
    cLib_addCalcAngleS2(&passer->current.angle.y, yaw, 4, 0x1000);
    passer->shape_angle.y = passer->current.angle.y;
    passer->current.pos.x += cM_ssin(passer->current.angle.y) * speed;
    passer->current.pos.z += cM_scos(passer->current.angle.y) * speed;
    passer->mAcch.CrrPos(dComIfG_Bgsp());
}

void strike(daNpcPasser_c* passer, PasserState& state) {
    dCcD_Sph& sph = g_strike[&state - g_states.data()];
    sph.Set(state.vsWolf ? kStrikeLinkSrc : kStrikeSrc);
    sph.SetStts(&passer->mStts);
    cXyz at = passer->current.pos;
    at.x += cM_ssin(passer->shape_angle.y) * 90.0f;
    at.y += 80.0f;
    at.z += cM_scos(passer->shape_angle.y) * 90.0f;
    sph.SetC(at);
    dComIfG_Ccsp()->Set(&sph);
    state.blowPending = true;
}

// The diary's record of every blow a fighter throws: what it hit (and its health), or that it
// missed; and a moment later, whether the target's health actually went down.
void blowTick(daNpcPasser_c* passer, PasserState& state) {
    const int i = static_cast<int>(&state - g_states.data());
    if (i < 0 || i >= static_cast<int>(g_strike.size())) return;
    char line[200];
    if (state.blowPending) {
        state.blowPending = false;
        dCcD_Sph& sph = g_strike[i];
        if (sph.ChkAtHit()) {
            cCcD_Obj* obj = sph.GetAtHitObj();
            fopAc_ac_c* who = obj != nullptr ? obj->GetAc() : nullptr;
            if (who != nullptr) {
                std::snprintf(line, sizeof(line), "landed on %u (%s) hp %d%s",
                              static_cast<unsigned>(fopAcM_GetID(who)), fopAcM_getProcNameString(who), who->health,
                              state.vsWolf ? " [wolf]" : "");
                state.blowTarget = fopAcM_GetID(who);
                state.blowHpBefore = who->health;
                state.blowCheck = 8;
            } else {
                std::snprintf(line, sizeof(line), "landed on something with no actor");
            }
            sph.ClrAtHit();
        } else {
            fopAc_ac_c* foe = nearestEnemy(passer, 400.0f);
            if (foe != nullptr) {
                std::snprintf(line, sizeof(line), "missed; nearest enemy %u (%s) at %.0f, %.0f up",
                              static_cast<unsigned>(fopAcM_GetID(foe)), fopAcM_getProcNameString(foe),
                              passer->current.pos.absXZ(foe->current.pos), foe->current.pos.y - passer->current.pos.y);
            } else {
                std::snprintf(line, sizeof(line), "missed; no enemy within 400");
            }
        }
        travellers::noteBlow(passer, line);
    }
    if (state.blowCheck > 0 && --state.blowCheck == 0) {
        fopAc_ac_c* who = fopAcM_SearchByID(state.blowTarget);
        if (who == nullptr) {
            std::snprintf(line, sizeof(line), "after: target %u gone (dead?)", static_cast<unsigned>(state.blowTarget));
        } else {
            std::snprintf(line, sizeof(line), "after: %u (%s) hp %d -> %d", static_cast<unsigned>(state.blowTarget),
                          fopAcM_getProcNameString(who), state.blowHpBefore, who->health);
        }
        travellers::noteBlow(passer, line);
    }
}

// The game's walking code is what pulls a traveller down onto the ground. While the mod holds a
// traveller still (standing its ground, at camp, waiting at the end of its road) that code is off,
// so they hung in the air over any dip: their own gravity then (not while sat on a stool).
void keepGrounded(daNpcPasser_c* passer, PasserState& state) {
    const bool heldByMod = state.standing || state.arrived || state.defending;
    if (!heldByMod || state.talking) return;
    const int slot = travellers::slotIndex(passer);
    if (slot >= 0 && traveller_gear::sitting(slot)) return;
    passer->speed.y = std::max(passer->speed.y - kGravity, -kMaxFall);
    passer->current.pos.y += passer->speed.y;
    passer->mAcch.CrrPos(dComIfG_Bgsp());
    if (passer->mAcch.ChkGroundHit()) passer->speed.y = 0.0f;
}

// Bumped into something (a tree, a tent, a rock): step along it, the side nearer the way it was
// going, for a moment. False if it isn't actually against anything.
bool startSidestep(daNpcPasser_c* passer, PasserState& state) {
    if (!passer->mAcchCir.ChkWallHit()) return false;
    const s16 wall = passer->mAcchCir.GetWallAngleY();
    const s16 a = static_cast<s16>(wall + 0x4000), b = static_cast<s16>(wall - 0x4000);
    const s16 heading = passer->shape_angle.y;
    s16 pick = std::abs(static_cast<s16>(a - heading)) <= std::abs(static_cast<s16>(b - heading)) ? a : b;
    if (state.stuck >= 2 * kStuckFrames) pick = pick == a ? b : a;   // that way didn't work: the other
    state.sideYaw = pick;
    state.sidestep = kSidestepFrames;
    return true;
}

void sidestepTick(daNpcPasser_c* passer, PasserState& state, f32 speed) {
    --state.sidestep;
    cLib_addCalcAngleS2(&passer->shape_angle.y, state.sideYaw, 3, 0x1000);
    passer->current.angle.y = passer->shape_angle.y;
    passer->current.pos.x += cM_ssin(passer->shape_angle.y) * speed;
    passer->current.pos.z += cM_scos(passer->shape_angle.y) * speed;
    passer->speed.y = std::max(passer->speed.y - kGravity, -kMaxFall);
    passer->current.pos.y += passer->speed.y;
    passer->mAcch.CrrPos(dComIfG_Bgsp());
    if (passer->mAcch.ChkGroundHit()) passer->speed.y = 0.0f;
    if (state.sidestep == 0) state.stuck = 0;
}

// A fighter in reach takes a swing: with its own animations the blows land on the swing's frames
// (fighterAnim); without them (#29 for now) the blow lands at once, as before. False while it's
// still busy (startled, drawing, mid-swing): try again next frame.
bool swingOrStrike(daNpcPasser_c* passer, PasserState& state) {
    if (!fighter_anim::has(passer)) {
        strike(passer, state);
        return true;
    }
    if (fighter_anim::busy(passer)) return false;
    state.swingReq = true;
    return true;
}

// Once per frame after the traveller's own action code: what its weapon and body should be doing.
void fighterAnim(daNpcPasser_c* passer, PasserState& state) {
    using fighter_anim::Want;
    if (!fighter_anim::has(passer)) {
        state.swingReq = false;
        return;
    }
    Want want = Want::Calm;
    if (state.talking) {
        want = Want::Calm;
    } else if (state.onGuard) {
        want = state.defending && state.chasing ? Want::Chase : Want::Ready;
    } else if (!holdsGround(passer)) {
        // down to its last heart
        if (passer->mActionIdx == daNpcPasser_c::MODE_1) {
            want = passer->field_0xb1c == 1 ? Want::Flee : Want::Keep;   // 0: vanilla's own startle first
        } else if (state.cowering || passer->mActionIdx == daNpcPasser_c::MODE_2) {
            want = Want::HurtWait;
        } else if (passer->mActionIdx == daNpcPasser_c::MODE_0 &&
                   !travellers::campInfo(passer, nullptr, nullptr, nullptr)) {
            want = Want::HurtWalk;
        }
    }
    if (fighter_anim::update(passer, want, state.swingReq, state.turning) > 0) {
        strike(passer, state);
    }
    state.swingReq = false;
    if (want == Want::HurtWalk) {
        // limping: slower than a walk (the limp's feet keep up)
        const f32 cap = Cd2_HIO_walkMaxSpeed(passer->m_type) * kLimpSpeed;
        passer->speedF = std::min(passer->speedF, cap);
        passer->field_0xb14 = std::min(passer->field_0xb14, cap);
    } else if (want == Want::Flee) {
        // fleeing hurt: the injured limp-run, slower than a full run (the animation slows with it)
        const f32 cap = Cd2_HIO_maxSpeed(passer->m_type) * 1.5f * kHurtFleeSpeed;
        passer->speedF = std::min(passer->speedF, cap);
        passer->field_0xb14 = std::min(passer->field_0xb14, cap);
    }
}

bool campGuard(daNpcPasser_c* passer, PasserState& state) {
    cXyz center, seat;
    s16 seatYaw = 0;
    if (!travellers::campInfo(passer, &center, &seat, &seatYaw)) {
        state.defending = false;
        state.campCower = false;
        return false;
    }
    const bool fighter = travellers::isFighter(passer);
    const f32 guardRadius = state.defending ? kCampLeashRadius : kCampGuardRadius;
    auto* enemy = nearestEnemyAt(center, guardRadius, fighter ? kReachAbove : 1.0e9f);
    if (fighter && !wolfShy(passer)) enemy = nearerFoe(center, enemy, wolfLinkNear(center, guardRadius));
    state.vsWolf = enemy != nullptr && enemy == dComIfGp_getPlayer(0);

    if (fighter && !holdsGround(passer)) {
        // down to its last heart: it runs (like anywhere else)
        if (enemy != nullptr && passer->mActionIdx != daNpcPasser_c::MODE_1) {
            state.defending = false;
            state.anim = 0xFF;
            travellers::noteCamp(passer, "runs from the fire, hurt, from", enemy);
            passer->setAction(daNpcPasser_c::MODE_1);
        }
        return enemy != nullptr;
    }

    if (fighter) {
        if (enemy != nullptr) {
            if (!state.defending) {
                state.defending = true;
                state.strikeTimer = 20;
                travellers::noteCamp(passer, "gets up to fight", enemy);
            }
            passer->speedF = 0.0f;
            const f32 dx = enemy->current.pos.x - passer->current.pos.x;
            const f32 dz = enemy->current.pos.z - passer->current.pos.z;
            const f32 dist = std::sqrt(dx * dx + dz * dz);
            state.chasing = dist > kStrikeReach;
            state.onGuard = true;
            if (fighter_anim::busy(passer)) {
                // startled, drawing or mid-swing: it stays where it is until that's done
            } else if (dist > kStrikeReach) {
                guardAnim(passer, state, kAnmRun);
                stepToward(passer, enemy->current.pos, Cd2_HIO_maxSpeed(passer->m_type) * 1.5f);
            } else {
                guardAnim(passer, state, kAnmIdle);
                const s16 yaw = cLib_targetAngleY(&passer->current.pos, &enemy->current.pos);
                state.turning = std::abs(static_cast<s16>(yaw - passer->shape_angle.y)) > kTurnShuffle;
                if (!fighter_anim::busy(passer)) {
                    cLib_addCalcAngleS2(&passer->current.angle.y, yaw, 4, fighter_anim::has(passer) ? kGuardTurn : 0x1000);
                }
                passer->shape_angle.y = passer->current.angle.y;
                if (--state.strikeTimer <= 0 && swingOrStrike(passer, state)) {
                    state.strikeTimer = kStrikeEvery;
                }
            }
            return true;
        }
        state.chasing = false;
        if (state.defending && fighter_anim::weaponOut(passer)) {
            // all clear: put the weapon away before walking back
            passer->speedF = 0.0f;
            return true;
        }
        if (state.defending) {
            // all clear: walk back to its seat and sit down again
            passer->speedF = 0.0f;
            const f32 dx = seat.x - passer->current.pos.x;
            const f32 dz = seat.z - passer->current.pos.z;
            if (dx * dx + dz * dz > 40.0f * 40.0f) {
                guardAnim(passer, state, kAnmWalk);
                stepToward(passer, seat, Cd2_HIO_walkMaxSpeed(passer->m_type));
                return true;
            }
            passer->current.pos.x = seat.x;
            passer->current.pos.z = seat.z;
            passer->current.angle.y = passer->shape_angle.y = seatYaw;
            state.defending = false;
            state.anim = 0xFF;
            travellers::refreshIdle(passer);
            travellers::noteCamp(passer, "is back at the fire", nullptr);
        }
        return false;
    }

    // everyone else cowers where they sit until it's been quiet for a few seconds
    if (enemy != nullptr && enemy->current.pos.abs(center) <= kCampGuardRadius) {
        if (!state.campCower) {
            state.campCower = true;
            travellers::noteCamp(passer, "cowers from", enemy);
        }
        state.calmFrames = 0;
        passer->speedF = 0.0f;
        guardAnim(passer, state, kAnmCower);
        return true;
    }
    if (state.campCower) {
        passer->speedF = 0.0f;
        if (++state.calmFrames < kSafeFramesToResume) return true;
        state.campCower = false;
        state.anim = 0xFF;
        travellers::refreshIdle(passer);
        travellers::noteCamp(passer, "calms down", nullptr);
    }
    return false;
}

HookAction onExecutePre(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    auto* state = findState(passer, false);
    if (!isPrototypePasser(passer) || state == nullptr) {
        return HOOK_CONTINUE;
    }

    (void)&logProgress;   // per-traveller logging off (too chatty with many travellers)

    if (state->dead) {
        ++state->deathFrame;
        if (!state->deathLanded) {
            passer->current.pos.x += state->knockbackX;
            passer->current.pos.y += state->knockbackY;
            passer->current.pos.z += state->knockbackZ;
            state->knockbackX *= kLaunchDrag;
            state->knockbackZ *= kLaunchDrag;
            state->knockbackY -= kLaunchGravity;
            passer->mAcch.CrrPos(dComIfG_Bgsp());
            if (passer->mAcch.ChkWallHit()) {
                // Slammed into a wall or cliff: drop straight down from there.
                state->knockbackX = 0.0f;
                state->knockbackZ = 0.0f;
            }
            if (passer->mAcch.ChkRoofHit() && state->knockbackY > 0.0f) {
                state->knockbackY = 0.0f;
            }
            const bool landed = state->knockbackY < 0.0f && passer->mAcch.ChkGroundHit();
            if (landed || state->deathFrame >= kDeathDuration) {
                // Down: the sprawl, then lie still for a moment.
                state->deathLanded = true;
                state->knockbackX = state->knockbackY = state->knockbackZ = 0.0f;
                fighter_anim::fall(passer, true);
            }
        } else if (fighter_anim::forcedDone(passer)) {
            ++state->deathLie;
        }
        passer->speedF = 0.0f;
        passer->speed.set(0.0f, 0.0f, 0.0f);
        if ((state->deathLanded && state->deathLie >= kDeathLie) || state->deathFrame >= kDeathLimit) {
            // Gone in a puff of smoke: the same puff a treasure chest appears in (the game's own,
            // always-loaded effect), and the soul rises out of it.
            {
                static const u16 kChestPuff[6] = {0x8840, 0x8841, 0x8842, 0x8843, 0x8844, 0x8845};
                cXyz at(passer->current.pos.x, passer->current.pos.y + 55.0f, passer->current.pos.z);
                csXyz rot = passer->shape_angle;
                cXyz scale(1.0f, 1.0f, 1.0f);
                for (u16 id : kChestPuff) {
                    dComIfGp_particle_set(id, &at, &rot, &scale, 0xFF, nullptr, -1, nullptr, nullptr, nullptr);
                }
                mDoAud_seStart(Z2SE_OBJ_T_BOX_EMERGE, &at, 0, dComIfGp_getReverb(fopAcM_GetRoomNo(passer)));
            }
            psr_soul::spawn(passer->current.pos, fopAcM_GetRoomNo(passer));
            travellers::onDeath(passer);
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


    // Settled at a camp for the night: the camp guard decides (defend the fire / cower / sit).
    bool campHandled = false;
    state->onGuard = false;
    state->turning = false;
    if (!state->talking && !state->cowering) {
        campHandled = campGuard(passer, *state);
    }

    // Fighters stop, face the enemy and stand their ground while they're healthy.
    if (!campHandled && !state->talking && !state->cowering &&
        !travellers::campInfo(passer, nullptr, nullptr, nullptr)) {
        auto* enemy = holdsGround(passer) ? nearestEnemyAt(passer->current.pos, kThreatRadius, kReachAbove) : nullptr;
        if (holdsGround(passer) && !wolfShy(passer)) {
            enemy = nearerFoe(passer->current.pos, enemy, wolfLinkNear(passer->current.pos, kThreatRadius));
        }
        state->vsWolf = enemy != nullptr && enemy == dComIfGp_getPlayer(0);
        if (enemy != nullptr) {
            state->foeLost = 0;
        } else if (state->standing && holdsGround(passer) && ++state->foeLost < kFoeLostFrames) {
            // lost sight of it for a moment (it circled out, went behind something): stay on guard
            state->onGuard = true;
            passer->speedF = 0.0f;
        }
        if (enemy != nullptr) {
            state->onGuard = true;
            passer->speedF = 0.0f;
            const s16 toEnemy = cLib_targetAngleY(&passer->current.pos, &enemy->current.pos);
            state->turning = std::abs(static_cast<s16>(toEnemy - passer->shape_angle.y)) > kTurnShuffle;
            if (!fighter_anim::busy(passer)) {
                cLib_addCalcAngleS2(&passer->current.angle.y, toEnemy, 4, fighter_anim::has(passer) ? kGuardTurn : 0x800);
            }
            passer->shape_angle.y = passer->current.angle.y;
            if (!state->standing) {
                state->standing = true;
                travellers::noteAct(passer, "standing its ground");
                travellers::noteStand(passer, enemy);
                // fighters with their own animations square up (fighter_anim); the others stand idle
                if (!fighter_anim::has(passer)) {
                    if (auto* idle = static_cast<J3DAnmTransformKey*>(passer->getAnmP(0, passer->getObjNum()))) {
                        passer->setAnm(idle, 1.0f, 6.0f, J3DFrameCtrl::EMode_LOOP, 0, -1);
                    }
                }
            }
            // in reach: swing at it (the blow lands on the swing's own frames)
            const f32 dx = enemy->current.pos.x - passer->current.pos.x;
            const f32 dz = enemy->current.pos.z - passer->current.pos.z;
            if (dx * dx + dz * dz <= kStrikeReach * kStrikeReach) {
                if (--state->strikeTimer <= 0 && swingOrStrike(passer, *state)) {
                    state->strikeTimer = kStrikeEvery;
                }
            } else {
                state->strikeTimer = std::min(state->strikeTimer, 20);
            }
        } else if (state->onGuard) {
            // still on guard (see above)
        } else if (state->standing && fighter_anim::weaponOut(passer)) {
            passer->speedF = 0.0f;   // put the weapon away first, then walk on
        } else if (state->standing) {
            state->standing = false;
            if (!state->arrived) {
                resumeRoute(passer, *state);
            }
        }
    }

    if (state->invuln > 0) {
        --state->invuln;
    }
    if (passer->mCyl.ChkTgHit() && state->invuln > 0) {
        passer->mCyl.ResetTgHit();   // the same swing is still touching: one swing, one hit
    } else if (passer->mCyl.ChkTgHit()) {
        // Only enemies may hurt the passer. Link's sword, arrows, bombs, boomerang and
        // other player-owned actors all report a non-enemy attacker and are ignored.
        auto* attacker = passer->mCyl.GetTgHitAc();
        passer->mCyl.ResetTgHit();
        const bool byWolf = attacker != nullptr && attacker == dComIfGp_getPlayer(0) &&
                            daPy_py_c::checkNowWolf() && travellers::isFighter(passer);
        if (!byWolf && (attacker == nullptr || !fopAcM_IsActor(attacker) ||
                        fopAcM_GetGroup(attacker) != fopAc_ENEMY_e)) {
            return HOOK_CONTINUE;
        }
        // Damage in quarter hearts, the same amount the attack would take from Link.
        cCcD_Obj* hitBy = passer->mCyl.GetTgHitObj();
        const int damage = std::max<int>(1, hitBy != nullptr ? hitBy->GetAtAtp() : 1);
        int life = travellers::life(passer);
        if (life < 0) {
            life = state->health * kQuartersPerHeart;   // not on the roster (old prototype walker)
        }
        // the wolf hurts, but never kills: at least a quarter heart is always left
        life = byWolf ? std::max(1, life - damage) : std::max(0, life - damage);
        travellers::setLife(passer, life);
        state->health = (life + kQuartersPerHeart - 1) / kQuartersPerHeart;
        passer->health = static_cast<s16>(state->health);
        travellers::noteHit(passer, damage, life);
        state->invuln = kHurtInvulnFrames;
        if (life > 0) fighter_anim::hit(passer, attacker->current.pos);   // the blow shows: a stagger
        if (byWolf) {
            // bitten: from now on it's like any other traveller with the wolf: runs from him, never fights
            fighter_anim::drop(passer);
            if (const int slot = travellers::slotIndex(passer); slot >= 0 && slot < travellers::kSlots) {
                g_wolfShyUntil[slot] = std::chrono::steady_clock::now() + kWolfShyTime;
            }
            state->standing = false;
            state->defending = false;
            state->campCower = false;
            state->arrived = false;
            state->cowering = false;
            state->anim = 0xFF;
            traveller_gear::sit(passer, false);
            travellers::noteAct(passer, "bitten by wolf Link, running");
            passer->setAction(daNpcPasser_c::MODE_1);
            return HOOK_CONTINUE;
        }
        if (life <= 0) {
            beginDeath(passer, *state);
        } else if (holdsGround(passer)) {
            // a fighter takes it and stays put
        } else if (!travellers::isFighter(passer) &&
                   travellers::campInfo(passer, nullptr, nullptr, nullptr)) {
            // a camper who can't fight keeps cowering by the fire (the camp guard)
        } else if (nearestEnemy(passer, kThreatRadius) != nullptr) {
            state->standing = false;
            state->arrived = false;  // running away replaces waiting at the end of the road
            state->cowering = false;
            passer->setAction(daNpcPasser_c::MODE_1);
        }
    }
    return HOOK_CONTINUE;
}

// A fighter stands its ground while it has more than one heart left.
bool holdsGround(const daNpcPasser_c* passer) {
    return travellers::isFighter(passer) && travellers::life(passer) > kQuartersPerHeart;
}

void onFearCheckPost(ModContext*, void* args, void* retval, void*) {
    auto* passer = static_cast<daNpcPasser_c*>(mods::arg<daNpcCd2_c*>(args, 0));
    if (retval != nullptr && isPrototypePasser(passer) && wolfShy(passer) &&
        wolfLinkNear(passer->current.pos, kThreatRadius) != nullptr) {
        return;   // bitten once: afraid of the wolf like any non-fighter (the game's own fear decides)
    }
    if (retval != nullptr && isPrototypePasser(passer) && holdsGround(passer)) {
        *static_cast<bool*>(retval) = false;
        return;
    }
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
                // A talk event only lets its two participants (Link and this marker) move, so the
                // passer froze for the whole conversation. Let him keep animating while it runs.
                fopAcM_OnStatus(passer, fopAcStts_NOPAUSE_e);
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
    // Wolf Link can't talk to people; to the passer he is something to run from.
    if (daPy_py_c::checkNowWolf()) {
        tag->attention_info.flags = 0;
    }
    if (auto* state = findState(passer, false); state != nullptr && state->talking &&
        !tag->eventInfo.checkCommandTalk()) {
        // Conversation over: put the walk (or wait) animation back instead of looping the talk.
        state->talking = false;
        fopAcM_OffStatus(passer, fopAcStts_NOPAUSE_e);
        if (state->arrived) {
            auto* idle = static_cast<J3DAnmTransformKey*>(passer->getAnmP(0, passer->getObjNum()));
            if (idle != nullptr) {
                passer->setAnm(idle, 1.0f, 8.0f, J3DFrameCtrl::EMode_LOOP, 0, -1);
            }
        } else if (state->cowering) {
            passer->setAction(daNpcPasser_c::MODE_2);
        } else {
            resumeRoute(passer, *state);
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

uint16_t cookFlowNode_(int line) {
    return g_cookNodes[std::clamp(line, 0, kCookLineCount - 1)];
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

    // The camp cook's lines: each greedy try gets the next one; the last comes with the spoon.
    static const char* const kCookLines[kCookLineCount][2] = {
        {"HEY!", " Don't be greedy!"},
        {"I SAID", " don't be greedy! That soup's for everyone!"},
        {"Last warning, kid.", " Put. The bottle. Down."},
        {"THAT'S IT!", ""},
    };
    for (int i = 0; i < kCookLineCount; ++i) {
        mods::flow::MessageBuilder line;
        line.box_kind(MESSAGE_BOX_TALK).text_scale(140).text(kCookLines[i][0]).text_scale(100).text(kCookLines[i][1]);
        std::vector<mods::flow::MessageVariant> variants;
        for (const auto language : kLanguages) {
            variants.push_back(line.build(language));
        }
        g_cookLines[i] = mods::flow::register_message(kTalkFlowGroup, variants);
        if (!g_cookLines[i]) {
            return g_cookLines[i].result();
        }
    }

    mods::flow::GraphBuilder graph{kTalkFlowGroup};
    auto line = graph.add_message(g_greeting.id());
    g_talkNode = line.id();
    line.next(mods::flow::kEnd);
    for (int i = 0; i < kCookLineCount; ++i) {
        auto yell = graph.add_message(g_cookLines[i].id());
        g_cookNodes[i] = yell.id();
        yell.next(mods::flow::kEnd);
    }
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
        // Test enemy: a club Bulblin on foot (E_rd: no switch, arg0 0 = on foot, weapon 1 = club)
        // standing mid-bridge, facing the canyon the passer walks out of.
        makeRecord("E_rd", 0xFF00F100u, kBridgeBulblinPos, 0, 0, 0),
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

uint16_t cookFlowNode(int line) { return cookFlowNode_(line); }

// The nearest monster to a spot (for the cook, who runs from them).
fopAc_ac_c* nearestMonster(const cXyz& at, f32 maxDistance) { return nearestEnemyAt(at, maxDistance); }

extern "C" {
MOD_EXPORT ModResult mod_initialize(ModError* error) {
    // The single room-5 test passer (and its talk marker and test Bulblin) is replaced by the
    // traveller system. Talking comes back per traveller later.
    (void)&registerGreetingFlow;
    (void)&addRoomActors;
    (void)&buildRoute;
    travellers::initialize();

    if (mods::hook::add_post<RoomPathLookup>(onRoomPathPost) != MOD_OK ||
        mods::hook::add_pre<PasserExecutePath>(onExecutePathPre) != MOD_OK ||
        mods::hook::add_pre<PasserExecuteEscape>(onExecuteEscapePre) != MOD_OK ||
        mods::hook::add_post<PasserExecuteEscape>(onExecuteEscapePost) != MOD_OK ||
        mods::hook::add_pre<PasserExecuteFear>(onKeepWhenOffscreenPre) != MOD_OK ||
        mods::hook::add_pre<PasserExecuteRelief>(onReliefPre) != MOD_OK ||
        mods::hook::add_pre<PasserInitEscape>(onInitEscapePre) != MOD_OK ||
        mods::hook::add_pre<PasserCreateInit>(onCreateInitPre) != MOD_OK ||
        mods::hook::add_post<PasserCreateInit>(onCreateInitPost) != MOD_OK ||
        mods::hook::add_pre<PasserProfileExecute>(onExecutePre) != MOD_OK ||
        mods::hook::add_pre<BulblinExecute>(onBulblinExecutePre) != MOD_OK ||
        mods::hook::add_post<BulblinExecute>(onBulblinExecutePost) != MOD_OK ||
        (static_cast<void>(&onActorJudgePost), false) ||
        mods::hook::add_post<PasserProfileExecute>(onProfileExecutePost) != MOD_OK ||
        mods::hook::add_post<PasserFearCheck>(onFearCheckPost) != MOD_OK ||
        mods::hook::add_post<EnemyTargetAngleY>(onEnemyTargetAngleYPost) != MOD_OK ||
        mods::hook::add_post<EnemyTargetAngleX>(onEnemyTargetAngleXPost) != MOD_OK ||
        mods::hook::add_post<EnemyTargetSeenAngle>(onEnemyTargetSeenAnglePost) != MOD_OK ||
        mods::hook::add_post<EnemyTargetDistance>(onEnemyTargetDistancePost) != MOD_OK ||
        mods::hook::add_post<EnemyTargetDistanceSquared>(onEnemyTargetDistanceSquaredPost) != MOD_OK ||
        mods::hook::add_post<EnemyTargetDistanceXZ>(onEnemyTargetDistanceXZPost) != MOD_OK ||
        mods::hook::add_post<EnemyTargetDistanceXZSquared>(onEnemyTargetDistanceXZSquaredPost) != MOD_OK) {
        return mods::set_error(error, MOD_UNAVAILABLE, "failed to install passer prototype hooks");
    }

    // Optional: keeps Bulblins chasing the passer once they've picked him. If get_pla can't be
    // hooked (missing from the symbol manifest), the passer still works with nearest-target only.
    // Optional: without it the old men's hunch stacks on the fight animations.
    if (!fighter_anim::install()) {
        svc_log->info(mod_ctx, "joint tweak hook unavailable; fight animations keep the body tweaks");
    }
    // Optional: without it the fighters' stances and swings play at the walk's speed.
    if (mods::hook::add_post<PasserPathMove>(onPathMovePost) != MOD_OK) {
        svc_log->info(mod_ctx, "passer walk-speed hook unavailable; fight animations follow the walk speed");
    }
    if (mods::hook::add_pre<PasserProfileDelete>(onProfileDeletePre) != MOD_OK) {
        svc_log->info(mod_ctx, "passer delete hook unavailable; vanished travellers won't be traced");
    }
    if (mods::hook::add_post<BulblinGetPla>(onGetPlaPost) != MOD_OK) {
        svc_log->info(mod_ctx, "Bulblin get_pla hook unavailable; Bulblins target whoever is closer");
    }
    if (!traveller_gear::install()) {
        return mods::set_error(error, MOD_UNAVAILABLE, "failed to install the traveller look hooks");
    }
    if (cook::registerActor() != MOD_OK) {
        return mods::set_error(error, MOD_ERROR, "failed to register the camp cook");
    }
    if (psr_fire::registerActor() != MOD_OK) {
        return mods::set_error(error, MOD_ERROR, "failed to register the camp fire actor");
    }
    if (psr_soul::registerActor() != MOD_OK) {
        return mods::set_error(error, MOD_ERROR, "failed to register the soul actor");
    }
    if (psr_tent::registerActor() != MOD_OK) {
        return mods::set_error(error, MOD_ERROR, "failed to register the tent actor");
    }
    if (psr_spawner::registerAndPlace() != MOD_OK) {
        return mods::set_error(error, MOD_ERROR, "failed to register or place the traveller spawners");
    }
    spot_picker::initialize();
    svc_log->info(mod_ctx, "Dynamic Travelers ready: travellers walk the Hyrule Field road network");
    return MOD_OK;
}

MOD_EXPORT ModResult mod_update(ModError*) {
    spot_picker::update();
    updateHunts();
    travellers::update();
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    g_talkGraph.reset();
    g_greeting = {};
    g_talkNode = 0xFFFF;
    return MOD_OK;
}
}

