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
#include "voice_banks.hpp"
#include "camp_music.hpp"
#include "mod_sounds.hpp"
#include "shop_stage.hpp"
#include "soul_actor.hpp"
#include "note_actor.hpp"
#include "bubble_actor.hpp"
#include "traveller_gear.hpp"
#include "fighter_anim.hpp"
#include "camp_fire.hpp"
#include "camp_cook.hpp"
#include "travellers.hpp"
#include "freeze.hpp"

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
constexpr int kShooterFrames = 600;   // twenty seconds after the last arrow, a fighter keeps after the archer
// Old women run at this share of a normal traveller's flight speed.
constexpr f32 kOldWomanRunScale = 0.85f;
// The body's own voice lines (what a townsperson says when you talk to them): 1 surprise, 2 fear, 3 hey.
constexpr int kVoiceShout = 3, kVoiceYelp = 1, kVoiceFear = 2;
constexpr u16 kDeathSmoke = 0x0108;   // in our own effect pack (psr_fx.jpc): the white death smoke
constexpr f32 kFellThrough = 1500.0f;   // this far below its last footing it has fallen through the world
constexpr f32 kTidyDistance = 6000.0f;   // off screen and this far from Link: the game may tidy it away
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
    int chatFrames = 0;       // a seated camper talking: frames left
    int mealFrames = 0;       // a seated camper eating or drinking: frames left
    bool wasScared = false;   // running or cowering last frame (to scream once as it starts)
    cXyz lastGround{};      // where it last stood on solid ground (to catch it falling through the world)
    bool hasGround = false;
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
    fpc_ProcID shooter = fpcM_ERROR_PROCESS_ID_e;   // a healthy fighter shot from afar: the archer it goes after
    int shooterFrames = 0;                          // ... for this long since the last arrow
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
    fpc_ProcID avoidId = fpcM_ERROR_PROCESS_ID_e;   // who it is stepping round (keeps to one side of them)
    s8 avoidSide = 0;                               // +1 passes them on its left, -1 on its right
    cXyz progFrom;          // where it was at the start of the progress check
    int progFrames = 0;     // frames into the progress check
    int stuckRuns = 0;      // progress checks in a row it failed
    int voiceWait = 0;      // frames until it may call out again (attack shout, hurt yelp)
    // the old women's freeze spell
    int hitsThisFight = 0;  // blows taken since it was last quiet
    int quietFrames = 0;
    int castCooldown = 0;   // frames until she can cast again
    u8 casting = 0;         // 1 turning to face it, 2 casting
    bool castFired = false;
    fpc_ProcID castTarget = fpcM_ERROR_PROCESS_ID_e;
    u8 fetching = 0;
    bool toSeat = false;    // a camper walking back to its seat (only sits once it's there)
    bool playing = false;   // the bard, playing by the fire
    int songFrames = 0;     // a camper singing along: frames left (0 not singing)
    int spellTick = 0;      // an old woman's spell building: frames (its glow pulses every few)
    int noteWait = 0;       // frames until the next music note floats off it        // a fighter getting its torch back after a fight: 1 walking to it, 2 picking it up
    int fetchFrames = 0;
    // needs (1 full, 0 empty): they drain, and a seated camper acts on the lowest (needTick, chatTick)
    f32 hunger = -1.0f;     // -1: not rolled yet
    f32 thirst = 1.0f;
    f32 social = 1.0f;
    int pending = 0;        // an action thought of, waiting for its bubble: 1 eat, 2 drink, 3 chat
    int laughIn = 0;        // someone at its fire told a joke: frames until it laughs (or doesn't get it)
    int laughFrames = 0;    // laughing: frames left
    int pendingWait = 0;
    bool mealIsFood = false;
    u8 bedStep = 0;         // bedtime: 1 crouching at the door, 2 crawling in; 10 crawling out, 11 standing up
    int bedFrames = 0;
    // its bubble (bubble_actor): the one over its head, how much it matters, and per-icon rests
    fpc_ProcID bubble = fpcM_ERROR_PROCESS_ID_e;
    int bubblePri = 0;
    std::array<s16, psr_bubble::kCount> bubbleRest{};
};

std::array<PasserState, 32> g_states{};

// --- bubbles and needs --------------------------------------------------------------------
// One bubble at a time over a traveller: fear and fighting beat everything, a muddle beats a need.
int bubblePriority(int icon) {
    using namespace psr_bubble;
    switch (icon) {
    case kScared: case kAngry: case kAttack: case kFlee: case kDefend: case kSurprised: return 3;
    case kConfused: case kSad: return 2;
    default: return 1;
    }
}
constexpr s16 kBubbleRest = 240;   // the same bubble no more than every 8 seconds
void say(daNpcPasser_c* passer, PasserState& state, int icon) {
    if (icon < 0 || icon >= psr_bubble::kCount || state.bubbleRest[icon] > 0) return;
    const int pri = bubblePriority(icon);
    if (psr_bubble::follow(state.bubble, passer->eyePos) && state.bubblePri > pri) return;
    psr_bubble::remove(state.bubble);
    cXyz pos = passer->eyePos;
    pos.y += 38.0f;
    state.bubble = psr_bubble::spawn(pos, fopAcM_GetRoomNo(passer), icon);
    state.bubblePri = pri;
    state.bubbleRest[icon] = kBubbleRest;
}
void bubbleTick(daNpcPasser_c* passer, PasserState& state) {
    for (s16& r : state.bubbleRest) if (r > 0) --r;
    if (state.bubble == fpcM_ERROR_PROCESS_ID_e) return;
    cXyz pos = passer->eyePos;
    pos.y += 38.0f;
    if (!psr_bubble::follow(state.bubble, pos)) {
        state.bubble = fpcM_ERROR_PROCESS_ID_e;
        state.bubblePri = 0;
    }
}
// How fast each need drains, by who they are (personality comes with the character type).
// Full to empty in about two minutes of hunger, a minute and a half of thirst, a minute of company.
void needTick(daNpcPasser_c* passer, PasserState& state) {
    if (state.hunger < 0.0f) {   // everyone arrives a little different
        state.hunger = 0.7f + cM_rndF(0.3f);
        state.thirst = 0.7f + cM_rndF(0.3f);
        state.social = 0.6f + cM_rndF(0.4f);
    }
    const int t = passer->m_type;
    f32 h = 1.0f, d = 1.0f, c = 1.0f;
    if (t == 3 || t == 11) d = 1.6f;                         // the dwarves: always thirsty
    if (travellers::isFighter(passer)) h = 1.3f;             // fighters: hungry work
    if (t == 20 || t == 27) { c = 1.3f; h = 0.8f; }          // the old women: love a chat, eat little
    if (traveller_gear::isBard(passer)) c = 0.7f;            // the bard: happy with his guitar
    // full to empty: about four minutes of hunger, three of thirst, two of company
    state.hunger = std::fmax(0.0f, state.hunger - h / 7200.0f);
    state.thirst = std::fmax(0.0f, state.thirst - d / 5400.0f);
    state.social = std::fmax(0.0f, state.social - c / 3600.0f);
}
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
DEFINE_HOOK(&PathTrace_c::checkObstacle, PathObstacle);
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
    return state != nullptr && (state->dead || state->talking || state->arrived || state->casting != 0);
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
    if (const auto* state = findState(passer, false); state != nullptr && (state->standing || state->fetching)) {
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

// --- getting round people, and not getting stuck --------------------------------------------------
// The game's own avoidance (PathTrace_c::checkObstacle) treats Link or any passer anywhere near the
// line to the next road point as in the way, and aims at a spot 250 units beside them that moves
// with them. Close to Link, or with other travellers fleeing alongside, they circled round and
// round instead of getting anywhere. Ours: only someone close and actually ahead counts, it keeps
// to the same side of them until it is past, and a fleeing traveller only steps round Link (the
// others are running too). Everyone else (town folk, the cook) keeps the game's own.
constexpr f32 kAvoidNear = 220.0f;          // only someone this close is in the way
constexpr s16 kAvoidCone = 0x2800;          // ... and within about 56 degrees of the way it is going
constexpr f32 kAvoidPass = 160.0f;          // aim this far out to the side of them
constexpr f32 kAvoidAhead = 120.0f;         // ... and this far past them
constexpr int kProgressFrames = 90;         // every three seconds on the move ...
constexpr f32 kProgressMin = 60.0f;         // ... it must have got at least this far from where it was

struct AvoidSearch {
    daNpcPasser_c* self;
    cXyz target;
    bool fleeing;
    fopAc_ac_c* best;
    f32 bestDist;
};

void* avoidCandidate(void* proc, void* data) {
    auto* s = static_cast<AvoidSearch*>(data);
    if (!fopAcM_IsActor(proc) || proc == s->self) return nullptr;
    auto* ac = static_cast<fopAc_ac_c*>(proc);
    const s16 name = fopAcM_GetName(ac);
    const bool isLink = name == fpcNm_ALINK_e && !dKy_darkworld_check();
    const bool isPerson = name == fpcNm_NPC_CD3_e || name == fpcNm_NPC_PASSER_e || name == fpcNm_NPC_GUARD_e ||
                          name == fpcNm_NPC_FGUARD_e;
    if (!isLink && !(isPerson && !s->fleeing)) return nullptr;
    const cXyz& me = s->self->current.pos;
    const f32 d = me.absXZ(ac->current.pos);
    if (d > kAvoidNear || d >= me.absXZ(s->target) || d >= s->bestDist) return nullptr;
    const s16 toThem = cLib_targetAngleY(&me, &ac->current.pos);
    const s16 toGoal = cLib_targetAngleY(&me, &s->target);
    if (std::abs(static_cast<s16>(toThem - toGoal)) > kAvoidCone) return nullptr;   // beside or behind it
    s->best = ac;
    s->bestDist = d;
    return nullptr;
}

HookAction onCheckObstaclePre(ModContext*, void* args, void* retval, void*) {
    auto* trace = mods::arg<PathTrace_c*>(args, 0);
    auto* me = mods::arg<fopAc_ac_c*>(args, 1);
    if (trace == nullptr || me == nullptr || !isPrototypePasser(me) || !trace->isPath()) return HOOK_CONTINUE;
    auto* passer = static_cast<daNpcPasser_c*>(me);
    trace->mMyself = me;
    trace->mObstacle = nullptr;
    PasserState* state = findState(passer, false);
    if (state != nullptr && !fopAcM_CheckCondition(me, 4)) {
        AvoidSearch s{passer, trace->getPoint(static_cast<u16>(trace->field_0x20)),
                      passer->mActionIdx == daNpcPasser_c::MODE_1, nullptr, kAvoidNear};
        fpcM_Search(avoidCandidate, &s);
        if (s.best != nullptr) {
            const cXyz& at = passer->current.pos;
            const s16 toThem = cLib_targetAngleY(&at, &s.best->current.pos);
            if (state->avoidId != fopAcM_GetID(s.best) || state->avoidSide == 0) {
                // first time round this one: the side the road already leans to (as the game does)
                const s16 diff = static_cast<s16>(toThem - cLib_targetAngleY(&at, &s.target));
                state->avoidId = fopAcM_GetID(s.best);
                state->avoidSide = diff > 0 ? -1 : 1;
            }
            const s16 side = static_cast<s16>(toThem + state->avoidSide * 0x4000);
            const cXyz& them = s.best->current.pos;
            trace->mAvoidPoint.x = them.x + cM_ssin(side) * kAvoidPass + cM_ssin(toThem) * kAvoidAhead;
            trace->mAvoidPoint.y = them.y;
            trace->mAvoidPoint.z = them.z + cM_scos(side) * kAvoidPass + cM_scos(toThem) * kAvoidAhead;
            trace->mObstacle = s.best;
        } else if (cXyz corner; fopAc_ac_c* tent = psr_tent::detour(passer->current.pos, s.target, &corner)) {
            // a tent across the way: round its nearest corner (its canvas counts as ground, not a
            // wall, so walking into one just climbs it and gets pushed back off, over and over)
            trace->mAvoidPoint = corner;
            trace->mObstacle = tent;
            state->avoidId = fpcM_ERROR_PROCESS_ID_e;
            state->avoidSide = 0;
        } else {
            state->avoidId = fpcM_ERROR_PROCESS_ID_e;
            state->avoidSide = 0;
        }
    }
    if (retval != nullptr) *static_cast<fopAc_ac_c**>(retval) = trace->mObstacle;
    return HOOK_SKIP_ORIGINAL;
}

// A fighter puts its torch down to fight (it lies there burning) and, once the weapon is away again,
// walks back to it, crouches and picks it up. Far away (or out of sight) it just has it back.
void stepToward(daNpcPasser_c* passer, const cXyz& to, f32 speed);   // (camp guard, further down)
void callOut(daNpcPasser_c* passer, PasserState& state, int line, int wait);   // (further down)

// The old women's freeze spell: turn to the monster that hit her twice, raise the staff and thrust
// (psr_cast_<body>); at the thrust it is frozen in Link's ice (freeze.cpp). Then she runs; a minute
// before she can do it again.
constexpr int kCastCooldown = 1800;
// the spell's effects (psr_fx.jpc in PsrPack: the chest puff's glow and sparkles, recoloured icy blue,
// and the ice shards Link's freeze breaks into)
constexpr u16 kSpellGlow = 0x0110, kSpellSparks = 0x0111, kSpellBurst = 0x0112, kSpellShards = 0x8818;
constexpr int kQuietToForget = 300;   // ten seconds with no monster near: the hits are forgotten
constexpr f32 kCastReach = 1500.0f;
void castTick(daNpcPasser_c* passer, PasserState& state) {
    if (state.castCooldown > 0) --state.castCooldown;
    if (state.hitsThisFight > 0 && nearestEnemy(passer, kThreatRadius) == nullptr &&
        ++state.quietFrames > kQuietToForget) {
        state.hitsThisFight = 0;
    }
    if (state.casting == 0) return;
    fopAc_ac_c* foe = fopAcM_SearchByID(state.castTarget);
    auto finish = [&]() {
        state.casting = 0;
        state.anim = 0xFF;   // (at a camp: back to cowering by the fire, the camp guard's pose)
        if (!travellers::campInfo(passer, nullptr, nullptr, nullptr) && nearestEnemy(passer, kThreatRadius) != nullptr) {
            passer->setAction(daNpcPasser_c::MODE_1);   // out on the road: and off she goes
        }
    };
    if ((foe == nullptr || passer->current.pos.absXZ(foe->current.pos) > kCastReach) && !state.castFired) {
        finish();
        return;
    }
    passer->speedF = 0.0f;
    if (foe != nullptr && !state.castFired) {
        const s16 yaw = cLib_targetAngleY(&passer->current.pos, &foe->current.pos);
        cLib_addCalcAngleS2(&passer->current.angle.y, yaw, 2, 0x1800);
        passer->shape_angle.y = passer->current.angle.y;
    }
    if (state.casting == 1) {
        if (fighter_anim::hitting(passer)) return;   // the stagger first
        if (foe != nullptr &&
            std::abs(static_cast<s16>(passer->shape_angle.y - cLib_targetAngleY(&passer->current.pos, &foe->current.pos))) < 0x800) {
            if (!fighter_anim::startCast(passer)) {
                finish();
                return;
            }
            state.casting = 2;
            state.voiceWait = 0;
            callOut(passer, state, 3, 60);
        }
        return;
    }
    // the spell: an icy glow gathers at the top of her staff as she raises it...
    const f32 castAt = fighter_anim::castFrame(passer);
    Mtx lantern;
    const bool haveTip = traveller_gear::lanternMtx(passer, lantern);
    const cXyz tip = haveTip ? cXyz(lantern[0][3], lantern[1][3] + 23.0f, lantern[2][3]) : passer->eyePos;
    if (!state.castFired && castAt >= 0.0f && ++state.spellTick % 3 == 0) {
        const f32 k = 0.5f + std::fmin(1.0f, castAt / fighter_anim::kCastFire) * 0.9f;   // brighter as it builds
        psr_freeze::effect(kSpellGlow, tip, passer->shape_angle.y, cXyz(k, k, k));
    }
    // ...and at the thrust it bursts: ice shards, glow and sparkles, and the monster freezes in Link's ice
    if (!state.castFired && castAt >= fighter_anim::kCastFire) {
        state.castFired = true;
        state.castCooldown = kCastCooldown;
        state.hitsThisFight = 0;
        const cXyz big(1.6f, 1.6f, 1.6f), one(1.0f, 1.0f, 1.0f);
        psr_freeze::effect(kSpellBurst, tip, passer->shape_angle.y, big);
        psr_freeze::effect(kSpellSparks, tip, passer->shape_angle.y, big);
        psr_freeze::effect(kSpellShards, tip, passer->shape_angle.y, one);
        if (foe != nullptr && psr_freeze::freeze(foe)) {
            travellers::noteAct(passer, "froze a monster");
            psr_freeze::effect(kSpellSparks, foe->eyePos, foe->shape_angle.y, big);
            psr_freeze::effect(kSpellShards, foe->eyePos, foe->shape_angle.y, one);
        }
    }
    if (fighter_anim::castDone(passer)) finish();
}
constexpr f32 kFetchMax = 1200.0f;
constexpr int kFetchGiveUp = 300;   // ten seconds of walking without getting there
void torchTick(daNpcPasser_c* passer, PasserState& state) {
    if (!travellers::isFighter(passer)) return;
    const bool fighting = state.onGuard || state.standing || state.defending || fighter_anim::weaponOut(passer);
    if (fighting && traveller_gear::torchInHand(passer)) {
        traveller_gear::dropTorch(passer);
        travellers::noteAct(passer, "put its torch down to fight");
    }
    cXyz standAt;
    s16 yaw = 0;
    const bool onGround = traveller_gear::torchOnGround(passer, &standAt, &yaw);
    auto stopFetch = [&](bool takeIt) {
        if (takeIt) traveller_gear::takeTorch(passer);
        if (state.fetching != 0) {
            state.fetching = 0;
            if (!state.arrived && passer->mActionIdx == daNpcPasser_c::MODE_0) resumeRoute(passer, state);
        }
    };
    if (!onGround) {
        if (state.fetching != 0) stopFetch(false);
        return;
    }
    if (fighting || state.talking || passer->mActionIdx != daNpcPasser_c::MODE_0 || fighter_anim::hitting(passer)) {
        // a new fight, or running away: the torch stays where it is for now
        if (state.fetching == 2 && fighter_anim::pickupFrame(passer) >= fighter_anim::kPickupGrab) stopFetch(true);
        else if (state.fetching != 0) state.fetching = 0;
        return;
    }
    const f32 far = passer->current.pos.absXZ(standAt);
    if (state.fetching == 0) {
        if (far > kFetchMax || fopAcM_CheckCondition(passer, fopAcCnd_NODRAW_e) ||
            travellers::campInfo(passer, nullptr, nullptr, nullptr)) {
            stopFetch(true);   // too far to bother, or nobody's watching: it just has it again
            return;
        }
        state.fetching = 1;
        state.fetchFrames = 0;
        travellers::noteAct(passer, "going back for its torch");
        if (auto* walk = static_cast<J3DAnmTransformKey*>(passer->getAnmP(2, passer->getObjNum()))) {
            passer->setAnm(walk, 1.0f, 8.0f, J3DFrameCtrl::EMode_LOOP, 0, -1);
        }
    }
    if (state.fetching == 1) {
        if (++state.fetchFrames > kFetchGiveUp) {
            stopFetch(true);
            return;
        }
        if (far > 8.0f) {
            stepToward(passer, standAt, std::min(far, Cd2_HIO_walkMaxSpeed(passer->m_type)));
            return;
        }
        cLib_addCalcAngleS2(&passer->current.angle.y, yaw, 3, 0x800);
        passer->shape_angle.y = passer->current.angle.y;
        if (std::abs(static_cast<s16>(passer->shape_angle.y - yaw)) < 0x400 && fighter_anim::startPickup(passer)) {
            state.fetching = 2;
        } else if (std::abs(static_cast<s16>(passer->shape_angle.y - yaw)) < 0x400) {
            stopFetch(true);   // (no pick-up animation for this body)
        }
        return;
    }
    // state.fetching == 2: crouched for it; in hand from the grab frame on, then walk on
    passer->speedF = 0.0f;
    if (fighter_anim::pickupFrame(passer) >= fighter_anim::kPickupGrab) traveller_gear::takeTorch(passer);
    if (fighter_anim::pickupDone(passer)) {
        traveller_gear::takeTorch(passer);
        stopFetch(false);
    }
}

// Backup for anything the above doesn't catch: a traveller on the move (running away or walking its
// road) that hasn't got anywhere in three seconds gives up on the road point it was making for and
// heads for the next; if that doesn't help either, a runner picks a new way to run from here and a
// walker goes back to its road. Each time goes in the diary with what was around it.
void progressCheck(daNpcPasser_c* passer, PasserState& state) {
    const bool fleeing = passer->mActionIdx == daNpcPasser_c::MODE_1 && passer->field_0xb1c == 1;
    const bool walking = passer->mActionIdx == daNpcPasser_c::MODE_0 && passer->field_0xb1c == 2 &&
                         passer->mPathMode != 1;
    const bool held = state.standing || state.defending || state.talking || state.arrived || state.cowering ||
                      state.fetching != 0 ||
                      state.sidestep > 0 || fighter_anim::hitting(passer) ||
                      travellers::campInfo(passer, nullptr, nullptr, nullptr);
    if ((!fleeing && !walking) || held || !passer->m_path.isPath()) {
        state.progFrames = 0;
        state.stuckRuns = 0;
        return;
    }
    if (state.progFrames++ == 0) {
        state.progFrom = passer->current.pos;
        return;
    }
    if (state.progFrames < kProgressFrames) return;
    state.progFrames = 0;
    const f32 moved = passer->current.pos.absXZ(state.progFrom);
    if (moved >= kProgressMin) {
        state.stuckRuns = 0;
        return;
    }
    ++state.stuckRuns;
    if (state.stuckRuns == 2) say(passer, state, psr_bubble::kConfused);   // can't get past: puzzled
    const int pt = passer->m_path.field_0x20;
    const int num = passer->m_path.mPath->m_num;
    const cXyz next = passer->m_path.getPoint(static_cast<u16>(pt));
    fopAc_ac_c* player = dComIfGp_getPlayer(0);
    fopAc_ac_c* avoiding = state.avoidId != fpcM_ERROR_PROCESS_ID_e ? fopAcM_SearchByID(state.avoidId) : nullptr;
    char line[220];
    std::snprintf(line, sizeof(line),
                  "%s, moved %.0f in 3 s at (%.0f %.0f %.0f) pt %d/%d %.0f away, wall %d, link %.0f, round %s; %s",
                  fleeing ? "fleeing" : "walking", moved, passer->current.pos.x, passer->current.pos.y,
                  passer->current.pos.z, pt, num, passer->current.pos.absXZ(next),
                  passer->mAcchCir.ChkWallHit() ? 1 : 0,
                  player != nullptr ? passer->current.pos.absXZ(player->current.pos) : -1.0f,
                  avoiding != nullptr ? fopAcM_getProcNameString(avoiding) : "nobody",
                  state.stuckRuns == 1 ? "skips to the next point" : (fleeing ? "picks a new way to run" : "back to its road"));
    travellers::noteStuck(passer, line);
    if (state.stuckRuns == 1) {
        passer->m_path.setNextPoint(passer->current.pos);
        return;
    }
    state.stuckRuns = 0;
    if (fleeing) {
        cXyz threat = passer->current.pos;
        if (auto* enemy = nearestEnemy(passer, kChasedRadius)) threat = enemy->current.pos;
        else if (player != nullptr) threat = player->current.pos;
        travellers::startFlee(passer, threat);
    } else {
        resumeRoute(passer, state);
    }
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
    voice_banks::tick();
    if (state->dead) {
        psr_bubble::remove(state->bubble);
        state->bubble = fpcM_ERROR_PROCESS_ID_e;
    } else {
        needTick(passer, *state);
        bubbleTick(passer, *state);
    }
    if (!state->dead) {
        setDamageable(passer);
        keepGrounded(passer, *state);
        // a scream the moment it takes fright (runs or cowers): the body's own "fear" line
        {
            const bool scared = state->cowering ||
                                (passer->mActionIdx == daNpcPasser_c::MODE_1 && passer->field_0xb1c == 1);
            if (scared && !state->wasScared) {
                callOut(passer, *state, kVoiceFear, 45);
                say(passer, *state, state->cowering ? psr_bubble::kScared : psr_bubble::kFlee);
            }
            state->wasScared = scared;
        }
        // Off screen, with the ground under it not loaded (or a gap in it), a traveller fell
        // forever and was lost. Far below where it last stood: put it back there.
        if (passer->mAcch.ChkGroundHit()) {
            state->lastGround = passer->current.pos;
            state->hasGround = true;
        } else if (state->hasGround && passer->current.pos.y < state->lastGround.y - kFellThrough) {
            passer->current.pos = state->lastGround;
            passer->old.pos = state->lastGround;
            passer->speed.y = 0.0f;
            travellers::noteAct(passer, "fell through the ground; put back where it last stood");
        }
        if (psr_tent::pushOff(passer, passer->mAcch)) {
            travellers::noteAct(passer, "stepped off a tent it had climbed");
        }
        fighter_anim::forcedTick(passer);   // a hit reaction beats the fight moves
        fighterAnim(passer, *state);
        blowTick(passer, *state);
        if (state->voiceWait > 0) --state->voiceWait;
        progressCheck(passer, *state);
        torchTick(passer, *state);
        psr_freeze::loadEffects();
        castTick(passer, *state);
        // at night, out on the roads (not settled at a camp fire): a torch or lantern
        const int hour = dKy_getdaytime_hour();
        const bool night = hour >= 19 || hour < 6;
        const bool hold = traveller_gear::lightTick(passer, night, state->bedStep != 0 ||
                                                    travellers::campInfo(passer, nullptr, nullptr, nullptr));
        fighter_anim::layerTick(passer, hold);
        return;
    }
    if (passer->mpMorf == nullptr) {
        return;
    }

    traveller_gear::lightTick(passer, false, true);   // dead: the light goes out
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

// A music note off a musician's head every so often (note_actor).
void noteTick(daNpcPasser_c* passer, PasserState& state, int every) {
    if (--state.noteWait > 0) return;
    state.noteWait = every + static_cast<int>(cM_rndF(static_cast<f32>(every)));
    cXyz head = passer->eyePos;
    head.y += 25.0f;
    head.x += cM_rndFX(12.0f);
    head.z += cM_rndFX(12.0f);
    psr_note::spawn(head, fopAcM_GetRoomNo(passer));
}

// --- music at the fire -----------------------------------------------------------------------
// At night the bard (MAN_b) plays the game's own guitar sitting on his stool (m_playm, the Castle
// Town band's seated strum) with the band's guitar part and its vocal: he sings as he plays. While
// he does, now and then someone at his fire who doesn't fight gets up by their stool and sings
// along for a while (the townsfolk's own sing), then sits back down.
constexpr int kSongMin = 600, kSongMax = 1200;   // a sing-along lasts 20-40 seconds
fpc_ProcID g_bardPlaying = fpcM_ERROR_PROCESS_ID_e;
int g_bardCamp = -1;
bool musicTime() { const int h = dKy_getdaytime_hour(); return h >= 19 || h < 5; }

void stopMusic(daNpcPasser_c* passer, PasserState& state) {
    if (state.playing) {
        state.playing = false;
        traveller_gear::setPlaying(passer, false);
        if (g_bardPlaying == fopAcM_GetID(passer)) g_bardPlaying = fpcM_ERROR_PROCESS_ID_e;
        state.anim = 0xFF;
        travellers::refreshIdle(passer);
    }
    if (state.songFrames > 0) {
        state.songFrames = 0;
        state.anim = 0xFF;
        travellers::refreshIdle(passer);   // back onto its stool
    }
}

// Sitting at a camp with nothing else to do, a camper talks now and then: the townsfolk's own
// seated talk (15 sittalk_a, 16 sittalk_a_b) for a few seconds, then back to sitting quietly (14).
constexpr u8 kAnmSitWait = 14, kAnmSitTalkA = 15, kAnmSitTalkB = 16;
constexpr int kChatMin = 120, kChatMax = 300;   // 4-10 seconds of talk
constexpr int kMealLoop = 150;                  // one go of the eat/drink loop: lap, mouth, lap
constexpr int kVoiceLaugh = 4;     // the body's own laugh line
constexpr int kMealLaugh = 7;      // the laugh loop rides the meal layer (no item in hand)
constexpr int kLaughFrames = 90;   // three seconds of it
// A joke at the fire: everyone else sitting round it laughs a moment later (or doesn't get it).
void tellJoke(daNpcPasser_c* teller, PasserState& tellerState) {
    cXyz fire;
    if (!travellers::campInfo(teller, &fire, nullptr, nullptr)) return;
    travellers::noteCamp(teller, "tells a joke", nullptr);
    for (PasserState& other : g_states) {
        if (&other == &tellerState || other.dead || other.actor == nullptr) continue;
        if (fopAcM_SearchByID(other.id) != other.actor) continue;
        cXyz theirs;
        if (!travellers::campInfo(other.actor, &theirs, nullptr, nullptr) || theirs.absXZ(fire) > 50.0f) continue;
        other.laughIn = 50 + static_cast<int>(cM_rndF(35.0f));   // the punchline lands, then each in turn
    }
}
void chatTick(daNpcPasser_c* passer, PasserState& state, bool busy) {
    const int slot = travellers::slotIndex(passer);
    const bool seated = traveller_gear::sitting(slot) && travellers::campInfo(passer, nullptr, nullptr, nullptr);
    const bool free = seated && !busy && !state.playing && state.songFrames == 0 && !state.campCower &&
                      !state.defending && !state.toSeat && state.casting == 0;
    if (!free) {
        state.chatFrames = 0;
        state.pending = 0;
        state.laughIn = 0;
        if (state.laughFrames > 0) {
            state.laughFrames = 0;
            fighter_anim::setMeal(passer, 0);
        }
        if (state.mealFrames > 0) {
            state.mealFrames = 0;
            fighter_anim::setMeal(passer, 0);
        }
        return;
    }
    // laughing at a joke: head thrown back, slapping its knee (psr_laugh_<body>, on the meal layer)
    if (state.laughFrames > 0) {
        if (--state.laughFrames == 0) fighter_anim::setMeal(passer, 0);
        return;
    }
    if (state.laughIn > 0 && --state.laughIn == 0 && state.mealFrames == 0 && state.chatFrames == 0) {
        if (cM_rndF(1.0f) < 0.8f) {
            say(passer, state, psr_bubble::kLaugh);
            state.voiceWait = 0;
            callOut(passer, state, kVoiceLaugh, 30);
            traveller_gear::clearMeal(passer);
            fighter_anim::setMeal(passer, kMealLaugh);
            state.laughFrames = kLaughFrames;
            state.social = std::fmin(1.0f, state.social + 0.3f);
        } else {
            say(passer, state, psr_bubble::kConfused);   // didn't get it
        }
        return;
    }
    // a bite to eat or something to drink, a few times round; then the need it was for is filled
    if (state.mealFrames > 0) {
        if (--state.mealFrames == 0) {
            fighter_anim::setMeal(passer, 0);
            if (state.mealIsFood) state.hunger = std::fmin(1.0f, state.hunger + 0.75f);
            else state.thirst = std::fmin(1.0f, state.thirst + 0.75f);
            if (cM_rndF(1.0f) < 0.35f) say(passer, state, psr_bubble::kHappy);
        }
        return;
    }
    if (state.chatFrames > 0) {
        if (--state.chatFrames == 0) {
            state.social = 1.0f;
            if (auto* wait = static_cast<J3DAnmTransformKey*>(passer->getAnmP(kAnmSitWait, 0))) {
                passer->setAnm(wait, 1.0f, 12.0f, J3DFrameCtrl::EMode_LOOP, 0, -1);
            }
            if (cM_rndF(1.0f) < 0.2f) say(passer, state, psr_bubble::kLove);
        }
        return;
    }
    // thought of something (the bubble is up): now do it
    if (state.pending != 0) {
        if (--state.pendingWait > 0) return;
        const int what = state.pending;
        state.pending = 0;
        if (what == 3 || what == 4) {
            const u8 which = cM_rndF(1.0f) < 0.5f ? kAnmSitTalkA : kAnmSitTalkB;
            if (auto* talk = static_cast<J3DAnmTransformKey*>(passer->getAnmP(which, 0))) {
                passer->setAnm(talk, 1.0f, 12.0f, J3DFrameCtrl::EMode_LOOP, 0, -1);
                state.chatFrames = kChatMin + static_cast<int>(cM_rndF(static_cast<f32>(kChatMax - kChatMin)));
            }
            if (what == 4) tellJoke(passer, state);
            return;
        }
        // food (by one end, or in the palm) or drink (a bottle, the goblet or the tankard); the dwarves
        // only ever drink from the tankard, the old women never do
        const bool dwarf = passer->m_type == 3 || passer->m_type == 11;
        int kind;
        if (what == 1) kind = cM_rndF(1.0f) < 0.6f ? 1 : 4;
        else if (dwarf) kind = 5;
        else { const f32 r = cM_rndF(1.0f); kind = r < 0.4f ? 2 : (r < 0.65f ? 3 : 5); }
        kind = traveller_gear::pickMeal(passer, kind);   // the item decides the loop (old women: no tankard)
        if (kind == 0) return;
        state.mealIsFood = kind == 1 || kind == 4 || kind == 6;
        fighter_anim::setMeal(passer, kind);
        state.mealFrames = kMealLoop * (2 + static_cast<int>(cM_rndF(2.0f)));   // two or three goes
        return;
    }
    // about once every two seconds, look at what it needs most; the lower it is, the likelier it acts
    if (cM_rndF(1.0f) >= 1.0f / 60.0f) return;
    int what = 1;
    f32 low = state.hunger;
    if (state.thirst < low) { low = state.thirst; what = 2; }
    if (state.social < low) { low = state.social; what = 3; }
    if (low >= 0.45f || cM_rndF(1.0f) >= (0.45f - low) * 0.5f) return;
    state.pending = what;
    if (what == 3) {   // company: straight into a chat, or now and then a joke for the whole fire
        state.pendingWait = 1;
        if (cM_rndF(1.0f) < 0.3f) {
            state.pending = 4;
            say(passer, state, psr_bubble::kJoke);
        } else {
            say(passer, state, psr_bubble::kChat);
        }
    } else {           // food or drink: the thought first, then it reaches for something
        state.pendingWait = 30;
        say(passer, state, what == 1 ? psr_bubble::kHungry : psr_bubble::kThirsty);
        travellers::noteCamp(passer, what == 1 ? "is hungry" : "is thirsty", nullptr);
    }
}

void musicTick(daNpcPasser_c* passer, PasserState& state, bool busy) {
    cXyz center;
    const bool atFire = travellers::campInfo(passer, &center, nullptr, nullptr) && !busy && musicTime() &&
                        !state.campCower && !state.defending && !state.toSeat && state.casting == 0 &&
                        !fighter_anim::hitting(passer);
    const int slot = travellers::slotIndex(passer);
    if (!atFire) {
        stopMusic(passer, state);
        return;
    }
    if (traveller_gear::isBard(passer)) {
        if (!traveller_gear::sitting(slot) && !state.playing) return;   // not on his stool yet
        auto* strum = static_cast<J3DAnmTransformKey*>(dComIfG_getObjectRes("Mspecial", "m_playm.bck"));
        if (strum == nullptr) return;
        if (!state.playing) {
            state.playing = true;
            say(passer, state, psr_bubble::kMusic);
            traveller_gear::setPlaying(passer, true);
            travellers::noteCamp(passer, "picks up his guitar", nullptr);
        }
        if (passer->mpMorf->getAnm() != strum) passer->setAnm(strum, 1.0f, 12.0f, J3DFrameCtrl::EMode_LOOP, 0, -1);
        g_bardPlaying = fopAcM_GetID(passer);
        g_bardCamp = static_cast<int>(center.x * 0.01f) ^ static_cast<int>(center.z * 0.01f);
        camp_music::bardPlays(passer);   // the campfire songs (camp_music)
        noteTick(passer, state, 14);   // a note about every half second
        return;
    }
    // everyone else who doesn't fight: sing along now and then while the bard at this fire plays
    if (travellers::isFighter(passer)) return;
    fopAc_ac_c* bard = g_bardPlaying != fpcM_ERROR_PROCESS_ID_e ? fopAcM_SearchByID(g_bardPlaying) : nullptr;
    const bool sameFire = bard != nullptr && bard->current.pos.absXZ(center) < 600.0f;
    if (state.songFrames > 0) {
        if (!sameFire || --state.songFrames == 0) {
            state.songFrames = 1;
            stopMusic(passer, state);
            return;
        }
        if (auto* sing = static_cast<J3DAnmTransformKey*>(passer->getAnmP(53, 0))) {
            if (passer->mpMorf->getAnm() != sing) passer->setAnm(sing, 1.0f, 12.0f, J3DFrameCtrl::EMode_LOOP, 0, -1);
        }
        camp_music::singsAlong(passer);
        noteTick(passer, state, 24);   // fewer than the bard's
        return;
    }
    if (sameFire && traveller_gear::sitting(slot) && cM_rndF(1.0f) < 1.0f / 900.0f) {   // about every 30 s
        traveller_gear::sit(passer, false);   // up off the stool, right there
        state.songFrames = kSongMin + static_cast<int>(cM_rndF(static_cast<f32>(kSongMax - kSongMin)));
        travellers::noteCamp(passer, "sings along", nullptr);
    }
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
constexpr int kStrikeEvery = 40;            // frames between swings (each swing is two blows)
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
    if (state.anim == anm && !traveller_gear::sitting(travellers::slotIndex(passer))) return;
    state.anim = anm;
    traveller_gear::sit(passer, false);   // up off its log
    if (auto* a = static_cast<J3DAnmTransformKey*>(passer->getAnmP(anm, passer->getObjNum()))) {
        passer->setAnm(a, 1.0f, morf, J3DFrameCtrl::EMode_LOOP, 0, -1);
    }
}

void stepToward(daNpcPasser_c* passer, const cXyz& to, f32 speed) {
    cXyz aim = to;
    psr_tent::detour(passer->current.pos, to, &aim);   // round a tent in the way, not up its canvas
    const s16 yaw = cLib_targetAngleY(&passer->current.pos, &aim);
    cLib_addCalcAngleS2(&passer->current.angle.y, yaw, 4, 0x1000);
    passer->shape_angle.y = passer->current.angle.y;
    // Closer than its turning circle (at most 0x1000 a frame, so a circle of speed / 0.39 across: about
    // 2.6 steps), heading-first stepping can orbit the spot without ever reaching it (the bedtime
    // circling). There it steps straight at the spot, never past it, and only the body turns.
    const f32 dx = aim.x - passer->current.pos.x, dz = aim.z - passer->current.pos.z;
    const f32 d = std::sqrt(dx * dx + dz * dz);
    if (d < speed * 3.0f) {
        const f32 step = std::min(speed, d);
        if (d > 0.001f) {
            passer->current.pos.x += dx / d * step;
            passer->current.pos.z += dz / d * step;
        }
    } else {
        passer->current.pos.x += cM_ssin(passer->current.angle.y) * speed;
        passer->current.pos.z += cM_scos(passer->current.angle.y) * speed;
    }
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
    const bool heldByMod = state.standing || state.arrived || state.defending || state.fetching || state.casting ||
                           state.toSeat;
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

// Their own voice (each townsfolk body has its own): a shout as they swing, a yelp when hit.
void callOut(daNpcPasser_c* passer, PasserState& state, int line, int wait) {
    if (state.voiceWait > 0) return;
    travellers::diagLine("voice   slot %d type %d line %d", travellers::slotIndex(passer), passer->m_type, line);
    voice_banks::play(passer, line);
    state.voiceWait = wait;
}

// A fighter in reach takes a swing: with its own animations the blows land on the swing's frames
// (fighterAnim); without them (#29 for now) the blow lands at once, as before. False while it's
// still busy (startled, drawing, mid-swing): try again next frame.
bool swingOrStrike(daNpcPasser_c* passer, PasserState& state) {
    if (!fighter_anim::has(passer)) {
        strike(passer, state);
        callOut(passer, state, kVoiceShout, 60);
        return true;
    }
    if (fighter_anim::busy(passer)) return false;
    state.swingReq = true;
    callOut(passer, state, kVoiceShout, 60);
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
        want = state.chasing ? Want::Chase : Want::Ready;
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

// A camper away from its seat (a stagger, running off, a fight) walks back to it on its feet and only
// then sits: it used to sit down where it stood and slide over to the fire, stool and all.
constexpr f32 kAtSeat = 40.0f;
bool walkBackToSeat(daNpcPasser_c* passer, PasserState& state, const cXyz& seat, s16 seatYaw) {
    if (passer->mActionIdx == daNpcPasser_c::MODE_1 || fighter_anim::hitting(passer) || state.talking) return false;
    const f32 dx = seat.x - passer->current.pos.x;
    const f32 dz = seat.z - passer->current.pos.z;
    if (dx * dx + dz * dz > kAtSeat * kAtSeat) {
        if (!state.toSeat) travellers::noteCamp(passer, "walks back to its seat", nullptr);
        state.toSeat = true;
        passer->speedF = 0.0f;
        guardAnim(passer, state, kAnmWalk);
        stepToward(passer, seat, Cd2_HIO_walkMaxSpeed(passer->m_type));
        return true;
    }
    if (state.toSeat) {
        state.toSeat = false;
        passer->current.pos.x = seat.x;
        passer->current.pos.z = seat.z;
        passer->current.angle.y = passer->shape_angle.y = seatYaw;
        state.anim = 0xFF;
        travellers::refreshIdle(passer);   // and sit down
    }
    return false;
}

// --- bedtime in the small tents ---------------------------------------------------------------------
// Off to bed: walk to the grass in front of its tent's door, turn to the door, crouch (the torch
// pickup's crouch, held) and shuffle in through the flaps; once inside it goes (the roster keeps it
// asleep). Morning: it appears just inside, crouched, shuffles out and stands up.
constexpr f32 kCrawlSpeed = 0.8f;   // under the legs' walking threshold, so they stay crouched
constexpr f32 kCrouchFrame = 12.0f; // the pickup's lowest point
bool bedTick(daNpcPasser_c* passer, PasserState& state) {
    cXyz inside, doorstep;
    s16 faceIn = 0;
    const int phase = travellers::bedPhase(passer, &inside, &doorstep, &faceIn);
    if (phase == 0) {
        if (state.bedStep != 0) { state.bedStep = 0; state.anim = 0xFF; passer->mpMorf->setPlaySpeed(1.0f); }
        return false;
    }
    if (state.casting != 0 || fighter_anim::hitting(passer)) return false;
    auto crawl = [&](const cXyz& to) {   // straight there, no collision (the doorway is the tent's opening)
        const f32 dx = to.x - passer->current.pos.x, dz = to.z - passer->current.pos.z;
        const f32 d = std::sqrt(dx * dx + dz * dz);
        if (d <= kCrawlSpeed) { passer->current.pos.x = to.x; passer->current.pos.z = to.z; return true; }
        passer->current.pos.x += dx / d * kCrawlSpeed;
        passer->current.pos.z += dz / d * kCrawlSpeed;
        passer->speedF = 0.0f;
        return false;
    };
    if (phase == 1) {
        switch (state.bedStep) {
        case 0: {
            if (state.bedFrames == 0) {
                traveller_gear::sit(passer, false);
                traveller_gear::clearMeal(passer);
                state.playing = false;
                state.songFrames = 0;
                travellers::noteCamp(passer, "off to bed", nullptr);
            }
            ++state.bedFrames;
            const f32 dx = doorstep.x - passer->current.pos.x, dz = doorstep.z - passer->current.pos.z;
            if (dx * dx + dz * dz > 12.0f * 12.0f) {
                passer->speedF = 0.0f;
                guardAnim(passer, state, kAnmWalk);
                stepToward(passer, doorstep, Cd2_HIO_walkMaxSpeed(passer->m_type));
                return true;
            }
            passer->current.pos.x = doorstep.x;
            passer->current.pos.z = doorstep.z;
            passer->current.angle.y = passer->shape_angle.y = faceIn;
            passer->speedF = 0.0f;
            if (fighter_anim::startPickup(passer)) state.bedStep = 1;
            else { travellers::tuckedIn(passer); return true; }
            state.anim = 0xFF;
            return true;
        }
        case 1:
            if (fighter_anim::pickupFrame(passer) >= kCrouchFrame) { passer->mpMorf->setPlaySpeed(0.0f); state.bedStep = 2; }
            return true;
        case 2:
            if (crawl(inside)) { passer->mpMorf->setPlaySpeed(1.0f); state.bedStep = 0; state.bedFrames = 0; travellers::tuckedIn(passer); }
            return true;
        default:
            state.bedStep = 0;
            return true;
        }
    }
    // phase 3: up for the day
    switch (state.bedStep) {
    case 0:
    default:
        passer->current.pos.x = inside.x;
        passer->current.pos.z = inside.z;
        passer->current.angle.y = passer->shape_angle.y = static_cast<s16>(faceIn + 0x8000);
        if (!fighter_anim::startPickup(passer)) { travellers::upAndAbout(passer); return false; }
        passer->mpMorf->setFrame(kCrouchFrame);
        passer->mpMorf->setPlaySpeed(0.0f);
        state.anim = 0xFF;
        state.bedStep = 10;
        return true;
    case 10:
        if (crawl(doorstep)) { passer->mpMorf->setPlaySpeed(1.0f); state.bedStep = 11; }
        return true;
    case 11:
        if (!fighter_anim::pickupDone(passer)) return true;
        state.bedStep = 0;
        state.bedFrames = 0;
        state.anim = 0xFF;
        travellers::upAndAbout(passer);
        travellers::refreshIdle(passer);
        return false;
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
    if (state.casting != 0) return true;   // an old woman casting: that comes first
    const bool fighter = travellers::isFighter(passer);
    const f32 guardRadius = state.defending ? kCampLeashRadius : kCampGuardRadius;
    auto* enemy = nearestEnemyAt(center, guardRadius, fighter ? kReachAbove : 1.0e9f);
    // the wolf at the fire: the fighters go for him; everyone else cowers as from any monster
    enemy = nearerFoe(center, enemy, wolfLinkNear(center, guardRadius));
    if (fighter && wolfShy(passer) && enemy == dComIfGp_getPlayer(0)) enemy = nullptr;
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
                say(passer, state, psr_bubble::kDefend);   // a monster at the fire: up to defend it
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
        return walkBackToSeat(passer, state, seat, seatYaw);
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
    return walkBackToSeat(passer, state, seat, seatYaw);
}

HookAction onExecutePre(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    auto* state = findState(passer, false);
    if (!isPrototypePasser(passer) || state == nullptr) {
        return HOOK_CONTINUE;
    }

    (void)&logProgress;   // per-traveller logging off (too chatty with many travellers)

    // The game deletes a passer the moment it is off screen once its end time of day has passed:
    // look away from a camp at the wrong moment and everyone was gone. Only far off (well out of
    // the way, not at a camp) may it be tidied up like that.
    if (!state->dead) {
        const auto* link = dComIfGp_getPlayer(0);
        const bool far = link != nullptr && passer->current.pos.absXZ(link->current.pos) > kTidyDistance;
        const bool camping = travellers::campInfo(passer, nullptr, nullptr, nullptr);
        if (!far || camping) passer->m_endTime = 0xFFFF;
    }

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
            // Gone in a puff of smoke: the game's monster puff, but only its smoke, recoloured a soft
            // white (no black, no monster bits: it's a person, and the soul rises out of it), with
            // the puff's own "poof".
            {
                cXyz at(passer->current.pos.x, passer->current.pos.y + 40.0f, passer->current.pos.z);
                cXyz scale(1.3f, 1.3f, 1.3f);
                psr_freeze::effect(kDeathSmoke, at, passer->shape_angle.y, scale);
                mDoAud_seStart(JA_SE_CM_MONS_EXPLODE, &at, 0, dComIfGp_getReverb(fopAcM_GetRoomNo(passer)));
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
        campHandled = bedTick(passer, *state) || campGuard(passer, *state);
    }
    musicTick(passer, *state, campHandled || state->talking);
    chatTick(passer, *state, campHandled || state->talking);

    // Fighters stop, face the enemy and stand their ground while they're healthy.
    if (!campHandled && !state->talking && !state->cowering &&
        !travellers::campInfo(passer, nullptr, nullptr, nullptr)) {
        auto* enemy = holdsGround(passer) ? nearestEnemyAt(passer->current.pos, kThreatRadius, kReachAbove) : nullptr;
        if (holdsGround(passer) && !wolfShy(passer)) {
            enemy = nearerFoe(passer->current.pos, enemy, wolfLinkNear(passer->current.pos, kThreatRadius));
        }
        if (state->shooterFrames > 0) {
            // shot at: the archer is its enemy (it charges him) however far off, while he keeps it up
            --state->shooterFrames;
            fopAc_ac_c* archer = fopAcM_SearchByID(state->shooter);
            if (!holdsGround(passer) || archer == nullptr || fopAcM_GetGroup(archer) != fopAc_ENEMY_e ||
                passer->current.pos.absXZ(archer->current.pos) > kChasedRadius) {
                state->shooterFrames = 0;
            } else if (enemy == nullptr) {
                enemy = archer;
            }
        }
        state->vsWolf = enemy != nullptr && enemy == dComIfGp_getPlayer(0);
        if (enemy == nullptr && state->chasing) {
            state->chasing = false;
            state->anim = 0xFF;
        }
        if (enemy != nullptr) {
            state->foeLost = 0;
        } else if (state->standing && holdsGround(passer) && ++state->foeLost < kFoeLostFrames) {
            // lost sight of it for a moment (it circled out, went behind something): stay on guard
            state->onGuard = true;
            passer->speedF = 0.0f;
        }
        if (enemy != nullptr) {
            state->onGuard = true;
            // out of reach: charge at it (not at wolf Link: him it only squares up to)
            const bool charge = !state->vsWolf && !fighter_anim::busy(passer) &&
                                passer->current.pos.absXZ(enemy->current.pos) > kStrikeReach;
            if (charge && !state->chasing) {
                say(passer, *state, psr_bubble::kAttack);
                travellers::noteAct(passer, "charges");
            }
            if (!charge && state->chasing) state->anim = 0xFF;
            state->chasing = charge;
            if (charge) {
                guardAnim(passer, *state, kAnmRun);
                stepToward(passer, enemy->current.pos, Cd2_HIO_maxSpeed(passer->m_type) * 1.5f);
                state->turning = false;
            } else {
                passer->speedF = 0.0f;
                const s16 toEnemy = cLib_targetAngleY(&passer->current.pos, &enemy->current.pos);
                state->turning = std::abs(static_cast<s16>(toEnemy - passer->shape_angle.y)) > kTurnShuffle;
                if (!fighter_anim::busy(passer)) {
                    cLib_addCalcAngleS2(&passer->current.angle.y, toEnemy, 4, fighter_anim::has(passer) ? kGuardTurn : 0x800);
                }
                passer->shape_angle.y = passer->current.angle.y;
            }
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
        state->voiceWait = 0;
        callOut(passer, *state, kVoiceYelp, 30);                           // and is heard
        say(passer, *state, travellers::isFighter(passer) ? psr_bubble::kAngry : psr_bubble::kScared);
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
        if (life > 0 && isOldWoman(passer)) {
            // the old women: the second blow in a fight, and she turns and freezes whoever dealt it
            state->quietFrames = 0;
            // shot (an arrow): the one who shot it, if he's within her reach; out of reach she runs
            fopAc_ac_c* culprit = isProjectile(attacker) ? nearestEnemy(passer, kCastReach) : attacker;
            if (++state->hitsThisFight >= 2 && state->castCooldown == 0 && state->casting == 0 && culprit != nullptr) {
                state->casting = 1;
                state->castFired = false;
                state->castTarget = fopAcM_GetID(culprit);
                state->standing = false;
                state->arrived = false;
                state->cowering = false;
                passer->speedF = 0.0f;
                traveller_gear::sit(passer, false);
                travellers::noteAct(passer, "turning to freeze what hit her");
                return HOOK_CONTINUE;
            }
        }
        if (life <= 0) {
            beginDeath(passer, *state);
        } else if (holdsGround(passer)) {
            // a fighter takes it and stays put; shot by an arrow, it goes after the archer (the nearest
            // monster within kChasedRadius: the threat radius doesn't reach where archers shoot from)
            if (isProjectile(attacker)) {
                if (auto* archer = nearestEnemy(passer, kChasedRadius)) {
                    if (state->shooterFrames == 0) travellers::noteAct(passer, "shot at: going after the archer");
                    state->shooter = fopAcM_GetID(archer);
                    state->shooterFrames = kShooterFrames;
                }
            }
        } else if (!travellers::isFighter(passer) &&
                   travellers::campInfo(passer, nullptr, nullptr, nullptr)) {
            // a camper who can't fight keeps cowering by the fire (the camp guard)
        } else {
            // hit by a monster: run, however far off it is (an archer shoots from well outside the
            // threat radius; the run itself goes away from the nearest monster within kChasedRadius)
            state->standing = false;
            state->arrived = false;  // running away replaces waiting at the end of the road
            state->cowering = false;
            if (passer->mActionIdx != daNpcPasser_c::MODE_1) passer->setAction(daNpcPasser_c::MODE_1);
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
    // wolf Link is a monster to anyone who won't fight him (everyone but a fighter, and a fighter
    // he has bitten): they run from him like from any monster
    if (retval != nullptr && isPrototypePasser(passer) && (!travellers::isFighter(passer) || wolfShy(passer)) &&
        wolfLinkNear(passer->current.pos, kThreatRadius) != nullptr) {
        *static_cast<bool*>(retval) = true;
        return;
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
    if (mods::hook::add_pre<PathObstacle>(onCheckObstaclePre) != MOD_OK) {
        svc_log->info(mod_ctx, "path obstacle hook unavailable; travellers keep the game's own sidestepping");
    }
    if (!shop_stage::install()) {
        svc_log->info(mod_ctx, "stage service unavailable; Beedle's shop can't be entered");
    }
    if (!mod_sounds::install()) {
        svc_log->info(mod_ctx, "added-sound hooks unavailable; travellers stay quiet and the bard plays silently");
    }
    if (!camp_music::install()) {
        svc_log->info(mod_ctx, "campfire songs unavailable; the bard plays silently");
    }
    if (!shop_stage::installMusic()) {
        svc_log->info(mod_ctx, "Beedle's shop music unavailable; the shop is quiet");
    }
    if (!voice_banks::install()) {
        svc_log->info(mod_ctx, "voice clips could not be registered; travellers stay quiet");
    }
    if (!psr_freeze::install()) {
        svc_log->info(mod_ctx, "actor update hook unavailable; the old women can't freeze monsters");
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
    if (psr_note::registerActor() != MOD_OK) {
        svc_log->info(mod_ctx, "music note actor unavailable; no notes over the music");
    }
    if (psr_bubble::registerActor() != MOD_OK) {
        svc_log->info(mod_ctx, "bubble actor unavailable; no thought bubbles");
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
    camp_music::tick();
    shop_stage::tick();
    return MOD_OK;
}

MOD_EXPORT ModResult mod_shutdown(ModError*) {
    g_talkGraph.reset();
    g_greeting = {};
    g_talkNode = 0xFFFF;
    return MOD_OK;
}
}

