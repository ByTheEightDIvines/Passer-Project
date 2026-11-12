#include "global.h"

#include "travellers.hpp"
#include "travel_network_data.inc"
#include "tent_actor.hpp"
#include "spot_picker.hpp"

#include "d/actor/d_a_npc_passer.h"
#include "d/d_bg_s_gnd_chk.h"
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
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

enum NodeKind : u8 { NK_ROAD = 0, NK_CAMP = 1, NK_BEEDLE = 2, NK_GATE = 3, NK_BRIDGE_END = 4, NK_BRIDGE_EAST = 5 };
enum EdgeFlag : u8 { EF_ESCORT = 1, EF_NEEDS_BRIDGE = 2, EF_NO_BEEDLE = 4, EF_HANDOFF = 8, EF_SPUR = 16 };

// --- tuning -----------------------------------------------------------------------------------
constexpr int kPopulation = 30;              // travellers on the network
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
constexpr int kAmphiSeats = 6;               // the amphitheater by Castle Town is a bit bigger
constexpr int kMaxSeats = 10;
constexpr int kGoneCooldownMin = 1800;       // frames (30-90 s) before a traveller the game deleted
constexpr int kGoneCooldownMax = 5400;       // can reappear, different for each so they trickle back
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
constexpr std::array<u8, 8> kFighters{3, 6, 8, 9, 11, 13, 14, 29};
// The passer types from the cast (stock passer type ids).
constexpr std::array<u8, 14> kCast{3, 4, 6, 7, 8, 9, 11, 13, 14, 15, 20, 27, 28, 29};

// --- network ----------------------------------------------------------------------------------
struct Adj {
    int edge;
    int other;
};
std::vector<std::vector<Adj>> g_adj;
std::vector<std::vector<f32>> g_cum;   // per edge: distance from its first point to each point

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
}

bool g_bridgeRepaired = false;

bool edgeAllowed(int e, bool escort) {
    const u8 f = kEdges[e].flags;
    if ((f & EF_ESCORT) && !escort) {
        return false;
    }
    if ((f & EF_NEEDS_BRIDGE) && !g_bridgeRepaired) {
        return false;
    }
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
constexpr bool kTentsEnabled = false;
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

int campSeats(int campNode) { return campNode == amphitheaterNode() ? kAmphiSeats : kCampSeats; }

int seatsTaken(int campNode) {
    int n = 0;
    for (const Traveller& t : g_t) if (t.used && t.campNode == campNode) ++n;
    return n;
}

int fightersAt(int campNode) {
    int n = 0;
    for (const Traveller& t : g_t) if (t.used && t.campNode == campNode && fighterType(t.type)) ++n;
    return n;
}

// Can t sit at this camp: it already has a seat there, or there's a free one (and, for a fighter,
// the fire doesn't already have its two fighters).
bool canSit(const Traveller& t, int campNode) {
    if (t.campNode == campNode) return true;
    if (seatsTaken(campNode) >= campSeats(campNode)) return false;
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

// Nearest camp (by road) that still has a free seat for t; -1 if none can be reached.
int nearestCamp(const Traveller& t, int from, bool escort) {
    int best = -1;
    f32 bestLen = 1e30f;
    for (int c = 0; c < kCampCount; ++c) {
        const int n = kCamps[c].node;
        if (kNodes[n].kind != NK_CAMP) continue;
        if (!canSit(t, n)) continue;
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

int randomDestination(const Traveller& t, int from) {
    std::vector<int> dests;
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
    switch (kNodes[node].kind) {
    case NK_GATE:
        releaseSeat(t);
        t.state = ST_TOWN;                       // into Castle Town for a while (all night at dusk)
        t.timer = rndRange(1800, 5400);
        break;
    case NK_CAMP:
        if (!takeSeat(t, node)) {
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
    if (auto* idle = static_cast<J3DAnmTransformKey*>(p->getAnmP(0, p->getObjNum()))) {
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
        for (int n = 0; n < kNodeCount; ++n) if (kNodes[n].kind == NK_GATE) gates.push_back(n);
        const int g = gates.empty() ? t.node : gates[rnd() % gates.size()];
        newRoute(t, g);
    } else if (t.state == ST_GONE) {
        if (--t.timer <= 0) {
            // a new traveller arrives on the road out of Castle Town
            t.type = kCast[rnd() % kCast.size()];
            t.dead = false;
            t.life = -1;
            t.state = ST_TOWN;
            t.timer = 1;
        }
    }
}

Where restWhere(const Traveller& t, int slot) {
    const Node& n = kNodes[t.node];
    Where w{};
    w.stage = n.stage;
    w.room = n.room;
    // its seat on a ring about 300 units around the camp's middle (5 seats; anyone without a seat,
    // e.g. resting at a road junction, just uses its slot to spread out)
    const int place = t.seat >= 0 ? t.seat : slot;
    const int seats = t.seat >= 0 ? campSeats(t.node) : kCampSeats;
    const f32 ring = seats > kCampSeats ? 420.0f : 300.0f;
    const s16 a = static_cast<s16>(place * (0x10000 / seats) + 0x1555);
    w.pos.set(n.x + cM_ssin(a) * ring, n.y, n.z + cM_scos(a) * ring);
    w.yaw = static_cast<s16>(a + 0x8000);   // facing the middle
    w.nextPoint = 0;
    return w;
}

// --- camps: tents appear when their room is loaded and Link is near -------------------------
struct CampTents {
    std::vector<ActorId> ids;
    bool up = false;
};
std::array<CampTents, kCampCount> g_camps{};

void spawnTent(CampTents& c, u8 kind, const cXyz& at, s16 yaw, s8 room) {
    diag("tent    kind %d room %d", kind, room);
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

void updateCamps(int stage, const cXyz& link) {
    for (int i = 0; i < kCampCount; ++i) {
        const Camp& cp = kCamps[i];
        const Node& n = kNodes[cp.node];
        CampTents& c = g_camps[i];
        const bool here = stage == n.stage && dComIfGp_roomControl_checkRoomDisp(n.room);
        const f32 dx = cp.x - link.x, dz = cp.z - link.z;
        const bool near = here && dx * dx + dz * dz < 12000.0f * 12000.0f;
        if (kTentsEnabled && near && !c.up && g_spawnCooldown <= 0) {
            c.up = true;
            const s16 face = static_cast<s16>(cp.facing);   // the way you faced when you logged it
            const cXyz mid(cp.x, cp.y, cp.z);
            if (n.kind == NK_BEEDLE) {
                spawnTent(c, PSR_TENT_BEEDLE, mid, face, n.room);
            } else {
                // the big wall tent behind the fire, a small A-tent off to one side, doors inward
                const f32 back = std::min(cp.clearRadius - 280.0f, 320.0f);
                const cXyz wall(mid.x - cM_ssin(face) * back, mid.y, mid.z - cM_scos(face) * back);
                const s16 side = static_cast<s16>(face + 0x4000);
                const cXyz small(mid.x + cM_ssin(side) * back, mid.y, mid.z + cM_scos(side) * back);
                spawnTent(c, PSR_TENT_WALL, wall, face, n.room);
                spawnTent(c, PSR_TENT_A, small, static_cast<s16>(side + 0x8000), n.room);
            }
        } else if (!here && c.up) {
            c.ids.clear();          // the room unloaded and took the tents with it
            c.up = false;
        } else if (here && c.up && dx * dx + dz * dz > 16000.0f * 16000.0f) {
            for (ActorId id : c.ids) svc_actor->delete_actor(mod_ctx, id);
            c.ids.clear();
            c.up = false;
        }
    }
}

void probeBridge(int stage) {
    if (g_bridgeRepaired || stage != 0 || !dComIfGp_roomControl_checkRoomDisp(0)) return;
    if ((g_frame % 60) != 0) return;
    dBgS_ObjGndChk gnd;
    cXyz probe(kBridgeProbe[0], kBridgeProbe[1] + 300.0f, kBridgeProbe[2]);
    gnd.SetPos(&probe);
    const f32 h = dComIfG_Bgsp().GroundCross(&gnd);
    if (h != -G_CM3D_F_INF && std::fabs(h - kBridgeProbe[1]) < 150.0f) {
        g_bridgeRepaired = true;
        svc_log->info(mod_ctx, "travellers: the Eldin-side bridge is repaired; the escort valley is open");
    }
}

void populate() {
    // start them spread over the network: some on the roads, some at camps
    for (int s = 0; s < kPopulation && s < kSlots; ++s) {
        Traveller& t = g_t[s];
        t = Traveller{};
        t.used = true;
        t.type = kCast[s % kCast.size()];
        int start = 0;
        for (int tries = 0; tries < 20; ++tries) {
            start = static_cast<int>(rnd() % kNodeCount);
            if (kNodes[start].kind == NK_ROAD || kNodes[start].kind == NK_CAMP) break;
        }
        // keep everyone out of the escort-only valley
        const int camp0 = nearestCamp(t, start, false);
        bool reachable = camp0 >= 0 && !plan(start, camp0, false).empty();
        if (!reachable) start = kCamps[0].node;
        newRoute(t, start);
        if (t.state == ST_WALK && !t.route.empty()) {
            t.dist = static_cast<f32>(rnd() % 1000) / 1000.0f * edgeLength(t.route[0].edge);
        }
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

dPath* pathFor(int pathId) {
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

void noteHit(const fopAc_ac_c* passer, int damage, int lifeLeft) {
    const int slot = slotOf(passer);
    if (slot >= 0) diag("hit     slot %d -%d quarters, %d left", slot, damage, lifeLeft);
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
    return slot >= 0 && fighterType(g_t[slot].type);
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
        populate();
        g_started = true;
    }
    const int stage = currentStage();
    auto* player = dComIfGp_getPlayer(0);
    // No travellers in a province still covered by twilight (they keep travelling virtually).
    const bool inWorld = stage >= 0 && player != nullptr && !dKy_darkworld_check();
    const cXyz link = inWorld ? player->current.pos : cXyz(0.0f, 0.0f, 0.0f);
    if (const int worldNow = inWorld ? stage : -2; worldNow != g_lastWorldLogged) {
        // only when it changes (this used to write a line every frame while out of the world)
        g_lastWorldLogged = worldNow;
        const char* sn = dComIfGp_getStartStageName();
        diag("world   %s stage %s (net %d) room %d darkworld %d", inWorld ? "IN" : "OUT", sn ? sn : "?", stage,
             dComIfGp_roomControl_getStayNo(), static_cast<int>(dKy_darkworld_check()));
    }
    g_lastStage = inWorld ? stage : -2;
    if (g_spawnCooldown > 0) --g_spawnCooldown;
    const bool settled = inWorld && !dComIfGp_event_runCheck();
    if (settled) {
        probeBridge(stage);
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
                    t.cooldown = rndRange(kGoneCooldownMin, kGoneCooldownMax);
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
                if (t.state == ST_REST && t.campNode != night) {   // that fire was full after all
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
    if (stage < 0 || player == nullptr || dKy_darkworld_check() || dComIfGp_event_runCheck()) {
        return false;   // no travellers in twilight; nothing appears during events
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
            if (d2 < kNoArrivalRadius * kNoArrivalRadius) continue;
            const bool inFront = std::abs(static_cast<s16>(cM_atan2s(dx, dz) - facing)) < 0x3000;
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

bool pathLookupOpen() { return g_pathLookupOpen; }
void setPathLookupOpen(bool open) { g_pathLookupOpen = open; }

}  // namespace travellers
