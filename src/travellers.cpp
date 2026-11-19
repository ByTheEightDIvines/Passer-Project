#include "global.h"

#include "travellers.hpp"
#include "travel_network_data.inc"
#include "tent_actor.hpp"
#include "camp_fire.hpp"
#include "spot_picker.hpp"
#include "traveller_gear.hpp"

#include "d/actor/d_a_npc_passer.h"
#include "d/d_bg_s_gnd_chk.h"
#include "d/d_bg_s_wtr_chk.h"
#include "camp_cook.hpp"
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "d/actor/d_a_obj_flag2.h"
#include "m_Do/m_Do_mtx.h"
#include "d/d_path.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_manager.h"
#include "f_pc/f_pc_name.h"
#include "SSystem/SComponent/c_math.h"
#include "mods/service.hpp"
#include "mods/svc/actor.h"
#include "mods/svc/hook.h"
#include "mods/svc/log.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <queue>
#include <vector>

namespace travellers {
namespace {

using namespace travel_data;

constexpr int kNodeCount = sizeof(kNodes) / sizeof(kNodes[0]);
constexpr int kEdgeCount = sizeof(kEdges) / sizeof(kEdges[0]);
constexpr int kCampCount = sizeof(kCamps) / sizeof(kCamps[0]);

// NK_GUARD_CAMP: the guards' own fire, joined to no road. Travellers never go there (every camp
// choice looks for NK_CAMP); the guards live there and never leave.
enum NodeKind : u8 { NK_ROAD = 0, NK_CAMP = 1, NK_BEEDLE = 2, NK_GATE = 3, NK_BRIDGE_END = 4, NK_BRIDGE_EAST = 5,
                     NK_GUARD_CAMP = 6 };
enum EdgeFlag : u8 { EF_ESCORT = 1, EF_NEEDS_BRIDGE = 2, EF_NO_BEEDLE = 4, EF_HANDOFF = 8, EF_SPUR = 16 };

// --- tuning -----------------------------------------------------------------------------------
constexpr int kPopulation = 30;              // travellers on the network
// The guards at their camp by the Bridge of Eldin, in the slots after the travellers: Castle Town's
// own guard bodies (Cd2 type 1 MAD_a, type 0 MAN_a), there once Castle Town's twilight is lifted.
constexpr std::array<u8, 6> kGuardCast{1, 0, 0, 1, 0, 1};
constexpr std::array<u8, 6> kGuardStyle{1, 1, 2, 2, 1, 2};   // three spearmen and three swordsmen
constexpr f32 kVirtualSpeed = 4.0f;          // units per frame while not spawned (about a walk)
// A traveller only ever appears from its room's spawner: everyone in a room when it loads, and
// after that whoever walks in from somewhere that isn't loaded. Those late arrivals wait (walking on
// "virtually") while their spot is on screen close by, so nobody pops in in front of Link.
constexpr f32 kViewGuardRadius = 2500.0f;
constexpr int kMaxPathPoints = 48;
constexpr f32 kFleeDistance = 2800.0f;
// Frames to wait for a created actor that the game is no longer creating but that never showed up.
// While the game is still creating it (fpcM_IsCreating), we keep waiting however long it takes.
constexpr int kCreateTimeout = 150;
constexpr int kMaxReal = 16;                 // safety cap on real travellers at once
constexpr int kCampSeats = 5;                // seats around each camp fire
constexpr int kMaxCampFighters = 2;         // at most this many fighters sit at one fire
constexpr int kBowman = 8;                   // passer 8 (bow and sword): only one out in a room at a time
constexpr int kGuardSeats = 8;               // the guards' fire (six guards, room to spare)
constexpr int kAmphiSeats = 6;               // the amphitheater by Castle Town is a bit bigger
constexpr int kMaxSeats = 10;
constexpr int kGoneCooldownMin = 1800;       // frames (30-90 s) before a traveller the game deleted
constexpr int kGoneCooldownMax = 5400;       // can reappear, different for each so they trickle back
constexpr int kCampBackMin = 450;             // frames (15-30 s) before a camper the game deleted
constexpr int kCampBackMax = 900;             // sits back down at its seat
constexpr f32 kCampBackRadius = 1200.0f;      // ...never closer to Link than this, and only behind him
constexpr f32 kGoneSkip = 800.0f;            // ...and how far down the road it has walked by then
constexpr f32 kNoArrivalRadius = 3000.0f;    // late arrivals never appear this close to Link
constexpr int kArrivalGap = 450;             // frames (7.5 s) between late arrivals in one room
// Hearts, counted in quarter hearts like Link's (an enemy hit that takes half a heart from Link
// takes half a heart from a traveller).
constexpr int kQuarters = 4;
constexpr int kFighterHearts = 5;
constexpr int kCommonHearts = 2;
constexpr int kHealFrames = 600;             // off screen, a quarter heart back every 10 s
constexpr int kHealFramesResting = 150;      // ...every 2.5 s while sitting at camp or in town
// Travellers who fight (every man but 4, 7 and 15, plus passer 29). They stand their ground and only
// run once they are down to their last heart.
constexpr int kCookPathId = 0x3F;   // the camp cook's body (camp_cook.cpp)
constexpr std::array<u8, 10> kFighters{3, 6, 8, 9, 11, 13, 14, 29, 0, 1};   // (0, 1: the guards)
// The passer types from the cast (stock passer type ids).
// Everyone on the road but the bards: each fire has a bard of its own (homeCamp), so no others.
// Beedle (the last traveller slot): the men's skeleton (type 0, MAN_a's frames) with his own look.
constexpr int kBeedleSlot = kPopulation - 1;
constexpr u8 kBeedleType = 0;
constexpr std::array<u8, 13> kWalkCast{3, 6, 7, 8, 9, 11, 13, 14, 15, 20, 27, 28, 29};

// --- network ----------------------------------------------------------------------------------
struct Adj {
    int edge;
    int other;
};
std::vector<std::vector<Adj>> g_adj;
std::vector<std::vector<f32>> g_cum;   // per edge: distance from its first point to each point

// --- the story: which parts of the field are open ------------------------------------------------
// Hyrule Field opens province by province (the game's own table, d_kankyo.cpp dKy_F_SP121Check):
// Faron Field (F_SP121 rooms 1, 6, 15) once Faron's twilight is lifted; Kakariko Gorge and Eldin
// Field (0, 2-5, 7) with Eldin's; Lanayru Field and the Great Bridge paths (9-14) and Castle Town's
// gates (F_SP122) with Lanayru's. The twilight walls stand on those borders. On top of that, two
// blockers on the roads (found in the field's stage files, every layer):
//  - the bomb rocks on the Kakariko Gorge - Eldin Field path (room 4, set BG object @bg004d, gone
//    once its switch 0x00 is on; the field's switches are save table 6),
//  - the rider gate at Faron Field's west exit (room 15, R_Gate: locked until the escort is done,
//    d_a_obj_rgate.cpp Create).
// Travellers only walk roads whose every room is open and whose blockers are gone.
enum EdgeBlock : u8 { EB_GORGE_ROCKS = 1, EB_RIDER_GATE = 2 };
struct Blocker { f32 x, z; u8 bit; };
// (The gorge rocks are gone for good now: passer_prototype.cpp never makes the road rock piles. Their
// blocker is kept, always open, should they ever come back.)
constexpr Blocker kBlockers[] = {
    {-17410.0f, 34955.0f, EB_GORGE_ROCKS},
    {-66221.0f, 69766.0f, EB_RIDER_GATE},
};
constexpr f32 kBlockerReach = 350.0f;   // a road passing this close goes through it
constexpr int kFieldSaveTable = 6;      // F_SP121's and F_SP122's save memory (their STAG)
constexpr u8 kGorgeRocksSwitch = 0x00;
std::vector<u32> g_edgeRooms;   // per edge: the rooms its points pass (roomBit)
std::vector<u8> g_edgeTentRoom; // per edge: passes F_SP122 room 8 (Beedle's tent: he never goes there)
bool g_planBeedle = false;      // the route being planned is Beedle's
struct PlanAs {                 // plans made inside it are for this traveller
    bool was;
    explicit PlanAs(bool beedle) : was(g_planBeedle) { g_planBeedle = beedle; }
    ~PlanAs() { g_planBeedle = was; }
};
std::vector<u8> g_edgeBlock;    // per edge: EdgeBlock bits
u32 g_openRooms = 0;
u8 g_openBlocks = 0;            // EdgeBlock bits that are open (gone)

u32 roomBit(int stage, int room) {
    if (stage == 1) return 1u << 16;
    return room >= 0 && room < 16 ? 1u << room : 0u;
}

f32 segDistXZ(const Point& a, const Point& b, f32 x, f32 z) {
    const f32 dx = b.x - a.x, dz = b.z - a.z;
    const f32 l2 = dx * dx + dz * dz;
    f32 t = l2 > 1e-6f ? ((x - a.x) * dx + (z - a.z) * dz) / l2 : 0.0f;
    t = std::clamp(t, 0.0f, 1.0f);
    const f32 px = a.x + dx * t - x, pz = a.z + dz * t - z;
    return std::sqrt(px * px + pz * pz);
}

f32 edgeLength(int e) { return g_cum[e].back(); }

void buildNetwork() {
    g_adj.assign(kNodeCount, {});
    g_cum.assign(kEdgeCount, {});
    for (int e = 0; e < kEdgeCount; ++e) {
        const Edge& ed = kEdges[e];
        g_adj[ed.a].push_back({e, ed.b});
        g_adj[ed.b].push_back({e, ed.a});
        auto& cum = g_cum[e];
        cum.resize(ed.count);
        f32 d = 0.0f;
        for (int i = 0; i < ed.count; ++i) {
            if (i > 0) {
                const Point& p = kPoints[ed.first + i - 1];
                const Point& q = kPoints[ed.first + i];
                // the jump across a loading screen (stage change) takes no walking
                if (p.stage == q.stage) {
                    d += std::sqrt((q.x - p.x) * (q.x - p.x) + (q.z - p.z) * (q.z - p.z));
                }
            }
            cum[i] = d;
        }
    }
    g_edgeRooms.assign(kEdgeCount, 0u);
    g_edgeBlock.assign(kEdgeCount, 0u);
    g_edgeTentRoom.assign(kEdgeCount, 0u);
    for (int e = 0; e < kEdgeCount; ++e) {
        const Edge& ed = kEdges[e];
        for (int i = 0; i < ed.count; ++i) {
            const Point& p = kPoints[ed.first + i];
            g_edgeRooms[e] |= roomBit(p.stage, p.room);
            if (p.stage == 1 && p.room == 8) g_edgeTentRoom[e] = 1;
            if (i + 1 < ed.count && p.stage == 0 && kPoints[ed.first + i + 1].stage == 0) {
                for (const Blocker& b : kBlockers) {
                    if (segDistXZ(p, kPoints[ed.first + i + 1], b.x, b.z) < kBlockerReach) g_edgeBlock[e] |= b.bit;
                }
            }
        }
    }
}

bool nodeOpen(int n) { return (roomBit(kNodes[n].stage, kNodes[n].room) & ~g_openRooms) == 0; }

bool g_bridgeRepaired = false;

bool edgeAllowed(int e, bool escort) {
    const u8 f = kEdges[e].flags;
    if ((f & EF_ESCORT) && !escort) {
        return false;
    }
    if ((f & EF_NEEDS_BRIDGE) && !g_bridgeRepaired) {
        return false;
    }
    if (!escort && ((g_edgeRooms[e] & ~g_openRooms) != 0 || (g_edgeBlock[e] & ~g_openBlocks) != 0)) {
        return false;   // through a part of the field the story hasn't opened yet
    }
    if (g_planBeedle && g_edgeTentRoom[e]) return false;   // Beedle keeps away from his tent's part of the field
    return true;
}

// Leaves (camps, gates, Beedle's tent) are only ever a start or an end, never walked through.
bool passThrough(int node) { return kNodes[node].kind == NK_ROAD || kNodes[node].kind >= NK_BRIDGE_END; }

struct Leg {
    u16 edge;
    bool fwd;
};

// Shortest route from a node to a node; empty if none.
std::vector<Leg> plan(int from, int to, bool escort) {
    std::vector<f32> dist(kNodeCount, 1e30f);
    std::vector<int> prevEdge(kNodeCount, -1);
    using QE = std::pair<f32, int>;
    std::priority_queue<QE, std::vector<QE>, std::greater<QE>> pq;
    dist[from] = 0.0f;
    pq.push({0.0f, from});
    while (!pq.empty()) {
        auto [d, n] = pq.top();
        pq.pop();
        if (d > dist[n] || n == to) {
            if (n == to) break;
            continue;
        }
        if (n != from && !passThrough(n)) {
            continue;
        }
        for (const Adj& a : g_adj[n]) {
            if (!edgeAllowed(a.edge, escort)) continue;
            const f32 nd = d + edgeLength(a.edge) + 1.0f;
            if (nd < dist[a.other]) {
                dist[a.other] = nd;
                prevEdge[a.other] = a.edge;
                pq.push({nd, a.other});
            }
        }
    }
    std::vector<Leg> route;
    if (prevEdge[to] < 0) return route;
    for (int n = to; n != from;) {
        const int e = prevEdge[n];
        const bool fwd = kEdges[e].b == n;
        route.push_back({static_cast<u16>(e), fwd});
        n = fwd ? kEdges[e].a : kEdges[e].b;
    }
    std::reverse(route.begin(), route.end());
    return route;
}

f32 routeLength(const std::vector<Leg>& r) {
    f32 s = 0.0f;
    for (const Leg& l : r) s += edgeLength(l.edge);
    return s;
}

int legEnd(const Leg& l) { return l.fwd ? kEdges[l.edge].b : kEdges[l.edge].a; }
int legStart(const Leg& l) { return l.fwd ? kEdges[l.edge].a : kEdges[l.edge].b; }

// i-th point of a leg in travel direction, and its distance along the leg
const Point& legPoint(const Leg& l, int i) {
    const Edge& e = kEdges[l.edge];
    return kPoints[e.first + (l.fwd ? i : e.count - 1 - i)];
}
f32 legPointDist(const Leg& l, int i) {
    const Edge& e = kEdges[l.edge];
    return l.fwd ? g_cum[l.edge][i] : edgeLength(l.edge) - g_cum[l.edge][e.count - 1 - i];
}

struct Where {
    u8 stage;
    s8 room;
    cXyz pos;
    s16 yaw;
    int nextPoint;   // index of the next point on the leg
};

Where locate(const Leg& l, f32 d) {
    const int n = kEdges[l.edge].count;
    int i = 1;
    while (i < n - 1 && legPointDist(l, i) < d) ++i;
    const Point& a = legPoint(l, i - 1);
    const Point& b = legPoint(l, i);
    const f32 da = legPointDist(l, i - 1), db = legPointDist(l, i);
    Where w{};
    if (a.stage != b.stage) {   // on a loading screen: you're already on the far side
        w.stage = b.stage; w.room = b.room; w.pos.set(b.x, b.y, b.z);
        w.yaw = 0; w.nextPoint = i;
        return w;
    }
    const f32 t = db > da ? std::clamp((d - da) / (db - da), 0.0f, 1.0f) : 0.0f;
    w.stage = a.stage;
    w.room = t < 0.5f ? a.room : b.room;
    w.pos.set(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t);
    w.yaw = cM_atan2s(b.x - a.x, b.z - a.z);
    w.nextPoint = i;
    return w;
}

// --- travellers -------------------------------------------------------------------------------
enum State : u8 { ST_WALK, ST_REST, ST_TOWN, ST_GONE };
enum PathEnd : u8 { PE_CONTINUE, PE_ARRIVE, PE_VANISH };

struct Traveller {
    bool used = false;
    bool beedle = false;   // Beedle: wanders the field day and night, never camps, never near his tent
    u8 type = 8;
    State state = ST_WALK;
    std::vector<Leg> route;
    int leg = 0;
    f32 dist = 0.0f;       // along route[leg], in travel direction
    int node = 0;          // where it is resting, or the last node it passed
    int timer = 0;
    bool restUntilMorning = false;
    // real actor
    bool spawned = false;
    bool registered = false;
    int createFrames = 0;
    fpc_ProcID actor = fpcM_ERROR_PROCESS_ID_e;
    std::vector<std::pair<int, f32>> pathMap;   // per walk-path point: (leg, dist)
    PathEnd pathEnd = PE_CONTINUE;
    std::vector<std::pair<int, f32>> fleeMap;   // flee path point -> (index into fleeLegs, distance)
    std::vector<Leg> fleeLegs;                  // the roads it is running along
    bool idleSet = false;
    bool dead = false;
    int campNode = -1;     // camp it has a seat at (reserved on the way there), or -1
    int seat = -1;         // which seat around that camp's fire
    bool leaving = false;  // walked into a gate / loading line / unloaded room; the game deletes it
    std::pair<int, f32> beyond{-1, 0.0f};   // first road point past the end of its walk path
    int cooldown = 0;      // frames until its room's spawner may bring it back
    const char* lastAct = "walk";   // what it was last doing (for the log)
    int life = -1;         // quarter hearts left (-1: full, set when it first needs it)
    int healTimer = 0;
    // bed for the night in one of its camp's small tents
    int bedCamp = -1;      // camp node of the tent it sleeps in, or -1
    int bed = -1;          // bed there (two to a tent: tent bed / 2)
    u8 sleep = 0;          // 0 up, 1 off to bed (walking to the tent, crawling in), 2 asleep, 3 getting up
    bool stayUp = false;   // tonight it stays up by the fire
    int sleepFrames = 0;
    int homeCamp = -1;     // a bard's own fire: he travels by day and is back to play there every night
    bool guard = false;    // one of the guards: lives at the guards' camp, never takes to the road
    u8 style = 0;          // a guard's arms: 1 spear and tall shield, 2 sword and small shield, 3 that and the royal bow
    // Convoy people (riders, escorts, the folk by the parked wagons): the convoys put them in and take
    // them out, and say where they stand. The roster leaves them alone: no roads, no camps, no spawner.
    bool crew = false;
    bool held = false;     // placed by hand (on the bench, walking beside the wagon); off: the passer's own AI
};

std::array<Traveller, kSlots> g_t{};
struct PathStore {
    dPath path;
    std::array<dPnt, kMaxPathPoints> points;
};
std::array<PathStore, kSlots> g_walk{};
std::array<PathStore, kSlots> g_flee{};
u32 g_rng = 0x2468ACE1u;
bool g_started = false;
int g_frame = 0;
// The current tent meshes are far too heavy for Dusklight's per-frame vertex buffer
// (6k-24k vertices in a single draw each). Off until they're rebuilt lighter.
constexpr bool kTentsEnabled = true;
constexpr int kSpawnGap = 105;          // tents only: frames between tent spawns
bool g_pathLookupOpen = false;

// ---- crash-proof diagnostic log ----
// Dusklight's own log loses its last chunk when the game crashes, so we keep our own: one line
// per event plus a status line every second, each flushed straight to disk. Lives in the mod's
// data folder as travellers_diag.log and is rewritten fresh each launch.
std::FILE* g_diag = nullptr;
void diag(const char* fmt, ...) {
    if (g_diag == nullptr) return;
    std::fprintf(g_diag, "%6d ", g_frame);
    va_list ap; va_start(ap, fmt); std::vfprintf(g_diag, fmt, ap); va_end(ap);
    std::fputc('\n', g_diag);
    std::fflush(g_diag);
}
int g_lastStage = -2;
int g_spawnCooldown = 0;
int g_lastWorldLogged = -3;   // what the last "world" line said (stage, or -2 for out of the world)
int g_redirects = 0;          // enemy targeting answers swapped to a traveller since the last status

// Dusklight's per-frame GPU buffer usage (the same numbers as its F3 debug overlay). Mirrors
// aurora's AuroraStats; resolved by name so no extra link dependency.
struct FrameStats {
    u32 queuedPipelines, createdPipelines, drawCallCount, mergedDrawCallCount;
    u32 lastVertSize, lastUniformSize, lastIndexSize, lastStorageSize, lastTextureUploadSize;
};
using GetStatsFn = const FrameStats* (*)();
GetStatsFn g_getStats = nullptr;
bool g_statsTried = false;
constexpr u32 kIndexBufferSize = 2u * 1024u * 1024u;    // aurora IndexBufferSize
constexpr u32 kVertexBufferSize = 5u * 1024u * 1024u;   // aurora VertexBufferSize

const FrameStats* frameStats() {
    if (!g_statsTried && g_diag != nullptr) {
        g_statsTried = true;
        void* addr = nullptr;
        if (svc_hook != nullptr && svc_hook->resolve != nullptr &&
            svc_hook->resolve(mod_ctx, "aurora_get_stats", &addr, nullptr) == MOD_OK && addr != nullptr) {
            g_getStats = reinterpret_cast<GetStatsFn>(addr);
        }
        diag("stats   aurora_get_stats %s", g_getStats != nullptr ? "found" : "NOT FOUND");
    }
    return g_getStats != nullptr ? g_getStats() : nullptr;
}

// Everything actually in the world, counted straight from the game's process list (not from
// what this mod thinks it spawned), plus the busiest actor types.
struct WorldCount {
    int actors = 0, enemies = 0, passers = 0, ours = 0, passersDrawn = 0;
    std::array<std::pair<s16, int>, 64> names{};   // profile name -> count
    int nameCount = 0;
};
WorldCount countWorld(bool byName) {
    WorldCount c;
    struct Ctx { WorldCount* c; bool byName; } ctx{&c, byName};
    fpcM_Search([](void* p, void* data) -> void* {
        if (!fopAcM_IsActor(p)) return nullptr;
        auto* x = static_cast<Ctx*>(data);
        auto* a = static_cast<fopAc_ac_c*>(p);
        ++x->c->actors;
        if (fopAcM_GetGroup(a) == fopAc_ENEMY_e) ++x->c->enemies;
        const s16 name = fopAcM_GetName(a);
        if (name == fpcNm_NPC_PASSER_e) {
            ++x->c->passers;
            if (isTraveller(a)) ++x->c->ours;
            if (!fopAcM_CheckCondition(a, fopAcCnd_NODRAW_e)) ++x->c->passersDrawn;
        }
        if (x->byName) {
            int i = 0;
            while (i < x->c->nameCount && x->c->names[i].first != name) ++i;
            if (i < x->c->nameCount) {
                ++x->c->names[i].second;
            } else if (x->c->nameCount < static_cast<int>(x->c->names.size())) {
                x->c->names[x->c->nameCount++] = {name, 1};
            }
        }
        return nullptr;
    }, &ctx);
    return c;
}

// Creates we stopped waiting for while the game might still finish them. If one turns up later
// it is deleted, so it can never become an untracked extra NPC.
std::array<fpc_ProcID, 16> g_orphans{};
void addOrphan(fpc_ProcID id) {
    for (auto& o : g_orphans) {
        if (o == 0 || o == fpcM_ERROR_PROCESS_ID_e) { o = id; return; }
    }
}
void sweepOrphans() {
    for (auto& o : g_orphans) {
        if (o == 0 || o == fpcM_ERROR_PROCESS_ID_e) continue;
        if (fopAc_ac_c* a = fopAcM_SearchByID(o)) {
            diag("orphan  deleted a late traveller (proc %u)", static_cast<unsigned>(o));
            fopAcM_delete(a);
            o = fpcM_ERROR_PROCESS_ID_e;
        } else if (!fpcM_IsCreating(o)) {
            o = fpcM_ERROR_PROCESS_ID_e;   // never made it: nothing to clean up
        }
    }
}

u32 rnd() {
    g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5;
    return g_rng;
}
int rndRange(int lo, int hi) { return lo + static_cast<int>(rnd() % static_cast<u32>(hi - lo + 1)); }

int hour() { return dKy_getdaytime_hour(); }
bool isNight() { const int h = hour(); return h >= 19 || h < 6; }
bool isDusk() { const int h = hour(); return h >= 18 || h < 6; }
// From late afternoon they head for camp (or town), so most arrive around sunset.
bool isEvening() { const int h = hour(); return h >= 16 || h < 6; }
// Properly dark: nobody is out on the roads.
bool isDeepNight() { const int h = hour(); return h >= 20 || h < 5; }

void fillPath(PathStore& store, const std::vector<cXyz>& pts) {
    const size_t n = std::min(pts.size(), static_cast<size_t>(kMaxPathPoints));
    for (size_t i = 0; i < n; ++i) {
        dPnt& p = store.points[i];
        p.mArg0 = p.mArg1 = p.mArg2 = p.mArg3 = 0xFF;
        p.m_position = Vec{pts[i].x, pts[i].y, pts[i].z};
    }
    store.path.m_num = static_cast<u16>(n);
    store.path.m_nextID = 0xFFFF;
    store.path.field_0x4 = 0;
    store.path.m_closed = false;
    store.path.swbit = 0xFF;
    store.path.field_0x7 = 0;
    const auto offset = static_cast<s32>(reinterpret_cast<char*>(store.points.data()) -
                                         reinterpret_cast<char*>(&store.path.m_points));
    store.path.m_points.value.value = static_cast<s32>(static_cast<u32>(offset) | 0x80000000u);
}

bool fighterType(int type);

// --- camp seats ---------------------------------------------------------------------------------
// The amphitheater camp just outside Castle Town (the one camp in the Castle Town field area).
int amphitheaterNode() {
    for (int c = 0; c < kCampCount; ++c) {
        const int n = kCamps[c].node;
        if (kNodes[n].kind == NK_CAMP && kNodes[n].stage == 1) return n;
    }
    return -1;
}

int guardCampNode() {
    for (int n = 0; n < kNodeCount; ++n) if (kNodes[n].kind == NK_GUARD_CAMP) return n;
    return -1;
}

int campSeats(int campNode) {
    if (campNode == guardCampNode()) return kGuardSeats;
    return campNode == amphitheaterNode() ? kAmphiSeats : kCampSeats;
}

int seatsTaken(int campNode) {
    int n = 0;
    for (const Traveller& t : g_t) if (t.used && t.campNode == campNode ) ++n;
    return n;
}

int fightersAt(int campNode) {
    int n = 0;
    for (const Traveller& t : g_t) if (t.used && t.campNode == campNode && fighterType(t.type)) ++n;
    return n;
}

// Can t sit at this camp: it already has a seat there, or there's a free one (and, for a fighter,
// the fire doesn't already have its two fighters).
bool bardAway(int campNode) {   // this fire's bard isn't in his seat (yet): keep one free for him
    for (const Traveller& o : g_t) {
        if (o.used && !o.dead && o.homeCamp == campNode) return o.campNode != campNode;
    }
    return false;
}
bool canSit(const Traveller& t, int campNode) {
    if (t.campNode == campNode) return true;
    if (t.homeCamp == campNode) return seatsTaken(campNode) < campSeats(campNode);
    if (seatsTaken(campNode) >= campSeats(campNode) - (bardAway(campNode) ? 1 : 0)) return false;
    return !fighterType(t.type) || fightersAt(campNode) < kMaxCampFighters;
}

void releaseSeat(Traveller& t) {
    t.campNode = -1;
    t.seat = -1;
}

// Reserve a seat at a camp for t; false if the fire is full.
bool takeSeat(Traveller& t, int campNode) {
    if (t.campNode == campNode) return true;
    if (!canSit(t, campNode)) return false;
    bool used[kMaxSeats] = {};
    for (const Traveller& o : g_t) {
        if (o.used && o.campNode == campNode && o.seat >= 0 && o.seat < kMaxSeats) used[o.seat] = true;
    }
    for (int i = 0; i < campSeats(campNode); ++i) {
        if (!used[i]) {
            t.campNode = campNode;
            t.seat = i;
            return true;
        }
    }
    return false;
}

bool bedTime();
bool takeBed(Traveller& t, int campNode);
int freeBeds(int campNode);
// Nearest camp (by road) that still has a free seat for t; -1 if none can be reached.
int nearestCamp(const Traveller& t, int from, bool escort) {
    int best = -1;
    f32 bestLen = 1e30f;
    for (int c = 0; c < kCampCount; ++c) {
        const int n = kCamps[c].node;
        if (kNodes[n].kind != NK_CAMP) continue;
        if (!canSit(t, n) && !(bedTime() && freeBeds(n) > 0)) continue;
        if (n == from) return n;
        auto r = plan(from, n, escort);
        if (r.empty()) continue;
        const f32 L = routeLength(r);
        if (L < bestLen) { bestLen = L; best = n; }
    }
    return best;
}

// Nearest Castle Town gate by road, or -1.
int nearestGate(int from) {
    int best = -1;
    f32 bestLen = 1e30f;
    for (int n = 0; n < kNodeCount; ++n) {
        if (kNodes[n].kind != NK_GATE) continue;
        if (n == from) return n;
        auto r = plan(from, n, false);
        if (r.empty()) continue;
        const f32 L = routeLength(r);
        if (L < bestLen) { bestLen = L; best = n; }
    }
    return best;
}

bool beedleNodeOk(int n) { return !(kNodes[n].stage == 1 && kNodes[n].room == 8); }

int randomDestination(const Traveller& t, int from) {
    PlanAs as(t.beedle);
    std::vector<int> dests;
    if (t.beedle) {   // anywhere along the roads (he walks past the camps; never into town, never his tent)
        for (int n = 0; n < kNodeCount; ++n) {
            if (n != from && (kNodes[n].kind == NK_ROAD || kNodes[n].kind == NK_CAMP) && beedleNodeOk(n)) dests.push_back(n);
        }
        for (int tries = 0; tries < 12 && !dests.empty(); ++tries) {
            const int n = dests[rnd() % dests.size()];
            if (!plan(from, n, false).empty()) return n;
        }
        return -1;
    }
    for (int n = 0; n < kNodeCount; ++n) {
        if (n == from) continue;
        const u8 k = kNodes[n].kind;
        if (k == NK_GATE) {
            dests.push_back(n);
        } else if (k == NK_CAMP && canSit(t, n)) {
            dests.push_back(n);
        } else if (k == NK_BEEDLE && rndRange(0, 3) == 0) {
            dests.push_back(n);   // the odd visit to Beedle's shop
        }
    }
    for (int tries = 0; tries < 8 && !dests.empty(); ++tries) {
        const int n = dests[rnd() % dests.size()];
        if (!plan(from, n, false).empty()) return n;
    }
    return -1;
}

// Where to spend the night from here: in the Castle Town field area always the amphitheater (into
// town if it's full); everyone else the nearest camp with a free seat (into town if every camp is full).
int nightDestination(Traveller& t, int from) {
    if (t.beedle) return randomDestination(t, from);   // no camping: he keeps on wandering
    if (t.homeCamp >= 0 && canSit(t, t.homeCamp) && (t.homeCamp == from || !plan(from, t.homeCamp, false).empty())) {
        return t.homeCamp;   // a bard goes back to his own fire
    }
    if (kNodes[from].stage == 1) {
        const int amphi = amphitheaterNode();
        if (amphi >= 0 && canSit(t, amphi) &&
            (amphi == from || !plan(from, amphi, false).empty())) {
            return amphi;
        }
        const int g = nearestGate(from);
        if (g >= 0) return g;
    }
    const int camp = nearestCamp(t, from, false);
    if (camp >= 0) return camp;
    return nearestGate(from);
}

void arrive(Traveller& t, int node);

void newRoute(Traveller& t, int from) {
    PlanAs as(t.beedle);
    const int to = isEvening() ? nightDestination(t, from) : randomDestination(t, from);
    if (to >= 0 && to == from) {   // already where it wants to be (a camp at dusk)
        arrive(t, from);
        return;
    }
    t.route = to >= 0 ? plan(from, to, false) : std::vector<Leg>{};
    // Seats: hold one only at the camp it is heading for (or still sitting at).
    if (!t.route.empty() && kNodes[to].kind == NK_CAMP) {
        if (t.campNode != to) {
            releaseSeat(t);
            if (!takeSeat(t, to)) t.route.clear();
        }
    } else if (!t.route.empty()) {
        releaseSeat(t);
    }
    t.leg = 0;
    t.dist = 0.0f;
    t.node = from;
    if (t.route.empty()) {
        t.state = ST_REST;
        t.timer = rndRange(600, 1800);
        t.restUntilMorning = false;
    } else {
        t.state = ST_WALK;
    }
}

void arrive(Traveller& t, int node) {
    t.node = node;
    t.route.clear();
    t.leg = 0;
    t.dist = 0.0f;
    t.idleSet = false;
    if (t.beedle) {   // a short look round, then on his way again
        t.state = ST_REST;
        t.restUntilMorning = false;
        t.timer = rndRange(150, 500);
        return;
    }
    switch (kNodes[node].kind) {
    case NK_GATE:
        releaseSeat(t);
        t.state = ST_TOWN;                       // into Castle Town for a while (all night at dusk)
        t.timer = rndRange(1800, 5400);
        break;
    case NK_CAMP:
        if (!takeSeat(t, node)) {
            if (bedTime() && takeBed(t, node)) {   // the fire is full but it's late: straight to bed
                t.state = ST_REST;
                t.restUntilMorning = true;
                t.sleep = t.spawned ? 1 : 2;
                t.sleepFrames = 0;
                t.timer = rndRange(900, 2400);
                break;
            }
            // the fire is full: a short breather, then on to somewhere else
            t.state = ST_REST;
            t.restUntilMorning = false;
            t.timer = rndRange(200, 500);
            break;
        }
        t.state = ST_REST;
        t.restUntilMorning = isEvening();
        t.timer = rndRange(900, 2400);
        break;
    default:
        t.state = ST_REST;
        t.restUntilMorning = false;
        t.timer = rndRange(400, 1200);
        break;
    }
}

int currentStage() {
    const char* s = dComIfGp_getStartStageName();
    if (s == nullptr) return -1;
    for (int i = 0; i < 2; ++i) {
        if (std::strcmp(s, kStages[i]) == 0) return i;
    }
    return -1;
}

int slotOf(const fopAc_ac_c* actor) {
    if (actor == nullptr || fopAcM_GetName(const_cast<fopAc_ac_c*>(actor)) != fpcNm_NPC_PASSER_e) return -1;
    const int pathId = (fopAcM_GetParam(actor) >> 16) & 0xFF;
    if (pathId < kWalkPathBase || pathId >= kWalkPathBase + kSlots) return -1;
    const int slot = pathId - kWalkPathBase;
    const Traveller& t = g_t[slot];
    if (!t.used || !t.spawned || t.actor != fopAcM_GetID(const_cast<fopAc_ac_c*>(actor))) return -1;
    return slot;
}

bool fighterType(int type) {
    return std::find(kFighters.begin(), kFighters.end(), static_cast<u8>(type)) != kFighters.end();
}
int fullLife(int type) { return (fighterType(type) ? kFighterHearts : kCommonHearts) * kQuarters; }
int lifeOf(Traveller& t) {
    if (t.life < 0 || t.life > fullLife(t.type)) t.life = fullLife(t.type);
    return t.life;
}
void heal(Traveller& t) {
    if (lifeOf(t) >= fullLife(t.type)) return;
    const int every = (t.state == ST_REST || t.state == ST_TOWN) ? kHealFramesResting : kHealFrames;
    if (++t.healTimer >= every) {
        t.healTimer = 0;
        ++t.life;
    }
}

// Walk path for a spawned traveller from where it is now: ahead along its route, stopping at the
// route's end, a loading screen or kMaxPathPoints.
void buildWalkPath(int slot, const cXyz& from) {
    Traveller& t = g_t[slot];
    std::vector<cXyz> pts{from};
    t.pathMap.clear();
    t.pathMap.push_back({t.leg, t.dist});
    t.pathEnd = PE_CONTINUE;
    t.beyond = {-1, 0.0f};
    t.leaving = false;
    if (t.state != ST_WALK || t.route.empty()) {
        // resting: a two-point path to its spot by the fire
        cXyz spot = from;
        spot.x += 1.0f;
        pts.push_back(spot);
        t.pathMap.push_back({t.leg, t.dist});
        t.pathEnd = PE_ARRIVE;
        fillPath(g_walk[slot], pts);
        return;
    }
    const Where here = locate(t.route[t.leg], t.dist);
    int leg = t.leg;
    int i = here.nextPoint;
    u8 stage = here.stage;
    while (static_cast<int>(pts.size()) < kMaxPathPoints) {
        const Leg& l = t.route[leg];
        const int n = kEdges[l.edge].count;
        if (i >= n) {
            ++leg;
            if (leg >= static_cast<int>(t.route.size())) {
                const u8 k = kNodes[legEnd(t.route.back())].kind;
                t.pathEnd = k == NK_GATE ? PE_VANISH : PE_ARRIVE;
                break;
            }
            i = 1;
            continue;
        }
        const Point& p = legPoint(l, i);
        if (p.stage != stage) {     // loading screen ahead: walk to it, then vanish
            t.pathEnd = PE_VANISH;
            t.beyond = {leg, legPointDist(l, i)};
            break;
        }
        if (p.room < 0 || !dComIfGp_roomControl_checkRoomDisp(p.room)) {
            // walking into a room that isn't loaded: vanish at its edge and carry on virtually
            // from just inside it (so this room's spawner doesn't bring it straight back)
            t.pathEnd = PE_VANISH;
            t.beyond = {leg, legPointDist(l, i)};
            break;
        }
        pts.push_back(cXyz(p.x, p.y, p.z));
        t.pathMap.push_back({leg, legPointDist(l, i)});
        ++i;
    }
    if (pts.size() < 2) {
        pts.push_back(from);
        t.pathMap.push_back({t.leg, t.dist});
    }
    fillPath(g_walk[slot], pts);
}

// How directly a stretch of road heads away from a threat: +1 straight away, -1 straight at it.
// Looks ~500 units down the leg from distance d.
f32 awayScore(const Leg& l, f32 d, const cXyz& here, const cXyz& threat) {
    const Where ahead = locate(l, std::min(d + 500.0f, edgeLength(l.edge)));
    f32 rx = ahead.pos.x - here.x, rz = ahead.pos.z - here.z;
    f32 ax = here.x - threat.x, az = here.z - threat.z;
    const f32 rl = std::sqrt(rx * rx + rz * rz), al = std::sqrt(ax * ax + az * az);
    if (rl < 1.0f) return -2.0f;
    if (al < 1.0f) return 0.0f;
    return (rx * ax + rz * az) / (rl * al);
}

// Flee path: along whichever road leads most directly away from the threat, choosing again at
// every fork, up to kFleeDistance (stopping at a camp, gate, loading zone or unloaded room).
void buildFleePath(int slot, const cXyz& from, const cXyz& threat) {
    Traveller& t = g_t[slot];
    std::vector<cXyz> pts{from};
    t.fleeMap.clear();
    t.fleeLegs.clear();
    // Where on the roads it is, and which ways it could run from there.
    std::vector<std::pair<Leg, f32>> options;
    int startNode = -1;
    if (t.state == ST_WALK && !t.route.empty()) {
        const Leg l = t.route[t.leg];
        options.push_back({l, t.dist});
        options.push_back({Leg{l.edge, !l.fwd}, edgeLength(l.edge) - t.dist});
    } else {
        startNode = t.node;
        for (const Adj& a : g_adj[t.node]) {
            if (!edgeAllowed(a.edge, false)) continue;
            options.push_back({Leg{static_cast<u16>(a.edge), kEdges[a.edge].a == t.node}, 0.0f});
        }
    }
    int best = -1;
    f32 bestScore = -10.0f;
    for (int i = 0; i < static_cast<int>(options.size()); ++i) {
        const f32 sc = awayScore(options[i].first, options[i].second, from, threat);
        if (sc > bestScore) { bestScore = sc; best = i; }
    }
    if (best >= 0) {
        Leg leg = options[best].first;
        f32 d = options[best].second;
        const u8 stage0 = startNode >= 0 ? kNodes[startNode].stage : locate(leg, d).stage;
        t.fleeLegs.push_back(leg);
        t.fleeMap.push_back({0, d});
        f32 run = 0.0f;
        cXyz last = from;
        bool done = false;
        while (!done && static_cast<int>(pts.size()) < kMaxPathPoints) {
            const int n = kEdges[leg.edge].count;
            int i = 1;
            while (i < n && legPointDist(leg, i) <= d) ++i;   // next point ahead
            for (; i < n && run < kFleeDistance && static_cast<int>(pts.size()) < kMaxPathPoints; ++i) {
                const Point& p = legPoint(leg, i);
                if (p.stage != stage0 || p.room < 0 || !dComIfGp_roomControl_checkRoomDisp(p.room)) {
                    done = true;
                    break;
                }
                const cXyz q(p.x, p.y, p.z);
                run += std::sqrt((q.x - last.x) * (q.x - last.x) + (q.z - last.z) * (q.z - last.z));
                pts.push_back(q);
                t.fleeMap.push_back({static_cast<int>(t.fleeLegs.size()) - 1, legPointDist(leg, i)});
                last = q;
            }
            if (done || run >= kFleeDistance || i < n) break;
            // reached a junction: pick the next road that heads away (never straight back)
            const int node = legEnd(leg);
            if (!passThrough(node)) break;
            int nextBest = -1;
            f32 nextScore = -10.0f;
            Leg next{};
            for (const Adj& a : g_adj[node]) {
                if (a.edge == leg.edge || !edgeAllowed(a.edge, false)) continue;
                const Leg cand{static_cast<u16>(a.edge), kEdges[a.edge].a == node};
                const f32 sc = awayScore(cand, 0.0f, last, threat);
                if (sc > nextScore) { nextScore = sc; nextBest = a.edge; next = cand; }
            }
            if (nextBest < 0) break;
            leg = next;
            d = 0.0f;
            t.fleeLegs.push_back(leg);
        }
    }
    if (pts.size() < 2) {
        // no road to run along: just get some distance from the threat
        cXyz away = from;
        const s16 a = cLib_targetAngleY(&threat, &from);
        away.x += cM_ssin(a) * 1200.0f;
        away.z += cM_scos(a) * 1200.0f;
        pts.push_back(away);
        t.fleeMap.clear();
        t.fleeLegs.clear();
    }
    fillPath(g_flee[slot], pts);
}

void spawn(int slot, const Where& w) {
    Traveller& t = g_t[slot];
    buildWalkPath(slot, w.pos);
    const u32 params = static_cast<u32>(t.type) | (0u << 8) |
                       (static_cast<u32>(kWalkPathBase + slot) << 16) | (1u << 24);
    const csXyz angle(0xFF, w.yaw, 0);   // angle.x 0xFF: no vanilla leave-at-a-time-of-day timer
    cXyz pos = w.pos;
    diag("spawn   slot %d type %d room %d at (%.0f %.0f %.0f)", slot, t.type, w.room, pos.x, pos.y, pos.z);
    const fpc_ProcID id = fopAcM_create(fpcNm_NPC_PASSER_e, params, &pos, w.room, &angle, nullptr, -1);
    if (id == fpcM_ERROR_PROCESS_ID_e) { diag("spawn   slot %d FAILED", slot); return; }
    t.actor = id;
    t.spawned = true;
    t.registered = false;
    t.createFrames = 0;
    t.idleSet = false;
}

void despawn(Traveller& t, fopAc_ac_c* actor) {
    diag("despawn slot %d", static_cast<int>(&t - g_t.data()));
    if (actor != nullptr) fopAcM_delete(actor);
    t.spawned = false;
    t.registered = false;
    t.actor = fpcM_ERROR_PROCESS_ID_e;
}

void syncFromActor(Traveller& t, daNpcPasser_c* p) {
    if (t.state != ST_WALK || t.pathMap.empty() || !p->m_path.isPath()) return;
    const int idx = std::clamp(static_cast<int>(p->m_path.field_0x20) - 1, 0, static_cast<int>(t.pathMap.size()) - 1);
    if (t.pathMap[idx].first >= t.leg) {
        t.leg = t.pathMap[idx].first;
        t.dist = t.pathMap[idx].second;
    }
}

void setIdle(daNpcPasser_c* p) {
    p->speedF = 0.0f;
    // at its own seat by a camp fire: sit down on its log (only once it's really there: away from it,
    // the camp guard walks it back first)
    cXyz seat;
    if (campInfo(p, nullptr, &seat, nullptr) && p->current.pos.absXZ(seat) <= 40.0f && traveller_gear::sit(p, true)) return;
    traveller_gear::sit(p, false);
    auto* idle = static_cast<J3DAnmTransformKey*>(p->getAnmP(0, p->getObjNum()));
    if (guardStyle(p) == kStyleSpear) {   // a spearman stands leaning on his spear
        if (auto* stick = static_cast<J3DAnmTransformKey*>(dComIfG_getObjectRes("Mspecial", "m_wait_stick.bck"))) idle = stick;
    }
    if (idle != nullptr) {
        p->setAnm(idle, 1.0f, 12.0f, J3DFrameCtrl::EMode_LOOP, 0, -1);
    }
}

// Advance a traveller that isn't a real actor right now.
// In the evening, someone still heading somewhere for the day turns toward the night's camp (or
// town) from the end of the stretch it is on. Works for travellers on paper and on screen alike.
void eveningReroute(Traveller& t) {
    if (!isEvening() || t.state != ST_WALK || t.route.empty()) return;
    const int dest = legEnd(t.route.back());
    if (kNodes[dest].kind == NK_GATE || (kNodes[dest].kind == NK_CAMP && t.campNode == dest)) return;
    const int from = legEnd(t.route[t.leg]);
    const int night = nightDestination(t, from);
    if (night < 0 || night == dest) return;
    std::vector<Leg> rest = night == from ? std::vector<Leg>{} : plan(from, night, false);
    if (night != from && rest.empty()) return;
    t.route.resize(t.leg + 1);
    t.route.insert(t.route.end(), rest.begin(), rest.end());
    if (kNodes[night].kind == NK_CAMP) {
        if (t.campNode != night) { releaseSeat(t); takeSeat(t, night); }
    } else {
        releaseSeat(t);
    }
}

void stepVirtual(Traveller& t) {
    if (t.state == ST_WALK) {
        if (t.route.empty()) { newRoute(t, t.node); return; }
        t.dist += kVirtualSpeed;
        while (t.state == ST_WALK && t.dist >= edgeLength(t.route[t.leg].edge)) {
            t.dist -= edgeLength(t.route[t.leg].edge);
            t.node = legEnd(t.route[t.leg]);
            ++t.leg;
            if (t.leg >= static_cast<int>(t.route.size())) {
                arrive(t, t.node);
                break;
            }
            // evening caught it on the road: head for the night's camp (or town) instead
            eveningReroute(t);
        }
    }
}

void stepRestOrTown(Traveller& t) {
    if (t.guard) {
        // A guard never takes to the road. One knocked out comes back to the fire after a while.
        if (t.state == ST_GONE && --t.timer <= 0) {
            t.dead = false;
            t.life = -1;
            t.node = guardCampNode();
            t.state = ST_REST;
            t.restUntilMorning = true;
            takeSeat(t, t.node);
        }
        return;
    }
    if (t.state == ST_REST) {
        if (t.restUntilMorning) {
            const int h = hour();
            if (h >= 6 && h < 16 && rndRange(0, h >= 8 ? 60 : 900) == 0) t.restUntilMorning = false;   // they wake between 6 and 8
            return;
        }
        if (--t.timer <= 0) newRoute(t, t.node);
    } else if (t.state == ST_TOWN) {
        if (--t.timer > 0) return;
        if (isEvening()) {           // stays in town overnight, out again in the morning (not all at once)
            t.timer = rndRange(600, 7200);
            return;
        }
        // out of Castle Town through one of its gates
        std::vector<int> gates;
        for (int n = 0; n < kNodeCount; ++n) {
            if (kNodes[n].kind == NK_GATE && (!t.beedle || beedleNodeOk(n))) gates.push_back(n);
        }
        const int g = gates.empty() ? t.node : gates[rnd() % gates.size()];
        newRoute(t, g);
    } else if (t.state == ST_GONE) {
        if (--t.timer <= 0) {
            // a new traveller arrives on the road out of Castle Town
            if (t.homeCamp < 0 && !t.beedle) t.type = kWalkCast[rnd() % kWalkCast.size()];   // a bard stays his fire's bard
            t.dead = false;
            t.life = -1;
            t.state = ST_TOWN;
            t.timer = 1;
        }
    }
}

// Seat i at a camp: from 135 deg left of the camp's facing round to 45 deg right of it,
// 185 out from the fire (220 at the amphitheater), facing the fire.
bool seatSpot(int campNode, int seat, cXyz* pos, s16* yaw) {
    if (campNode < 0 || seat < 0) return false;
    const int seats = campSeats(campNode);
    if (seat >= seats) return false;
    const Node& n = kNodes[campNode];
    const f32 ring = seats > kCampSeats ? 220.0f : 185.0f;
    const int step = seats > 1 ? 0x8000 / (seats - 1) : 0;
    const s16 a = static_cast<s16>(n.facing - 0x6000 + seat * step);
    if (pos) pos->set(n.x + cM_ssin(a) * ring, n.y, n.z + cM_scos(a) * ring);
    if (yaw) *yaw = static_cast<s16>(a + 0x8000);
    return true;
}

bool bedSpot(const Traveller& t, cXyz* inside, cXyz* doorstep, s16* faceIn);
Where restWhere(const Traveller& t, int slot) {
    const Node& n = kNodes[t.node];
    Where w{};
    w.stage = n.stage;
    w.room = n.room;
    if (t.sleep == 3 && bedSpot(t, &w.pos, nullptr, &w.yaw)) {   // crawling out of its tent
        w.yaw = static_cast<s16>(w.yaw + 0x8000);
        w.nextPoint = 0;
        return w;
    }
    // A camp seat: on a half-ring in front of the fire, away from the tents behind it and the
    // small tent at its side (see seatSpot). Anyone without a seat (resting at a road junction,
    // say) just spreads out around the spot by its slot.
    if (t.seat >= 0 && seatSpot(t.node, t.seat, &w.pos, &w.yaw)) {
        w.nextPoint = 0;
        return w;
    }
    const f32 ring = 300.0f;
    const s16 a = static_cast<s16>(slot * (0x10000 / kCampSeats) + 0x1555);
    w.pos.set(n.x + cM_ssin(a) * ring, n.y, n.z + cM_scos(a) * ring);
    w.yaw = static_cast<s16>(a + 0x8000);   // facing the middle
    w.nextPoint = 0;
    return w;
}

// --- camps: tents appear when their room is loaded and Link is near -------------------------
struct CampTents {
    std::vector<ActorId> ids;
    bool up = false;
    // the small tents (beds 2*i and 2*i+1 are in tent i)
    cXyz tentPos[4];
    s16 tentYaw[4] = {};
    int tents = 0;
    bool rolled = false;   // tonight's stay-ups picked
    // the guards' camp: the game's own Castle Town pennant on their tent's ridge, shrunk once made
    fpc_ProcID flag = fpcM_ERROR_PROCESS_ID_e;
    bool flagShrunk = false;
};
std::array<CampTents, kCampCount> g_camps{};

// Which of Yeto's soups a camp's pot holds, by province (the game's own field-room split,
// dKy_F_SP121Check): Faron 1 (Simple), Eldin 2 (Good), Lanayru and Castle Town's field 3 (Superb).
u32 soupLevel(int stage, int room) {
    if (stage == 1) return 3;
    if (room == 1 || room == 6 || room == 15) return 1;
    if (room >= 9 && room <= 14) return 3;
    return 2;
}

void spawnFire(CampTents& c, u32 params, const cXyz& at, s16 yaw, s8 room) {
    diag("fire    camp node %d soup %d room %d", static_cast<int>(params & 0xFF), static_cast<int>(params >> 8), room);
    ActorSpawnParams p{};
    p.parameters = params;
    p.argument = -1;
    p.room_num = room;
    p.position.x = at.x; p.position.y = at.y; p.position.z = at.z;
    p.angle.y = yaw;
    p.scale.x = p.scale.y = p.scale.z = 1.0f;
    ActorId id{};
    if (svc_actor->create_actor_from_name(mod_ctx, PSR_FIRE_NAME, &p, &id) == MOD_OK) c.ids.push_back(id);
}

void spawnTent(CampTents& c, u32 kind, const cXyz& at, s16 yaw, s8 room) {
    diag("tent    kind %d room %d", static_cast<int>(kind & 0xFF), room);
    ActorSpawnParams p{};
    p.parameters = kind;
    p.argument = -1;
    p.room_num = room;
    p.position.x = at.x; p.position.y = at.y; p.position.z = at.z;
    p.angle.y = yaw;
    p.scale.x = p.scale.y = p.scale.z = 1.0f;
    ActorId id{};
    if (svc_actor->create_actor_from_name(mod_ctx, PSR_TENT_NAME, &p, &id) == MOD_OK) c.ids.push_back(id);
    g_spawnCooldown = kSpawnGap;
}

// --- where the tents stand: the flattest open ground around the camp ----------------------------
f32 probeGround(f32 x, f32 y, f32 z) {
    dBgS_ObjGndChk gnd;
    cXyz probe(x, y + 600.0f, z);
    gnd.SetPos(&probe);
    return dComIfG_Bgsp().GroundCross(&gnd);
}

bool underWater(f32 x, f32 ground, f32 z) {
    dBgS_WtrChk w;
    cXyz at(x, ground - 50.0f, z);
    w.Set(at, ground + 400.0f);
    return dComIfG_Bgsp().WaterChk(&w) && w.GetHeight() > ground - 5.0f;
}

// Closest distance from a point to a tent's footprint (a rectangle turned by yaw), on the ground plane.
f32 distToFootprint(const cXyz& at, s16 yaw, f32 hx, f32 hz, f32 px, f32 pz) {
    const f32 c = cM_scos(yaw), sn = cM_ssin(yaw);
    const f32 dx = px - at.x, dz = pz - at.z;
    const f32 lx = dx * c - dz * sn, lz = dx * sn + dz * c;   // into the tent's own frame
    const f32 ox = std::max(0.0f, std::abs(lx) - hx), oz = std::max(0.0f, std::abs(lz) - hz);
    return std::sqrt(ox * ox + oz * oz);
}

constexpr int kCampTents = 3;          // small A-tents at every camp

// Camps whose tents are placed by hand instead of searched for (where the ground around the fire
// is too tight for the search: camp 53 sits between the cliff and the road by the bridge).
struct HandTent { f32 x, y, z; u16 yaw; };
struct HandTents { int node; int count; HandTent tent[4]; };
constexpr HandTents kHandTents[] = {
    {53, 3, {{-12447.8f, -1075.3f, 24815.9f, 55323}, {-13152.3f, -1075.3f, 25854.0f, 25875},
             {-11450.0f, -1075.3f, 26000.0f, 44829}}},
};
const HandTents* handTents(int node) {
    for (const HandTents& h : kHandTents) if (h.node == node) return &h;
    return nullptr;
}

struct TentSpot {
    cXyz pos;
    s16 yaw;
    f32 hx, hz;
};

// How bumpy the ground under a footprint is (highest minus lowest of a 5 x 5 grid of probes), or
// -1 if it can't stand there (no ground, water, or a ledge well above or below the camp).
f32 bumpiness(const cXyz& at, s16 yaw, f32 hx, f32 hz, f32 refY, bool fine = true) {
    const f32 c = cM_scos(yaw), sn = cM_ssin(yaw);
    f32 lo = 1e9f, hi = -1e9f;
    const int step = fine ? 1 : 2;   // rough pass: corners, edge middles and centre only
    for (int i = 0; i <= 4; i += step) {
        for (int j = 0; j <= 4; j += step) {
            const f32 lx = hx * (i * 0.5f - 1.0f), lz = hz * (j * 0.5f - 1.0f);
            const f32 x = at.x + lx * c + lz * sn, z = at.z - lx * sn + lz * c;
            const f32 g = probeGround(x, refY, z);
            if (g == -G_CM3D_F_INF || std::abs(g - refY) > 300.0f) return -1.0f;
            if (fine && ((i | j) & 1) == 0 && underWater(x, g, z)) return -1.0f;
            lo = std::min(lo, g);
            hi = std::max(hi, g);
        }
    }
    return hi - lo;
}

// Search rings around the fire for the flattest spot a tent fits: clear of the fire, every seat,
// the cook's mat and the tents already placed, with its door toward the fire. Close in is better,
// but flat ground beats close.
bool findTentSpot(int campNode, const cXyz& fire, s16 campYaw, u32 kind, const std::vector<TentSpot>& placed,
                  TentSpot* out, f32 maxBump = 1e9f) {
    const f32 hx = psr_tent::kFootX[kind], hz = psr_tent::kFootZ[kind];
    cXyz seats[16];
    int seatCount = 0;
    for (int i = 0; i < 16 && i < campSeatCount(campNode); i++) {
        s16 y;
        if (campSeatSpot(campNode, i, &seats[seatCount], &y)) ++seatCount;
    }
    cXyz mat;
    s16 matYaw;
    cook::matSpot(fire, campYaw, &mat, &matYaw);
    // rough pass over every candidate that fits, then the closer look at the best few
    struct Cand { f32 score; cXyz at; s16 yaw; };
    std::vector<Cand> cands;
    for (f32 r = 300.0f; r <= 1500.0f; r += 150.0f) {
        for (int a = 0; a < 24; a++) {
            const s16 dir = static_cast<s16>(a * 0x10000 / 24);
            const cXyz at(fire.x + cM_ssin(dir) * r, fire.y, fire.z + cM_scos(dir) * r);
            const s16 yaw = static_cast<s16>(dir + 0x8000);   // door toward the fire
            if (distToFootprint(at, yaw, hx, hz, fire.x, fire.z) < 320.0f) continue;
            if (distToFootprint(at, yaw, hx, hz, mat.x, mat.z) < 160.0f) continue;
            bool clear = true;
            for (int i = 0; i < seatCount && clear; i++) {
                clear = distToFootprint(at, yaw, hx, hz, seats[i].x, seats[i].z) >= 130.0f;
            }
            for (const TentSpot& o : placed) {
                if (!clear) break;
                // keep a walkway between tents: no corner of either near the other
                for (int k = 0; k < 4 && clear; k++) {
                    const f32 cx = (k & 1) ? hx : -hx, cz = (k & 2) ? hz : -hz;
                    const f32 wx = at.x + cx * cM_scos(yaw) + cz * cM_ssin(yaw);
                    const f32 wz = at.z - cx * cM_ssin(yaw) + cz * cM_scos(yaw);
                    const f32 ox = (k & 1) ? o.hx : -o.hx, oz = (k & 2) ? o.hz : -o.hz;
                    const f32 vx = o.pos.x + ox * cM_scos(o.yaw) + oz * cM_ssin(o.yaw);
                    const f32 vz = o.pos.z - ox * cM_ssin(o.yaw) + oz * cM_scos(o.yaw);
                    clear = distToFootprint(o.pos, o.yaw, o.hx, o.hz, wx, wz) >= 120.0f &&
                            distToFootprint(at, yaw, hx, hz, vx, vz) >= 120.0f;
                }
                if (clear) clear = at.abs(o.pos) >= std::max(hx, hz) + std::max(o.hx, o.hz);
            }
            if (!clear) continue;
            const f32 bump = bumpiness(at, yaw, hx, hz, fire.y, false);
            if (bump < 0.0f) continue;
            cands.push_back({bump + r * 0.03f, at, yaw});   // 100 units further out costs 3 of bump
        }
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& a, const Cand& b) { return a.score < b.score; });
    f32 best = 1e9f, bestBump = 0.0f;
    bool found = false;
    for (size_t i = 0; i < cands.size() && i < (maxBump < 1e8f ? 16u : 6u); i++) {
        const f32 bump = bumpiness(cands[i].at, cands[i].yaw, hx, hz, fire.y, true);
        if (bump < 0.0f || bump > maxBump) continue;
        const f32 score = bump + cands[i].at.abs(fire) * 0.03f;
        if (score < best) {
            best = score;
            bestBump = bump;
            found = true;
            *out = {cands[i].at, cands[i].yaw, hx, hz};
        }
    }
    if (found) diag("tentpos kind %d bump %.0f at %.0f out (%d spots fit)", static_cast<int>(kind), bestBump,
                    out->pos.abs(fire), static_cast<int>(cands.size()));
    return found;
}

// Beedle's tent has no fire: look nearby for the flattest open ground, keeping its facing.
bool findBeedleSpot(const cXyz& mid, s16 face, TentSpot* out) {
    const f32 hx = psr_tent::kFootX[PSR_TENT_BEEDLE], hz = psr_tent::kFootZ[PSR_TENT_BEEDLE];
    f32 best = 1e9f;
    bool found = false;
    for (f32 r = 0.0f; r <= 1200.0f; r += 150.0f) {
        for (int a = 0; a < (r == 0.0f ? 1 : 16); a++) {
            const s16 dir = static_cast<s16>(a * 0x1000);
            const cXyz at(mid.x + cM_ssin(dir) * r, mid.y, mid.z + cM_scos(dir) * r);
            const f32 bump = bumpiness(at, face, hx, hz, mid.y);
            if (bump < 0.0f) continue;
            const f32 score = bump + r * 0.03f;
            if (score < best) {
                best = score;
                found = true;
                *out = {at, face, hx, hz};
            }
        }
    }
    return found;
}

// --- beds: the small tents at night ------------------------------------------------------------------
// At each fire the bard, one fighter and a couple of others stay up; everyone else turns in at 22:00
// (the old women at dusk). Someone who reaches a fire at night with every seat taken goes straight to
// bed. Two to a tent. Asleep they aren't actors at all; they crawl out again between 6 and 8.
constexpr int kBedsPerTent = 2;
constexpr int kStayUpOthers = 2;
bool bedTime() { const int h = hour(); return h >= 19 || h < 4; }
bool lateNight() { const int h = hour(); return h >= 22 || h < 4; }
bool oldWomanType(int type) { return type == 20 || type == 27; }
constexpr int kBardType = 4;
int campIndex(int node) {
    for (int i = 0; i < kCampCount; ++i) if (kCamps[i].node == node) return i;
    return -1;
}
int bedsAt(int campNode) {
    if (campNode < 0 || kNodes[campNode].kind != NK_CAMP) return 0;
    return kCampTents * kBedsPerTent;
}
int freeBeds(int campNode) {
    int n = bedsAt(campNode);
    for (const Traveller& o : g_t) if (o.used && o.bedCamp == campNode) --n;
    return n;
}
bool takeBed(Traveller& t, int campNode) {
    if (t.bedCamp == campNode && t.bed >= 0) return true;
    bool used[16] = {};
    for (const Traveller& o : g_t) if (o.used && o.bedCamp == campNode && o.bed >= 0 && o.bed < 16) used[o.bed] = true;
    for (int b = 0; b < bedsAt(campNode); ++b) {
        if (!used[b]) { t.bedCamp = campNode; t.bed = b; return true; }
    }
    return false;
}
void releaseBed(Traveller& t) {
    t.bedCamp = -1;
    t.bed = -1;
    t.sleep = 0;
    t.sleepFrames = 0;
}
// Just inside its tent's door (where it vanishes), the spot on the grass in front of the door, and the
// way into the tent. False while that tent isn't standing (its camp isn't loaded).
bool bedSpot(const Traveller& t, cXyz* inside, cXyz* doorstep, s16* faceIn) {
    const int ci = campIndex(t.bedCamp);
    if (ci < 0 || t.bed < 0) return false;
    const CampTents& c = g_camps[ci];
    const int k = t.bed / kBedsPerTent;
    if (!c.up || k >= c.tents) return false;
    const s16 yaw = c.tentYaw[k];
    const f32 fx = cM_ssin(yaw), fz = cM_scos(yaw), rx = fz, rz = -fx;   // forward (out of the door), right
    const f32 side = (t.bed % kBedsPerTent == 0 ? -1.0f : 1.0f) * 22.0f;
    const cXyz& p = c.tentPos[k];
    if (inside) inside->set(p.x + fx * 110.0f + rx * side, p.y, p.z + fz * 110.0f + rz * side);
    if (doorstep) doorstep->set(p.x + fx * 225.0f + rx * side * 0.5f, p.y, p.z + fz * 225.0f + rz * side * 0.5f);
    if (faceIn) *faceIn = static_cast<s16>(yaw + 0x8000);
    return true;
}
// Tonight's stay-ups at a fire: the bard, one fighter and a couple of others (never the old women).
void rollStayUps(int campNode) {
    std::vector<Traveller*> rest;
    bool fighter = false;
    for (Traveller& o : g_t) {
        if (!o.used || o.campNode != campNode || o.seat < 0) continue;
        o.stayUp = false;
        if (o.type == kBardType) { o.stayUp = true; continue; }
        if (fighterType(o.type) && !fighter) { o.stayUp = fighter = true; continue; }
        if (!oldWomanType(o.type)) rest.push_back(&o);
    }
    for (int k = 0; k < kStayUpOthers && !rest.empty(); ++k) {
        const int i = static_cast<int>(rnd() % rest.size());
        rest[i]->stayUp = true;
        rest.erase(rest.begin() + i);
    }
}
void bedTick(Traveller& t) {
    if (t.dead) { if (t.bedCamp >= 0) releaseBed(t); return; }
    if (t.homeCamp >= 0) t.stayUp = true;   // the bard plays all night
    for (int i = 0; i < kCampCount; ++i) {   // a fresh pick of stay-ups each night
        CampTents& c = g_camps[i];
        if (bedTime() && !c.rolled) { c.rolled = true; rollStayUps(kCamps[i].node); }
        else if (!bedTime() && hour() >= 8) c.rolled = false;
    }
    switch (t.sleep) {
    case 0: {
        // seated at its fire and it's bedtime for it
        if (t.state != ST_REST || t.seat < 0 || t.campNode != t.node || !bedTime()) return;
        const bool want = oldWomanType(t.type) || (lateNight() && !t.stayUp);
        if (!want || !takeBed(t, t.node)) return;
        t.restUntilMorning = true;
        t.sleep = t.spawned ? 1 : 2;
        t.sleepFrames = 0;
        diag("bed     slot %d turns in (camp %d bed %d)", static_cast<int>(&t - g_t.data()), t.bedCamp, t.bed);
        return;
    }
    case 1:   // walking to its tent and crawling in (the actor does it; tuckedIn ends it)
        if (!t.spawned || ++t.sleepFrames > 1800) {
            if (t.spawned) despawn(t, fopAcM_SearchByID(t.actor));
            t.sleep = 2;
        }
        return;
    case 2:   // asleep: up again once morning wakes it (stepRestOrTown clears restUntilMorning)
        if (t.restUntilMorning) return;
        if (bedSpot(t, nullptr, nullptr, nullptr)) { t.sleep = 3; t.sleepFrames = 0; t.cooldown = 0; }
        else releaseBed(t);
        return;
    case 3:   // waiting to crawl out (its spawner brings it); nobody watching: it's just up
        if (!t.spawned && ++t.sleepFrames > 900) releaseBed(t);
        return;
    }
}

// --- the guards' camp: their red wall tent behind the fire, the pennant on its ridge -------------
constexpr f32 kGuardTentBack = 930.0f;    // fire to tent middle (the tent's half depth is 580)
constexpr f32 kGuardTentSide = 1150.0f;   // fire to each side tent's middle
constexpr f32 kPennantScale = 0.3f;
constexpr u32 kPennantPole = 0x000C;      // @bg000c: Castle Town's pennant pole (its spec height is 500)
constexpr s16 kPennantCloth = 1;          // FlagObj01: the orange crest
// Where the pennant stands on the tent: the front end of the ridge (tent space, before its 1.3 scale
// and with the pole's foot a little into the ridge).
constexpr f32 kRidgeY = 370.0f, kRidgeFrontZ = 313.0f;

void spawnPennant(CampTents& c, const cXyz& tent, s16 yaw, s8 room) {
    const f32 fx = cM_ssin(yaw), fz = cM_scos(yaw);
    cXyz at(tent.x + fx * kRidgeFrontZ, tent.y + kRidgeY, tent.z + fz * kRidgeFrontZ);
    const csXyz angle(kPennantCloth, 0, 0);   // shape_angle.x: which cloth
    c.flag = fopAcM_create(fpcNm_Obj_Flag2_e, kPennantPole, &at, room, &angle, nullptr, -1);
    c.flagShrunk = false;
}

// The pennant actor has no scale of its own: once it's made, shrink its pole and its cloth (both are
// placed once, when it's created) about the pole's foot.
void shrinkPennant(CampTents& c) {
    if (c.flagShrunk || c.flag == fpcM_ERROR_PROCESS_ID_e) return;
    auto* f = static_cast<daObjFlag2_c*>(fopAcM_SearchByID(c.flag));
    if (f == nullptr || f->mModel == nullptr) return;
    const cXyz k(kPennantScale, kPennantScale, kPennantScale);
    f->mModel->setBaseScale(k);
    mDoMtx_stack_c::transS(f->current.pos.x, f->current.pos.y + 500.0f * kPennantScale, f->current.pos.z);
    mDoMtx_stack_c::scaleM(kPennantScale, kPennantScale, kPennantScale);
    cMtx_copy(mDoMtx_stack_c::get(), f->mFlagCloth.mModelMtx);
    f->mCyl.SetH(200.0f * kPennantScale);
    c.flagShrunk = true;
}

void dropPennant(CampTents& c, bool deleteIt) {
    if (deleteIt && c.flag != fpcM_ERROR_PROCESS_ID_e) {
        if (auto* f = fopAcM_SearchByID(c.flag)) fopAcM_delete(f);
    }
    c.flag = fpcM_ERROR_PROCESS_ID_e;
    c.flagShrunk = false;
}

void updateCamps(int stage, const cXyz& link) {
    for (int i = 0; i < kCampCount; ++i) {
        const Camp& cp = kCamps[i];
        const Node& n = kNodes[cp.node];
        CampTents& c = g_camps[i];
        const bool here = stage == n.stage && dComIfGp_roomControl_checkRoomDisp(n.room);
        const f32 dx = cp.x - link.x, dz = cp.z - link.z;
        const bool near = here && dx * dx + dz * dz < 12000.0f * 12000.0f;
        const bool guards = n.kind == NK_GUARD_CAMP;
        if (guards && c.up) shrinkPennant(c);
        if (guards && !lanayruCleared()) continue;   // the guards' camp is there once Castle Town is lit
        if (kTentsEnabled && near && !c.up && g_spawnCooldown <= 0) {
            c.up = true;
            const s16 face = static_cast<s16>(cp.facing);   // the way you faced when you logged it
            const cXyz mid(cp.x, cp.y, cp.z);
            if (guards) {
                // the fire, and behind it (away from the road) the guards' tent, its door to the fire
                spawnFire(c, cp.node | (soupLevel(n.stage, n.room) << 8), mid, face, n.room);
                cXyz at(mid.x - cM_ssin(face) * kGuardTentBack, mid.y, mid.z - cM_scos(face) * kGuardTentBack);
                const f32 y = probeGround(at.x, at.y, at.z);
                if (y != -G_CM3D_F_INF && std::fabs(y - at.y) < 150.0f) at.y = y;
                spawnTent(c, PSR_TENT_GUARD, at, face, n.room);
                spawnPennant(c, at, face, n.room);
                // two more tents for the bigger watch: either side of the fire, doors turned to it
                for (int side = -1; side <= 1; side += 2) {
                    const s16 sideFace = static_cast<s16>(face + side * 0x4000);   // pointing out to that side
                    cXyz t2(mid.x + cM_ssin(sideFace) * kGuardTentSide, mid.y, mid.z + cM_scos(sideFace) * kGuardTentSide);
                    const f32 y2 = probeGround(t2.x, t2.y, t2.z);
                    if (y2 == -G_CM3D_F_INF || std::fabs(y2 - t2.y) > 150.0f) continue;   // no good ground there
                    t2.y = y2;
                    spawnTent(c, PSR_TENT_GUARD, t2, static_cast<s16>(sideFace + 0x8000), n.room);
                }
            } else if (n.kind == NK_BEEDLE) {
                TentSpot b{mid, face, 0.0f, 0.0f};
                findBeedleSpot(mid, face, &b);
                spawnTent(c, PSR_TENT_BEEDLE, b.pos, b.yaw, n.room);
            } else {
                spawnFire(c, cp.node | (soupLevel(n.stage, n.room) << 8), mid, face, n.room);
                // A few small A-tents (no big tent), each on the flattest open ground left around
                // the fire, doors toward it, with a walkway between them.
                std::vector<TentSpot> placed;
                const HandTents* hand = handTents(cp.node);
                for (int i = 0; hand != nullptr && i < hand->count; i++) {   // hand-placed spots for this camp
                    const HandTent& h = hand->tent[i];
                    f32 y = probeGround(h.x, h.y, h.z);
                    if (y == -G_CM3D_F_INF || std::fabs(y - h.y) > 150.0f) y = h.y;
                    const cXyz at(h.x, y, h.z);
                    spawnTent(c, PSR_TENT_A, at, static_cast<s16>(h.yaw), n.room);
                    if (c.tents < 4) { c.tentPos[c.tents] = at; c.tentYaw[c.tents] = static_cast<s16>(h.yaw); ++c.tents; }
                }
                for (int i = 0; hand == nullptr && i < kCampTents; i++) {
                    TentSpot t{};
                    if (!findTentSpot(cp.node, mid, face, PSR_TENT_A, placed, &t)) break;
                    placed.push_back(t);
                    spawnTent(c, PSR_TENT_A, t.pos, t.yaw, n.room);
                    if (c.tents < 4) { c.tentPos[c.tents] = t.pos; c.tentYaw[c.tents] = t.yaw; ++c.tents; }
                }
            }
        } else if (!here && c.up) {
            c.ids.clear();          // the room unloaded and took the tents with it
            dropPennant(c, false);
            c.up = false;
            c.tents = 0;
        } else if (here && c.up && dx * dx + dz * dz > 16000.0f * 16000.0f) {
            for (ActorId id : c.ids) svc_actor->delete_actor(mod_ctx, id);
            c.ids.clear();
            dropPennant(c, true);
            c.up = false;
            c.tents = 0;
        }
    }
}

// Escort: the game switches the field to its escort layers on this temp bit, and Telma's
// wagon (NPC_COACH) is out. Once the escort's closing cutscene has played (M_035) it's over for
// good, whatever the temp bit says. The wagon search runs once a frame, here, not in the hooks.
bool g_escort = false;

bool computeEscort() {
    /* dSv_event_flag_c::M_035 - Cutscene - [cutscene: 35] after carriage guarding event */
    if (dComIfGs_isEventBit(dSv_event_flag_c::saveBitLabels[68])) return false;
    if (dComIfGs_isTmpBit(0x0601)) return true;
    return fopAcM_SearchByName(fpcNm_NPC_COACH_e) != nullptr;
}

}  // namespace

bool escortRunning() { return g_escort; }
bool eldinCleared() { return dComIfGs_isDarkClearLV(1) == TRUE; }
bool lanayruCleared() { return dComIfGs_isDarkClearLV(2) == TRUE; }

bool eldinBridgeWarpedBack() {
    /* dSv_event_flag_c::M_092 - Hyrule Field - Warped Eldin Bridge */
    return dComIfGs_isEventBit(dSv_event_flag_c::saveBitLabels[125]);
}

bool allTwilightCleared() {
    return dComIfGs_isDarkClearLV(0) == TRUE && eldinCleared() && lanayruCleared();
}

bool eldinBridgeOpen() {
    /* dSv_event_flag_c::M_092 - Hyrule Field - Warped Eldin Bridge */
    if (dComIfGs_isEventBit(dSv_event_flag_c::saveBitLabels[125])) return true;
    /* dSv_event_flag_c::M_050 - Main Event - Eldin Bridge disappears */
    return !dComIfGs_isEventBit(dSv_event_flag_c::saveBitLabels[83]);
}

namespace {

// The bridge is read from the save (see eldinBridgeOpen), so it's right wherever Link is; this
// only logs the change.
void refreshBridge() {
    const bool open = eldinBridgeOpen();
    if (open != g_bridgeRepaired) {
        g_bridgeRepaired = open;
        svc_log->info(mod_ctx, open ? "travellers: the Bridge of Eldin is standing; the escort valley is open"
                                    : "travellers: the Bridge of Eldin is gone; the escort valley is shut");
    }
}

// The field's own switches: live while Link is in it, from its save memory while he isn't.
[[maybe_unused]] bool fieldSwitch(int no) {
    const char* st = dComIfGp_getStartStageName();
    if (st != nullptr && (std::strcmp(st, "F_SP121") == 0 || std::strcmp(st, "F_SP122") == 0)) {
        return dComIfGs_isSwitch(no, -1);
    }
    return dComIfGs_isSaveSwitch(kFieldSaveTable, no);
}

// Which rooms and blockers the story has opened (read from the save, so right wherever Link is).
void refreshStory() {
    u32 rooms = 0;
    if (dComIfGs_isDarkClearLV(0) == TRUE) rooms |= roomBit(0, 1) | roomBit(0, 6) | roomBit(0, 15);
    if (eldinCleared()) {
        for (int r : {0, 2, 3, 4, 5, 7}) rooms |= roomBit(0, r);
    }
    if (lanayruCleared()) {
        for (int r = 9; r <= 14; ++r) rooms |= roomBit(0, r);
        rooms |= roomBit(1, 0);
    }
    u8 blocks = 0;
    blocks |= EB_GORGE_ROCKS;   // never made now (see kBlockers); was: fieldSwitch(kGorgeRocksSwitch)
    /* dSv_event_flag_c::M_035 - Cutscene - after carriage guarding event (the rider gates unlock) */
    if (dComIfGs_isEventBit(dSv_event_flag_c::saveBitLabels[68])) blocks |= EB_RIDER_GATE;
    if (rooms != g_openRooms || blocks != g_openBlocks) {
        diag("story   open rooms %05x (was %05x) blockers gone %x (was %x)", rooms, g_openRooms, blocks, g_openBlocks);
        const bool firstOpen = g_openRooms == 0 && rooms != 0 && g_started;
        g_openRooms = rooms;
        g_openBlocks = blocks;
        // the field has just opened (a save loaded after everyone was placed with it shut): those parked
        // resting get going soon, as at the start of a day, not "tomorrow morning"
        if (firstOpen) {
            for (int s = 0; s < kPopulation && s < kSlots; ++s) {
                Traveller& t = g_t[s];
                if (!t.used || t.spawned || t.guard || t.crew || t.dead || t.state != ST_REST) continue;
                t.restUntilMorning = false;
                t.timer = rndRange(30, 1800);
            }
        }
    }
}

// Somewhere open to be: a road spot or a camp in an open room with an open road out of it.
int randomOpenNode(bool beedle = false) {
    PlanAs as(beedle);
    for (int tries = 0; tries < 40; ++tries) {
        const int n = static_cast<int>(rnd() % kNodeCount);
        if (kNodes[n].kind != NK_ROAD && kNodes[n].kind != NK_CAMP) continue;
        if (!nodeOpen(n) || (beedle && !beedleNodeOk(n))) continue;
        for (const Adj& a : g_adj[n]) {
            if (edgeAllowed(a.edge, false)) return n;
        }
    }
    return -1;
}

// A traveller out of sight in a part of the field that isn't open (from before the save was read,
// or the bridge went): it moves to somewhere open, as if it had come from elsewhere.
void rehomeStranded() {
    for (int s = 0; s < kPopulation && s < kSlots; ++s) {
        Traveller& t = g_t[s];
        if (!t.used || t.spawned || t.guard || t.crew || t.dead || t.homeCamp >= 0) continue;
        PlanAs as(t.beedle);
        bool stuck = !nodeOpen(t.node) || (t.beedle && !beedleNodeOk(t.node));
        if (!stuck && t.state == ST_WALK && t.leg < static_cast<int>(t.route.size())) {
            stuck = !edgeAllowed(t.route[t.leg].edge, false);
        }
        if (!stuck) continue;
        const int n = randomOpenNode(t.beedle);
        if (n < 0) return;   // nothing open yet (the start of the game): they wait
        releaseSeat(t);
        t.route.clear();
        newRoute(t, n);
    }
}

void populate() {
    // start them spread over the network: some on the roads, some at camps
    // the first few are the bards, one to each fire; everyone else from the rest of the cast
    std::vector<int> fires;
    for (int c = 0; c < kCampCount; ++c) if (kNodes[kCamps[c].node].kind == NK_CAMP) fires.push_back(kCamps[c].node);
    for (int s = 0; s < kPopulation && s < kSlots; ++s) {
        Traveller& t = g_t[s];
        t = Traveller{};
        t.used = true;
        if (s == kBeedleSlot) {
            t.type = kBeedleType;
            t.beedle = true;
        } else if (s < static_cast<int>(fires.size())) {
            t.type = kBardType;
            t.homeCamp = fires[s];
        } else {
            t.type = kWalkCast[s % kWalkCast.size()];
        }
        int start = randomOpenNode(t.beedle);   // where the story has opened the field
        for (int tries = 0; start < 0 && tries < 20; ++tries) {
            const int n = static_cast<int>(rnd() % kNodeCount);
            if (kNodes[n].kind == NK_ROAD || kNodes[n].kind == NK_CAMP) start = n;
        }
        if (start < 0) start = 0;
        // keep everyone out of the escort-only valley
        const int camp0 = nearestCamp(t, start, false);
        bool reachable = camp0 >= 0 && !plan(start, camp0, false).empty();
        if (!reachable && !t.beedle) start = kCamps[0].node;
        newRoute(t, start);
        if (t.state == ST_WALK && !t.route.empty()) {
            t.dist = static_cast<f32>(rnd() % 1000) / 1000.0f * edgeLength(t.route[0].edge);
        }
    }
    // the guards, at their own fire for good
    const int guards = guardCampNode();
    for (int k = 0; guards >= 0 && k < static_cast<int>(kGuardCast.size()) && kPopulation + k < kSlots; ++k) {
        Traveller& t = g_t[kPopulation + k];
        t = Traveller{};
        t.used = true;
        t.guard = true;
        t.type = kGuardCast[k];
        t.style = kGuardStyle[k];
        t.node = guards;
        t.state = ST_REST;
        t.restUntilMorning = true;
        takeSeat(t, guards);
    }
}

}  // namespace

// --- public -----------------------------------------------------------------------------------
void initialize() {
    buildNetwork();
    char line[160];
    std::snprintf(line, sizeof(line), "travellers: network %d spots, %d roads, %d camps", kNodeCount, kEdgeCount, kCampCount);
    svc_log->info(mod_ctx, line);
}

bool inNetworkStage() { return currentStage() >= 0; }

void noteEnemyRedirect() { ++g_redirects; }

void noteAct(fopAc_ac_c* passer, const char* what) {
    const int slot = slotOf(passer);
    if (slot >= 0) g_t[slot].lastAct = what;
}

bool isTraveller(const fopAc_ac_c* actor) { return slotOf(actor) >= 0; }
bool isGuard(const fopAc_ac_c* actor) {
    const int s = slotOf(actor);
    return s >= 0 && g_t[s].guard;
}
int slotIndex(const fopAc_ac_c* actor) { return slotOf(actor); }
bool isBeedle(const fopAc_ac_c* actor) {
    const int s = slotOf(actor);
    return s >= 0 && g_t[s].beedle;
}

PathStore g_cookPath;

void setCookPath(const cXyz& from, const cXyz& to) { fillPath(g_cookPath, {from, to}); }

dPath* pathFor(int pathId) {
    if (pathId == kCookPathId) return &g_cookPath.path;
    if (pathId >= kWalkPathBase && pathId < kWalkPathBase + kSlots) return &g_walk[pathId - kWalkPathBase].path;
    if (pathId >= kFleePathBase && pathId < kFleePathBase + kSlots) return &g_flee[pathId - kFleePathBase].path;
    return nullptr;
}

// Sets a traveller's path through the game's own path lookup, which answers only while this is open.
void setOwnPath(daNpcPasser_c* passer, int pathId) {
    g_pathLookupOpen = true;
    passer->m_path.setPath(pathId, fopAcM_GetRoomNo(passer), 1, &passer->current.pos, true);
    g_pathLookupOpen = false;
    passer->m_path.setNextPoint(passer->current.pos);
}

bool onPathEnd(daNpcPasser_c* passer) {
    const int slot = slotOf(passer);
    if (slot < 0) return false;
    Traveller& t = g_t[slot];
    if (t.leaving) return false;   // already on its way out; vanilla deletes it at the path's end
    if (!t.pathMap.empty()) {
        t.leg = t.pathMap.back().first;
        t.dist = t.pathMap.back().second;
    }
    switch (t.pathEnd) {
    case PE_CONTINUE:
        eveningReroute(t);
        buildWalkPath(slot, passer->current.pos);
        setOwnPath(passer, kWalkPathBase + slot);
        return true;
    case PE_VANISH: {
        // Into Castle Town, across a loading line or into a room that isn't loaded: the game deletes
        // it at the end of its path like any vanilla walker, and the roster carries it on from there.
        t.leaving = true;
        diag("leave   slot %d", slot);
        if (t.state == ST_WALK && !t.route.empty()) {
            const int last = static_cast<int>(t.route.size()) - 1;
            if (t.leg >= last && kNodes[legEnd(t.route.back())].kind == NK_GATE) {
                arrive(t, legEnd(t.route.back()));
            } else if (kEdges[t.route[t.leg].edge].flags & EF_HANDOFF) {
                // skip to the far side of the loading screen
                const Leg& l = t.route[t.leg];
                const int split = kEdges[l.edge].split;
                const int idx = l.fwd ? split : kEdges[l.edge].count - split;
                t.dist = legPointDist(l, std::clamp(idx, 0, kEdges[l.edge].count - 1));
            } else if (t.beyond.first >= 0) {
                t.leg = t.beyond.first;
                t.dist = t.beyond.second;
            }
        }
        return false;
    }
    case PE_ARRIVE:
    default:
        if (t.state == ST_WALK && !t.route.empty()) arrive(t, legEnd(t.route.back()));
        // Resting skips vanilla's walking code, and with it the fear check: do it here, so people
        // waiting at a junction run too. Settled campers don't run: fighters defend the fire and
        // the rest cower (passer_prototype's camp guard).
        if (!campInfo(passer, nullptr, nullptr, nullptr) && passer->checkFearSituation()) {
            t.idleSet = false;
            passer->setAction(daNpcPasser_c::MODE_1);   // flee (our initEscape takes it from there)
            return true;
        }
        if (!t.idleSet) {
            setIdle(passer);
            t.idleSet = true;
        }
        passer->speedF = 0.0f;
        return true;
    }
}

bool startFlee(daNpcPasser_c* passer, const cXyz& threat) {
    const int slot = slotOf(passer);
    if (slot < 0) return false;
    g_t[slot].lastAct = "fled";
    buildFleePath(slot, passer->current.pos, threat);
    setOwnPath(passer, kFleePathBase + slot);
    if (auto* run = static_cast<J3DAnmTransformKey*>(passer->getAnmP(4, passer->getObjNum()))) {
        passer->setAnm(run, 1.0f, 12.0f, J3DFrameCtrl::EMode_LOOP, 0, -1);
    }
    passer->field_0xb1c = 1;   // running
    return true;
}

void resumeWalk(daNpcPasser_c* passer) {
    const int slot = slotOf(passer);
    if (slot < 0) return;
    Traveller& t = g_t[slot];
    if (t.crew) {   // back from running: stand where it is; its convoy calls it back
        t.fleeMap.clear();
        t.fleeLegs.clear();
        t.route.clear();
        t.state = ST_REST;
        buildWalkPath(slot, passer->current.pos);
        setOwnPath(passer, kWalkPathBase + slot);
        passer->setAction(daNpcPasser_c::MODE_0);
        return;
    }
    // After running away it is somewhere else on the roads: plan a fresh trip from there.
    if (!t.fleeMap.empty() && !t.fleeLegs.empty() && passer->m_path.isPath() &&
        passer->m_path.mPath == &g_flee[slot].path) {
        const int pi = std::clamp(static_cast<int>(passer->m_path.field_0x20) - 1, 0,
                                  static_cast<int>(t.fleeMap.size()) - 1);
        const auto [li, d] = t.fleeMap[pi];
        const Leg leg = t.fleeLegs[li];
        const int end = legEnd(leg);
        const int to = isEvening() ? nightDestination(t, end) : randomDestination(t, end);
        std::vector<Leg> route{leg};
        if (to >= 0 && to != end) {
            const auto rest = plan(end, to, false);
            route.insert(route.end(), rest.begin(), rest.end());
        }
        const int dest = legEnd(route.back());
        if (kNodes[dest].kind == NK_CAMP) {
            if (t.campNode != dest) { releaseSeat(t); takeSeat(t, dest); }
        } else {
            releaseSeat(t);
        }
        t.route = route;
        t.leg = 0;
        t.dist = d;
        t.node = legStart(leg);
        t.state = ST_WALK;
        t.restUntilMorning = false;
        diag("replan  slot %d after fleeing: heading for node %d", slot, dest);
    }
    t.fleeMap.clear();
    t.fleeLegs.clear();
    t.idleSet = false;
    t.lastAct = "walk";
    buildWalkPath(slot, passer->current.pos);
    setOwnPath(passer, kWalkPathBase + slot);
    passer->setAction(daNpcPasser_c::MODE_0);
}

void onDeath(daNpcPasser_c* passer) {
    const int slot = slotOf(passer);
    if (slot < 0) return;
    Traveller& t = g_t[slot];
    diag("death   slot %d", slot);
    t.dead = true;
    releaseSeat(t);
    t.spawned = false;
    t.actor = fpcM_ERROR_PROCESS_ID_e;
    t.state = ST_GONE;
    t.route.clear();
    t.timer = rndRange(9000, 18000);   // someone new comes along later
}

void noteBlow(const fopAc_ac_c* passer, const char* what) {
    const int slot = slotOf(passer);
    if (slot >= 0) diag("blow    slot %d %s", slot, what);
}

void noteStuck(const fopAc_ac_c* passer, const char* what) {
    const int slot = slotOf(passer);
    if (slot >= 0) diag("stuck   slot %d %s", slot, what);
}

void noteHit(const fopAc_ac_c* passer, int damage, int lifeLeft) {
    const int slot = slotOf(passer);
    if (slot >= 0) diag("hit     slot %d -%d quarters, %d left", slot, damage, lifeLeft);
}

// Called as the game tears a traveller's actor down: records what it was doing, so a camper
// that vanishes can be traced to the code that deleted it.
void noteDelete(const daNpcPasser_c* p) {
    const int slot = slotOf(p);
    if (slot < 0) return;
    const Traveller& t = g_t[slot];
    const auto* link = dComIfGp_getPlayer(0);
    const f32 dist = link != nullptr ? const_cast<cXyz&>(p->current.pos).abs(link->current.pos) : -1.0f;
    auto& path = const_cast<daNpcPasser_c*>(p)->m_path;
    const int last = path.isPath() && path.mPath != nullptr ? path.mPath->m_num - 1 : -1;
    diag("delete  slot %d st %d act %d pmode %d sub %d pt %d/%d nodraw %d leaving %d y %.0f home %.0f link %.0f",
         slot, static_cast<int>(t.state), static_cast<int>(p->mActionIdx), static_cast<int>(p->mPathMode),
         static_cast<int>(p->field_0xb1c), static_cast<int>(path.field_0x20), last,
         fopAcM_CheckCondition(const_cast<daNpcPasser_c*>(p), fopAcCnd_NODRAW_e) ? 1 : 0,
         t.leaving ? 1 : 0, p->current.pos.y, p->home.pos.y, dist);
}

void noteHunt(const fopAc_ac_c* enemy, const fopAc_ac_c* prey, bool start) {
    const int slot = slotOf(prey);
    diag("hunt    enemy %u (%s) %s slot %d", static_cast<unsigned>(fopAcM_GetID(const_cast<fopAc_ac_c*>(enemy))),
         fopAcM_getProcNameString(const_cast<fopAc_ac_c*>(enemy)), start ? "after" : "gave up on", slot);
}

void noteStand(const fopAc_ac_c* passer, const fopAc_ac_c* enemy) {
    const int slot = slotOf(passer);
    if (slot >= 0) diag("stand   slot %d faces enemy %u (%s)", slot,
                        static_cast<unsigned>(fopAcM_GetID(const_cast<fopAc_ac_c*>(enemy))),
                        fopAcM_getProcNameString(const_cast<fopAc_ac_c*>(enemy)));
}

int campSeatCount(int campNode) { return campSeats(campNode); }

bool campSeatSpot(int campNode, int seat, cXyz* pos, s16* yaw) { return seatSpot(campNode, seat, pos, yaw); }

bool campSeatInUse(int campNode, int seat) {
    for (int i = 0; i < kSlots; ++i) {
        const Traveller& t = g_t[i];
        if (t.used && t.spawned && t.state == ST_REST && t.campNode == campNode && t.node == campNode &&
            t.seat == seat && traveller_gear::sitting(i)) return true;
    }
    return false;
}

bool campInfo(const fopAc_ac_c* passer, cXyz* center, cXyz* seat, s16* seatYaw) {
    const int slot = slotOf(passer);
    if (slot < 0) return false;
    const Traveller& t = g_t[slot];
    if (t.state != ST_REST || t.seat < 0 || t.campNode < 0 || t.campNode != t.node) return false;
    if (center) center->set(kNodes[t.node].x, kNodes[t.node].y, kNodes[t.node].z);
    const Where w = restWhere(t, slot);
    if (seat) *seat = w.pos;
    if (seatYaw) *seatYaw = w.yaw;
    return true;
}

void refreshIdle(const fopAc_ac_c* passer) {
    const int slot = slotOf(passer);
    if (slot >= 0) g_t[slot].idleSet = false;
}

void noteCamp(const fopAc_ac_c* passer, const char* what, const fopAc_ac_c* enemy) {
    const int slot = slotOf(passer);
    if (slot < 0) return;
    diag("camp    slot %d %s %s", slot, what,
         enemy ? fopAcM_getProcNameString(const_cast<fopAc_ac_c*>(enemy)) : "");
}

int life(const fopAc_ac_c* passer) {
    const int slot = slotOf(passer);
    return slot < 0 ? -1 : lifeOf(g_t[slot]);
}

int maxLife(const fopAc_ac_c* passer) {
    const int slot = slotOf(passer);
    return slot < 0 ? -1 : fullLife(g_t[slot].type);
}

void setLife(const fopAc_ac_c* passer, int quarters) {
    const int slot = slotOf(passer);
    if (slot < 0) return;
    Traveller& t = g_t[slot];
    t.life = std::max(0, std::min(quarters, fullLife(t.type)));
    t.healTimer = 0;
}

bool isFighter(const fopAc_ac_c* passer) {
    const int slot = slotOf(passer);
    // Beedle wears the men's skeleton (type 0, which the guards fight in): he never fights, he runs from
    // everything (wolf Link too), and carries a lantern, not a fighter's torch
    return slot >= 0 && !g_t[slot].beedle && fighterType(g_t[slot].type);
}

daNpcPasser_c* nearestActor(const cXyz& pos, f32 maxDist) {
    daNpcPasser_c* best = nullptr;
    f32 bestSq = maxDist * maxDist;
    for (auto& t : g_t) {
        if (!t.used || !t.spawned || !t.registered) continue;
        fopAc_ac_c* found = fopAcM_SearchByID(t.actor);
        if (found == nullptr || fopAcM_GetName(found) != fpcNm_NPC_PASSER_e) continue;
        auto* a = static_cast<daNpcPasser_c*>(found);
        const f32 dx = a->current.pos.x - pos.x, dz = a->current.pos.z - pos.z;
        if (dx * dx + dz * dz < bestSq) {
            bestSq = dx * dx + dz * dz;
            best = a;
        }
    }
    return best;
}

void update() {
    ++g_frame;
    // open the diagnostic log, retrying every ~2 s until it works; say so in Dusklight's log either way
    static int s_diagTries = 0;
    if (g_diag == nullptr && (g_frame == 1 || g_frame % 60 == 0) && s_diagTries < 30) {
        ++s_diagTries;
        g_diag = spot_picker::openDataFile("travellers_diag.log", "w");
        if (g_diag != nullptr) {
            svc_log->info(mod_ctx, "travellers: diagnostic log open (mod_data/travellers_diag.log)");
            diag("diag log open, attempt %d", s_diagTries);
        } else if (s_diagTries == 1 || s_diagTries == 30) {
            svc_log->warn(mod_ctx, "travellers: could not open mod_data/travellers_diag.log");
        }
    }
    if (!g_started) {
        g_rng ^= static_cast<u32>(dKy_getdaytime_minute() * 7919 + hour() * 104729);
        refreshBridge();
        refreshStory();
        populate();
        g_started = true;
    }
    // the story moves on (or a save is loaded): open up the field. (This sat inside the block above,
    // which runs once before any save is loaded: the field stayed shut and nobody ever set off.)
    if ((g_frame % 60) == 0) {
        refreshStory();
        rehomeStranded();
    }
    const int stage = currentStage();
    auto* player = dComIfGp_getPlayer(0);
    // No travellers in a province still covered by twilight (they keep travelling virtually).
    // ...nor while Telma's escort runs: the field is the game's own then.
    g_escort = computeEscort();
    refreshBridge();
    const bool inWorld = stage >= 0 && player != nullptr && !dKy_darkworld_check() && !g_escort;
    const cXyz link = inWorld ? player->current.pos : cXyz(0.0f, 0.0f, 0.0f);
    if (const int worldNow = inWorld ? stage : -2; worldNow != g_lastWorldLogged) {
        // only when it changes (this used to write a line every frame while out of the world)
        g_lastWorldLogged = worldNow;
        // a stage change took every tent and fire with it (e.g. into Beedle's shop and back out):
        // put them up again when Link is back
        for (CampTents& c : g_camps) {
            c.ids.clear(); c.up = false; c.tents = 0;
            c.flag = fpcM_ERROR_PROCESS_ID_e; c.flagShrunk = false;
        }
        const char* sn = dComIfGp_getStartStageName();
        diag("world   %s stage %s (net %d) room %d darkworld %d", inWorld ? "IN" : "OUT", sn ? sn : "?", stage,
             dComIfGp_roomControl_getStayNo(), static_cast<int>(dKy_darkworld_check()));
    }
    g_lastStage = inWorld ? stage : -2;
    if (g_spawnCooldown > 0) --g_spawnCooldown;
    const bool settled = inWorld && !dComIfGp_event_runCheck();
    if (settled) {
        updateCamps(stage, link);
    }
    const int maxLive = kMaxReal;
    int live = 0;
    for (const Traveller& t : g_t) if (t.used && t.spawned) ++live;
    sweepOrphans();
    const FrameStats* fs = frameStats();
    const u32 idx = fs != nullptr ? fs->lastIndexSize : 0;
    const u32 vtx = fs != nullptr ? fs->lastVertSize : 0;
    const bool high = idx > kIndexBufferSize * 3 / 4 || vtx > kVertexBufferSize * 3 / 4;
    static int s_highLines = 0;
    if (g_frame % 30 == 0) {
        int virt = 0, rest = 0, town = 0, gone = 0;
        for (const Traveller& t : g_t) {
            if (!t.used) continue;
            if (t.state == ST_REST) ++rest; else if (t.state == ST_TOWN) ++town; else if (t.state == ST_GONE) ++gone; else ++virt;
        }
        diag("status  live %d/%d settled %d event %d room %d link (%.0f %.0f %.0f) walk %d rest %d town %d gone %d",
             live, maxLive, static_cast<int>(settled), static_cast<int>(dComIfGp_event_runCheck() != 0),
             dComIfGp_roomControl_getStayNo(), link.x, link.y, link.z, virt, rest, town, gone);
        const WorldCount wc = countWorld(false);
        diag("frame   idx %u KB / 2048 vtx %u KB / 5120 draws %u | actors %d enemies %d passers %d (ours %d, drawn %d) redirects %d",
             idx / 1024, vtx / 1024, fs != nullptr ? fs->drawCallCount : 0, wc.actors, wc.enemies, wc.passers, wc.ours,
             wc.passersDrawn, g_redirects);
        g_redirects = 0;
    }
    // Buffer getting full: write it down every few frames, and once a second list which actor types
    // are most common, so the last lines before a crash show what was filling it.
    if (high && g_frame % 5 == 0 && s_highLines < 2000) {
        ++s_highLines;
        const bool breakdown = g_frame % 30 == 0;
        const WorldCount wc = countWorld(breakdown);
        diag("HIGH    idx %u KB vtx %u KB draws %u | actors %d enemies %d passers %d (ours %d, drawn %d) live %d",
             idx / 1024, vtx / 1024, fs->drawCallCount, wc.actors, wc.enemies, wc.passers, wc.ours, wc.passersDrawn, live);
        if (breakdown) {
            auto names = wc.names;
            std::sort(names.begin(), names.begin() + wc.nameCount,
                      [](const auto& a, const auto& b) { return a.second > b.second; });
            char line[256];
            int len = std::snprintf(line, sizeof(line), "HIGH    top actor types (profile name: count):");
            for (int i = 0; i < std::min(wc.nameCount, 10) && len < static_cast<int>(sizeof(line)) - 16; ++i) {
                len += std::snprintf(line + len, sizeof(line) - len, " %d:%d", names[i].first, names[i].second);
            }
            diag("%s", line);
        }
    }

    for (int s = 0; s < kSlots; ++s) {
        Traveller& t = g_t[s];
        if (!t.used) continue;
        if (t.crew) {   // the convoys look after their own; just notice when the game took one away
            if (t.spawned && fopAcM_SearchByID(t.actor) == nullptr && !fpcM_IsCreating(t.actor)) {
                t.spawned = false;
                t.registered = false;
                t.actor = fpcM_ERROR_PROCESS_ID_e;
            }
            continue;
        }
        bedTick(t);

        if (t.spawned) {
            auto* actor = static_cast<daNpcPasser_c*>(fopAcM_SearchByID(t.actor));
            if (actor != nullptr) {
                t.registered = true;
            } else if (t.registered) {
                // Gone: deleted by the game at an exit or off screen, or its room unloaded. It carries
                // on virtually from where the roster last had it.
                diag("gone    slot %d (%s)", s, t.leaving ? "left" : t.lastAct);
                if (!t.leaving) {
                    // The game deleted it (scared or stuck off screen). Give it time to get further
                    // down the road before its spawner may bring it back, so it doesn't reappear in
                    // the same spot and get deleted again.
                    // a camper goes back to its seat soon (out of sight); walkers trickle back later
                    t.cooldown = t.state == ST_REST ? rndRange(kCampBackMin, kCampBackMax)
                                                    : rndRange(kGoneCooldownMin, kGoneCooldownMax);
                    for (int k = 0; k < static_cast<int>(kGoneSkip / kVirtualSpeed) && t.state == ST_WALK; ++k) {
                        stepVirtual(t);
                    }
                }
                t.lastAct = "walk";
                t.spawned = false;
                t.registered = false;
                t.leaving = false;
                t.actor = fpcM_ERROR_PROCESS_ID_e;
                continue;
            } else if (fpcM_IsCreating(t.actor)) {
                // Still being created (loading its files): keep waiting.
                if (++t.createFrames == kCreateTimeout * 4) {
                    diag("slow    slot %d still being created after %d frames", s, t.createFrames);
                }
                if (!inWorld) {
                    diag("orphan  slot %d left while still being created (proc %u)", s, static_cast<unsigned>(t.actor));
                    addOrphan(t.actor);
                    t.spawned = false;
                    t.actor = fpcM_ERROR_PROCESS_ID_e;
                }
                continue;
            } else if (++t.createFrames > kCreateTimeout) {
                diag("failed  slot %d create never appeared", s);
                addOrphan(t.actor);
                t.spawned = false;
                t.actor = fpcM_ERROR_PROCESS_ID_e;
                continue;
            }
            if (actor == nullptr) continue;
            if (!t.leaving) syncFromActor(t, actor);
            // rest is over while you watch: get up and go
            if (!t.leaving && (t.state == ST_REST || t.state == ST_TOWN)) {
                stepRestOrTown(t);
                if (t.state == ST_WALK) resumeWalk(actor);
            }
            continue;
        }

        // virtual: the roster moves it along; its room's spawner brings it in when that room is loaded
        if (t.cooldown > 0) --t.cooldown;
        heal(t);
        if (t.state == ST_WALK && isDeepNight() && !t.route.empty()) {
            // Properly dark and nobody can see it: it is at its camp seat (or in town) for the night.
            const int night = nightDestination(t, legEnd(t.route[t.leg]));
            if (night >= 0) {
                t.route.clear();
                t.leg = 0;
                t.dist = 0.0f;
                arrive(t, night);
                if (t.state == ST_REST && t.campNode != night && t.bedCamp != night) {   // that fire was full after all
                    t.state = ST_TOWN;
                    t.timer = rndRange(300, 900);
                }
            }
        }
        if (t.state == ST_WALK) {
            stepVirtual(t);
        } else {
            stepRestOrTown(t);
        }
    }
}

// --- room spawners ------------------------------------------------------------------------------
bool spawnerSpot(int i, SpawnerSpot* out) {
    // every (stage, room) the road points pass through, in order of first appearance
    static std::vector<SpawnerSpot> s_spots;
    if (s_spots.empty()) {
        constexpr int kPointCount = sizeof(kPoints) / sizeof(kPoints[0]);
        for (int k = 0; k < kPointCount; ++k) {
            const Point& p = kPoints[k];
            if (p.room < 0) continue;
            bool seen = false;
            for (const SpawnerSpot& sp : s_spots) {
                if (sp.room == p.room && std::strcmp(sp.stage, kStages[p.stage]) == 0) { seen = true; break; }
            }
            if (!seen) s_spots.push_back({kStages[p.stage], p.room, p.x, p.y, p.z});
        }
    }
    if (i < 0 || i >= static_cast<int>(s_spots.size())) return false;
    *out = s_spots[i];
    return true;
}

// Is passer 8 (the bow-and-sword fighter) already out in this room?
bool bowmanIn(int room) {
    for (const Traveller& o : g_t) {
        if (!o.used || !o.spawned || o.type != kBowman) continue;
        if (auto* a = fopAcM_SearchByID(o.actor); a != nullptr && fopAcM_GetRoomNo(a) == room) return true;
    }
    return false;
}

bool spawnerTick(fopAc_ac_c* spawner, bool firstTick) {
    const int stage = currentStage();
    auto* player = dComIfGp_getPlayer(0);
    if (stage < 0 || player == nullptr || dKy_darkworld_check() || dComIfGp_event_runCheck() || g_escort) {
        return false;   // no travellers in twilight; nothing appears during events or Telma's escort
    }
    const int room = fopAcM_GetRoomNo(spawner);
    static std::array<int, 64> s_lastArrival{};   // per room: frame of its last late arrival
    if (!firstTick) {
        if ((g_frame + room) % 10 != 0) return true;              // a few checks a second is plenty
        if (room >= 0 && room < 64 && g_frame - s_lastArrival[room] < kArrivalGap) return true;
    }
    int live = 0;
    for (const Traveller& t : g_t) if (t.used && t.spawned) ++live;
    const cXyz& link = player->current.pos;
    const s16 facing = player->shape_angle.y;
    int made = 0;
    for (int s = 0; s < kSlots; ++s) {
        Traveller& t = g_t[s];
        if (!t.used || t.spawned || t.dead || t.cooldown > 0) continue;
        if (t.sleep == 1 || t.sleep == 2) continue;   // in its tent
        if (t.crew) continue;                          // convoy people come and go with their convoy
        if (t.guard && !lanayruCleared()) continue;   // the guards come out once Castle Town is lit
        Where w{};
        if (t.state == ST_WALK && !t.route.empty()) {
            if (isDeepNight()) continue;   // roads are empty at night: only campers appear
            w = locate(t.route[t.leg], t.dist);
        } else if (t.state == ST_REST) {
            w = restWhere(t, s);
        } else {
            continue;   // in Castle Town or gone
        }
        if (w.stage != stage || w.room != room) continue;
        if (live >= kMaxReal) break;
        if (t.type == kBowman && bowmanIn(room)) continue;   // bow and sword: one per room
        if (!firstTick) {
            // a late arrival (walked in from somewhere not loaded): never right next to Link, and
            // not in front of him until further out
            const f32 dx = w.pos.x - link.x, dz = w.pos.z - link.z;
            const f32 d2 = dx * dx + dz * dz;
            const s16 off = static_cast<s16>(cM_atan2s(dx, dz) - facing);
            // Someone back at their camp seat may sit down again closer in, as long as it happens
            // behind Link's back (the camera looks over his shoulder): camps don't stay empty.
            const bool campSeat = t.state == ST_REST && t.seat >= 0;
            const bool behind = std::abs(static_cast<int>(off)) > 0x6000;
            if (campSeat && behind && d2 >= kCampBackRadius * kCampBackRadius) {
                // fine: out of sight
            } else if (d2 < kNoArrivalRadius * kNoArrivalRadius) {
                continue;
            }
            const bool inFront = std::abs(static_cast<int>(off)) < 0x3000;
            if (inFront && d2 < (kNoArrivalRadius + kViewGuardRadius) * (kNoArrivalRadius + kViewGuardRadius)) continue;
        }
        spawn(s, w);
        ++live;
        ++made;
        if (!firstTick) {
            if (room >= 0 && room < 64) s_lastArrival[room] = g_frame;
            break;   // one late arrival at a time
        }
    }
    if (firstTick) diag("scatter room %d: %d travellers", room, made);
    return true;
}

// --- beds, for the actors ---
int bedPhase(const fopAc_ac_c* passer, cXyz* inside, cXyz* doorstep, s16* faceIn) {
    const int slot = slotOf(passer);
    if (slot < 0) return 0;
    const Traveller& t = g_t[slot];
    if (t.sleep != 1 && t.sleep != 3) return 0;
    if (!bedSpot(t, inside, doorstep, faceIn)) return 0;
    return t.sleep;
}
void tuckedIn(fopAc_ac_c* passer) {
    const int slot = slotOf(passer);
    if (slot < 0) return;
    Traveller& t = g_t[slot];
    diag("bed     slot %d crawled into its tent", slot);
    despawn(t, passer);
    t.sleep = 2;
}
void upAndAbout(const fopAc_ac_c* passer) {
    const int slot = slotOf(passer);
    if (slot < 0) return;
    Traveller& t = g_t[slot];
    diag("bed     slot %d up for the day", slot);
    releaseBed(t);
    t.idleSet = false;
}
void tentBeds(const cXyz& tentPos, int* asleep, int* busy) {
    *asleep = *busy = 0;
    for (int i = 0; i < kCampCount; ++i) {
        const CampTents& c = g_camps[i];
        for (int k = 0; k < c.tents; ++k) {
            if (std::abs(c.tentPos[k].x - tentPos.x) > 5.0f || std::abs(c.tentPos[k].z - tentPos.z) > 5.0f) continue;
            for (const Traveller& t : g_t) {
                if (!t.used || t.bedCamp != kCamps[i].node || t.bed / kBedsPerTent != k) continue;
                if (t.sleep == 2) ++*asleep;
                else if (t.sleep == 1 || t.sleep == 3) { ++*busy; if (t.sleep == 1) ++*asleep; }
            }
            return;
        }
    }
}

bool pathLookupOpen() { return g_pathLookupOpen; }
void setPathLookupOpen(bool open) { g_pathLookupOpen = open; }

// The diary, for the other parts of the mod.
void diagLine(const char* fmt, ...) {
    if (g_diag == nullptr) return;
    std::fprintf(g_diag, "%6d ", g_frame);
    va_list ap; va_start(ap, fmt); std::vfprintf(g_diag, fmt, ap); va_end(ap);
    std::fputc('\n', g_diag);
    std::fflush(g_diag);
}

// --- convoy people ------------------------------------------------------------------------------
constexpr int kCrewFirst = kPopulation + static_cast<int>(kGuardCast.size());
void crewDespawn(int slot);

int crewClaim(u8 type, u8 style) {
    for (int s = kCrewFirst; s < kSlots; ++s) {
        Traveller& t = g_t[s];
        if (t.used) continue;
        t = Traveller{};
        t.used = true;
        t.crew = true;
        t.type = type;
        t.style = style;
        t.state = ST_REST;
        return s;
    }
    return -1;
}

void crewRelease(int slot) {
    if (slot < kCrewFirst || slot >= kSlots || !g_t[slot].crew) return;
    crewDespawn(slot);
    g_t[slot] = Traveller{};
}

bool crewSpawn(int slot, const cXyz& pos, s16 yaw, s8 room) {
    if (slot < kCrewFirst || slot >= kSlots || !g_t[slot].crew) return false;
    Traveller& t = g_t[slot];
    if (t.spawned) return true;
    const int stage = currentStage();
    if (stage < 0) return false;
    Where w{};
    w.stage = static_cast<u8>(stage);
    w.room = room;
    w.pos = pos;
    w.yaw = yaw;
    w.nextPoint = 0;
    t.dead = false;
    t.state = ST_REST;
    t.route.clear();
    spawn(slot, w);
    return true;
}

void crewDespawn(int slot) {
    if (slot < kCrewFirst || slot >= kSlots || !g_t[slot].crew) return;
    Traveller& t = g_t[slot];
    if (!t.spawned) return;
    if (fpcM_IsCreating(t.actor)) {   // still loading: it's deleted the moment it's made
        addOrphan(t.actor);
        t.spawned = false;
        t.registered = false;
        t.actor = fpcM_ERROR_PROCESS_ID_e;
        return;
    }
    despawn(t, fopAcM_SearchByID(t.actor));
}

daNpcPasser_c* crewActor(int slot) {
    if (slot < kCrewFirst || slot >= kSlots || !g_t[slot].crew || !g_t[slot].spawned) return nullptr;
    return static_cast<daNpcPasser_c*>(fopAcM_SearchByID(g_t[slot].actor));
}

void crewHold(int slot, bool held) {
    if (slot >= kCrewFirst && slot < kSlots) g_t[slot].held = held;
}

bool crewDead(int slot) { return slot >= kCrewFirst && slot < kSlots && g_t[slot].dead; }

bool crewHeld(const fopAc_ac_c* passer) {
    const int slot = slotOf(passer);
    return slot >= 0 && g_t[slot].crew && g_t[slot].held;
}

u8 guardStyle(const fopAc_ac_c* passer) {
    const int slot = slotOf(passer);
    return slot >= 0 && g_t[slot].used ? g_t[slot].style : 0;
}

bool isCrew(const fopAc_ac_c* passer) {
    const int slot = slotOf(passer);
    return slot >= 0 && g_t[slot].crew;
}

}  // namespace travellers
