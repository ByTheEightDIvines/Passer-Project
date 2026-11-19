#include "global.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "convoys.hpp"
#include "dialogue_marks.hpp"
#include "travellers.hpp"
#include "wagon_actor.hpp"
#include "guard_grudge.hpp"
#include "fighter_anim.hpp"

#include "SSystem/SComponent/c_math.h"
#include "SSystem/SComponent/c_lib.h"
#include "d/actor/d_a_npc_cd2.h"
#include "d/actor/d_a_npc_passer.h"
#include "d/d_bg_s_gnd_chk.h"
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_manager.h"
#include "f_pc/f_pc_searcher.h"
#include "m_Do/m_Do_mtx.h"
#include "mods/service.hpp"
#include "mods/svc/log.h"

// The convoys
// ---------------------------------------------------------------------------------------------
// Three convoys run once Hyrule is out of the twilight, each on its own road and its own clock
// (game time; they keep going while Link is elsewhere, "on paper"):
//   gorge   travellers, Faron Woods -> Kakariko Gorge -> the east field -> Castle Town's east gate,
//           every 12 game hours, from when all three provinces are lit;
//   west    travellers, Castle Town's west gate -> the west canyon -> Faron Woods, every 12 hours,
//   royal   the guards, Castle Town's east gate -> the Eldin bridge -> the north canyon -> the north
//           field -> the west canyon -> Faron Woods, every 24 hours; these two from when Link has
//           warped the Eldin bridge home (by then every blocker on that road is gone).
// Link comes upon one wherever it has got to: it appears out of sight once he's near and leaves
// again once he's well away, never in view. A convoy walks at its escorts' pace. When monsters come
// at it, it stops and the escorts fight (the passers' own fighting); once it's quiet they fall back
// in and it rolls on.
//
// Before that, two parked wagons nod at what's coming: a traveller convoy stopped on the road below
// each twilight wall that still stands (Faron-Eldin, then Eldin-Lanayru), its people standing by
// the wagon. The same three people at both. Each goes once its wall does.

namespace convoys {
namespace {
#include "convoy_routes.inc"
#include "convoy_gates.inc"

constexpr f32 kSpeed = 4.5f;            // units a frame: the escorts' walk
constexpr f32 kShowRadius = 7500.0f;    // a convoy this near Link is out in the world
constexpr f32 kHideRadius = 10500.0f;   // ...and goes back on paper once he's this far away
constexpr f32 kNoPopRadius = 3200.0f;   // never appears this close to Link
constexpr f32 kFightRadius = 1800.0f;   // a monster this near the wagon stops it
constexpr f32 kCalmRadius = 2600.0f;    // ...and none this near for a while lets it go on
constexpr int kCalmFrames = 150;
constexpr f32 kLeash = 1400.0f;        // an escort this far from its place stops fighting and runs back
constexpr int kGorgeEvery = 12 * 60, kWestEvery = 12 * 60, kRoyalEvery = 24 * 60;   // game minutes
constexpr f32 kSideOut = 230.0f;        // the escorts walk this far out from the wagon's middle
constexpr f32 kSitDrop = 37.0f;         // the bench sit puts the seat this far under the feet

struct Road {
    std::vector<ConvoyPt> p;
    std::vector<u8> stage;        // per point: 0 the field (F_SP121), 1 outside Castle Town (F_SP122)
    int n = 0;
    std::vector<f32> cum;
};
std::array<Road, 3> g_roads{};

enum Kind : u8 { K_GORGE = 0, K_WEST = 1, K_ROYAL = 2, K_NUM };

enum Role : u8 { R_RIDER = 0, R_ESCORT = 1, R_BYSTANDER = 2 };
struct Member {
    int slot = -1;
    u8 role = R_RIDER;
    f32 x = 0.0f, z = 0.0f;       // where it walks or sits, in the wagon's frame (+z forward, from the hitch)
    s16 face = 0;                 // bystanders: which way they stand, from the wagon's way
    u8 anim = 0xFF;
    bool fighting = false;
    int scared = 0;               // frames still off running from one of Link's bombs (drivers never)
    bool archer = false;          // shoots (the bowman, the royal bow): he can deal with a foe up a ledge
    int order = 0;                // escorts: which one (0, 1, ...), for single file behind the wagon
    bool file = false;            // the side spot is over a drop or in a wall: walking behind instead
    int fileAge = 0;
};

struct Convoy {
    bool active = false;
    Kind kind = K_GORGE;
    int road = 0;
    bool reverse = false;
    f32 s = 0.0f;
    std::vector<Member> crew;
    fpc_ProcID wagon = fpcM_ERROR_PROCESS_ID_e;
    bool real = false;
    bool halted = false;
    int calm = 0;
    s32 nextAt = -1;              // game minute the next one sets off
    bool wrecked = false;         // its wagon was burnt out or smashed: the convoy's over
    bool fleeing = false;         // a foe they can't get at (up a ledge) and nobody to shoot it: drive on, fast
    bool abandoned = false;       // every escort's down: the drivers jump off and run, the wagon stands
    f32 horseS = 0.0f;            // the loose horse, running on down the road
};
std::array<Convoy, K_NUM> g_convoys{};

// the parked wagons by the twilight walls
struct Parked {
    cXyz pos;
    s16 yaw;
    s8 room;
};
const Parked kParked[2] = {
    {cXyz(-23903.0f, -7400.0f, 77305.0f), 0, 1},    // below the Faron-Eldin wall (the ravine to it)
    {cXyz(7054.0f, 2300.0f, -62152.0f), 0, 7},       // below the Eldin-Lanayru wall (the north canyon)
};
const cXyz kWall[2] = {cXyz(-24042.0f, 0.0f, 73660.0f), cXyz(3768.0f, 0.0f, -59767.0f)};
struct Nod {
    int which = -1;               // which wall's spot it is out at (-1: none)
    fpc_ProcID wagon = fpcM_ERROR_PROCESS_ID_e;
    std::vector<Member> crew;
} g_nod;

bool g_ready = false;
int g_frame = 0;

// --- small helpers --------------------------------------------------------------------------
s32 gameMinutes() {
    return static_cast<s32>(dComIfGs_getDate()) * 1440 + dKy_getdaytime_hour() * 60 + dKy_getdaytime_minute();
}

u32 g_rng = 0x2545F491u;
u32 rnd() {
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}

f32 groundAt(f32 x, f32 y, f32 z) {
    dBgS_ObjGndChk gnd;
    cXyz probe(x, y + 600.0f, z);
    gnd.SetPos(&probe);
    return dComIfG_Bgsp().GroundCross(&gnd);
}

// which of the convoys' stages Link is in: 0 the field, 1 outside Castle Town's gates, -1 neither
int curStage() {
    const char* s = dComIfGp_getStartStageName();
    if (s == nullptr) return -1;
    if (std::strcmp(s, "F_SP121") == 0) return 0;
    if (std::strcmp(s, "F_SP122") == 0) return 1;
    return -1;
}
bool inField() { return curStage() >= 0; }

// Link in the lit field, outside events and the escort: a convoy may be out in the world
bool worldOpen() {
    return inField() && dComIfGp_getPlayer(0) != nullptr && !dKy_darkworld_check() && !travellers::escortRunning();
}

void buildRoads() {
    // the field roads, carried on outside Castle Town to its gates: the gorge and long roads end
    // over the east bridge at the east gate (the royal convoys run the long one from there), the
    // west road starts at the west gate and crosses the west bridge into the field
    auto add = [](Road& road, const ConvoyPt* pts, size_t n, u8 stage) {
        road.p.insert(road.p.end(), pts, pts + n);
        road.stage.insert(road.stage.end(), n, stage);
    };
#define PSR_N(a) (sizeof(a) / sizeof((a)[0]))
    for (Road& road : g_roads) { road.p.clear(); road.stage.clear(); }
    add(g_roads[0], kGorgeRoad, PSR_N(kGorgeRoad), 0);
    add(g_roads[0], kEastGate, PSR_N(kEastGate), 1);
    add(g_roads[1], kLongRoad, PSR_N(kLongRoad), 0);
    add(g_roads[1], kEastGate, PSR_N(kEastGate), 1);
    add(g_roads[2], kWestGate, PSR_N(kWestGate), 1);
    add(g_roads[2], kWestRoad, PSR_N(kWestRoad), 0);
#undef PSR_N
    for (int r = 0; r < 3; ++r) {
        Road& road = g_roads[r];
        road.n = static_cast<int>(road.p.size());
        road.cum.assign(road.n, 0.0f);
        for (int i = 1; i < road.n; ++i) {
            const f32 dx = road.p[i].x - road.p[i - 1].x, dz = road.p[i].z - road.p[i - 1].z;
            road.cum[i] = road.cum[i - 1] + std::sqrt(dx * dx + dz * dz);
        }
    }
}

f32 roadLength(int r) { return g_roads[r].cum.back(); }

// where along a road, distance d from its start (reverse: from its end), and which way it heads
void roadAt(int r, bool reverse, f32 d, cXyz* pos, s16* yaw, s8* room) {
    const Road& road = g_roads[r];
    const f32 L = road.cum.back();
    f32 t = std::clamp(reverse ? L - d : d, 0.0f, L);
    int i = static_cast<int>(std::upper_bound(road.cum.begin(), road.cum.end(), t) - road.cum.begin()) - 1;
    i = std::clamp(i, 0, road.n - 2);
    const ConvoyPt& a = road.p[i];
    const ConvoyPt& b = road.p[i + 1];
    const f32 seg = std::max(road.cum[i + 1] - road.cum[i], 1.0f);
    const f32 k = std::clamp((t - road.cum[i]) / seg, 0.0f, 1.0f);
    if (pos != nullptr) pos->set(a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k, a.z + (b.z - a.z) * k);
    if (yaw != nullptr) {
        // the heading over the next stretch (a few points on), so the horse turns smoothly
        const int j = std::clamp(reverse ? i - 3 : i + 4, 0, road.n - 1);
        const ConvoyPt& c = road.p[j];
        const cXyz from = pos != nullptr ? *pos : cXyz(a.x, a.y, a.z);
        *yaw = cM_atan2s(c.x - from.x, c.z - from.z);
    }
    if (room != nullptr) *room = (k < 0.5f ? a : b).room;
}
// which stage that spot is in (0 the field, 1 outside Castle Town)
int stageAt(int r, bool reverse, f32 d) {
    const Road& road = g_roads[r];
    const f32 L = road.cum.back();
    const f32 t = std::clamp(reverse ? L - d : d, 0.0f, L);
    int i = static_cast<int>(std::upper_bound(road.cum.begin(), road.cum.end(), t) - road.cum.begin()) - 1;
    return road.stage[std::clamp(i, 0, road.n - 1)];
}

psrWagon_c* wagonOf(fpc_ProcID id) {
    if (id == fpcM_ERROR_PROCESS_ID_e) return nullptr;
    auto* a = fopAcM_SearchByID(id);
    if (a == nullptr || fopAcM_GetName(a) != psrWagon_c::sProcName) return nullptr;
    return static_cast<psrWagon_c*>(a);
}

fpc_ProcID makeWagon(u8 kind, bool driven, const cXyz& at, s16 yaw, s8 room) {
    if (psrWagon_c::sProcName < 0) return fpcM_ERROR_PROCESS_ID_e;
    cXyz pos = at;
    const csXyz angle(0, yaw, 0);
    const u32 params = kind | (driven ? kPsrWagonDriven : 0u);
    return fopAcM_create(psrWagon_c::sProcName, params, &pos, room, &angle, nullptr, -1);
}

// Take a wagon out. One still loading can't be taken yet: false (try again next frame).
bool dropWagon(fpc_ProcID& id) {
    if (id == fpcM_ERROR_PROCESS_ID_e) return true;
    if (fpcM_IsCreating(id)) return false;
    if (auto* a = fopAcM_SearchByID(id)) fopAcM_delete(a);
    id = fpcM_ERROR_PROCESS_ID_e;
    return true;
}

// a monster near this spot (alive, an enemy)
struct NearFoe { cXyz at; f32 r2; bool found; };
void* foeSub(void* p, void* data) {
    if (!fopAcM_IsActor(p)) return nullptr;
    auto* a = static_cast<fopAc_ac_c*>(p);
    if (fopAcM_GetGroup(a) != fopAc_ENEMY_e || a->health <= 0) return nullptr;
    if (fopAcM_GetName(a) == fpcNm_E_WB_e) return nullptr;   // a boar is no foe: its rider is
    auto* n = static_cast<NearFoe*>(data);
    const f32 dx = a->current.pos.x - n->at.x, dz = a->current.pos.z - n->at.z;
    if (dx * dx + dz * dz < n->r2) { n->found = true; return p; }
    return nullptr;
}
bool foeNear(const cXyz& at, f32 r) {
    NearFoe n{at, r * r, false};
    fpcM_Search(foeSub, &n);
    return n.found;
}
fopAc_ac_c* foeAt(const cXyz& at, f32 r) {
    NearFoe n{at, r * r, false};
    return static_cast<fopAc_ac_c*>(fpcM_Search(foeSub, &n));
}

// out of Link's sight: behind him, or far enough off
bool outOfSight(const cXyz& at) {
    auto* link = dComIfGp_getPlayer(0);
    if (link == nullptr) return true;
    const f32 dx = at.x - link->current.pos.x, dz = at.z - link->current.pos.z;
    const f32 d2 = dx * dx + dz * dz;
    if (d2 < kNoPopRadius * kNoPopRadius) return false;
    const s16 off = static_cast<s16>(cM_atan2s(dx, dz) - link->shape_angle.y);
    const bool behind = std::abs(static_cast<int>(off)) > 0x5000;
    return behind || d2 > 6000.0f * 6000.0f;
}

f32 linkDist(const cXyz& at) {
    auto* link = dComIfGp_getPlayer(0);
    if (link == nullptr) return 1e9f;
    const f32 dx = at.x - link->current.pos.x, dz = at.z - link->current.pos.z;
    return std::sqrt(dx * dx + dz * dz);
}

// --- who rides and who walks ----------------------------------------------------------------
constexpr std::array<u8, 3> kRiderCast{7, 15, 28};              // townsfolk who don't fight
constexpr std::array<u8, 7> kEscortCast{3, 6, 8, 9, 11, 13, 14}; // the men fighters (8: the bowman)

void addMember(std::vector<Member>& crew, u8 type, u8 role, f32 x, f32 z, s16 face = 0, u8 style = 0) {
    const int slot = travellers::crewClaim(type, style);
    if (slot < 0) return;
    Member m;
    m.slot = slot;
    m.role = role;
    m.archer = type == 8 || style == travellers::kStyleArcher;
    if (role == R_ESCORT) for (const Member& o : crew) if (o.role == R_ESCORT) ++m.order;
    m.x = x;
    m.z = z;
    m.face = face;
    crew.push_back(m);
}

void releaseCrew(std::vector<Member>& crew) {
    for (Member& m : crew) travellers::crewRelease(m.slot);
    crew.clear();
}

void staffConvoy(Convoy& c) {
    releaseCrew(c.crew);
    if (c.kind == K_ROYAL) {
        // two guards on the bench, three walking: a spearman, a swordsman, a spearman
        using travellers::kStyleSpear; using travellers::kStyleSword; using travellers::kStyleArcher;
        addMember(c.crew, 0, R_RIDER, -30.0f, 0.0f, 0, kStyleSword);
        addMember(c.crew, 1, R_RIDER, 30.0f, 0.0f, 0, kStyleSword);
        addMember(c.crew, 1, R_ESCORT, -kSideOut, -120.0f, 0, kStyleSpear);
        addMember(c.crew, 0, R_ESCORT, kSideOut, -520.0f, 0, kStyleArcher);   // the royal bow
        addMember(c.crew, 0, R_ESCORT, -kSideOut, -760.0f, 0, kStyleSpear);
    } else {
        // two travellers driving, two fighters walking on each side
        addMember(c.crew, kRiderCast[rnd() % kRiderCast.size()], R_RIDER, -30.0f, 0.0f);
        addMember(c.crew, kRiderCast[rnd() % kRiderCast.size()], R_RIDER, 30.0f, 0.0f);
        const f32 zs[2] = {-150.0f, -600.0f};
        for (int side = 0; side < 2; ++side) {
            for (f32 z : zs) {
                addMember(c.crew, kEscortCast[rnd() % kEscortCast.size()], R_ESCORT, side ? kSideOut : -kSideOut, z);
            }
        }
    }
}

// --- out in the world, or back on paper -----------------------------------------------------
// The wagon's frame: origin at the hitch, +z the way the wagon faces.
cXyz wagonFrame(psrWagon_c* w, f32 x, f32 y, f32 z) {
    mDoMtx_stack_c::copy(w->mCoachMtx);
    cXyz local(x, y, z), out;
    mDoMtx_stack_c::multVec(&local, &out);
    return out;
}

// The wagon has placed itself (its first update has run): before that its frame is all zeros and
// the crew would be made at the world's origin, out of sight for good (seen 2026-11-18).
bool wagonPlaced(psrWagon_c* w) {
    if (w == nullptr) return false;
    const f32 axis = w->mCoachMtx[0][0] * w->mCoachMtx[0][0] + w->mCoachMtx[2][0] * w->mCoachMtx[2][0];
    return axis > 0.25f && wagonFrame(w, 0.0f, 0.0f, 0.0f).abs(w->current.pos) < 3000.0f;
}

// Where an escort walks: at the wagon's side, unless that's off the side of a bridge, over a drop or
// in a wall (the ground there isn't the road's); then in single file behind the wagon.
bool sideSafe(psrWagon_c* w, const Member& m) {
    const cXyz mid = wagonFrame(w, 0.0f, 0.0f, m.z);
    const f32 road = groundAt(mid.x, mid.y + 100.0f, mid.z);
    if (road == -G_CM3D_F_INF) return true;   // (can't tell: as before)
    for (f32 t : {0.5f, 1.0f}) {
        const cXyz at = wagonFrame(w, m.x * t, 0.0f, m.z);
        const f32 g = groundAt(at.x, road + 100.0f, at.z);
        if (g == -G_CM3D_F_INF || std::fabs(g - road) > 90.0f) return false;
    }
    return true;
}
cXyz escortSpot(psrWagon_c* w, Member& m) {
    if (--m.fileAge <= 0) {
        m.fileAge = 15;
        m.file = !sideSafe(w, m);
    }
    return m.file ? wagonFrame(w, 0.0f, 0.0f, -720.0f - m.order * 170.0f) : wagonFrame(w, m.x, 0.0f, m.z);
}

void bringOut(std::vector<Member>& crew, psrWagon_c* w, s8 room) {
    if (!wagonPlaced(w)) return;
    for (Member& m : crew) {
        if (travellers::crewActor(m.slot) != nullptr || travellers::crewDead(m.slot)) continue;
        cXyz at = m.role == R_RIDER ? w->benchSeat(m.x < 0.0f ? 0 : 1) : escortSpot(w, m);
        if (m.role != R_RIDER) {
            if (const f32 g = groundAt(at.x, at.y, at.z); g != -G_CM3D_F_INF) at.y = g;
        }
        travellers::crewHold(m.slot, true);
        m.anim = 0xFF;
        m.fighting = false;
        travellers::crewSpawn(m.slot, at, static_cast<s16>(w->coachYaw() + m.face), room);
    }
}

void putAway(std::vector<Member>& crew) {
    for (Member& m : crew) travellers::crewDespawn(m.slot);
}

void startConvoy(Convoy& c, Kind kind, f32 along = 0.0f) {
    c = Convoy{};
    c.active = true;
    c.kind = kind;
    c.road = kind == K_GORGE ? 0 : (kind == K_ROYAL ? 1 : 2);
    c.reverse = kind == K_ROYAL;
    c.s = along;
    staffConvoy(c);
    char line[96];
    std::snprintf(line, sizeof(line), "convoys: a %s convoy sets off", kind == K_GORGE ? "gorge" : (kind == K_WEST ? "west-gate" : "royal"));
    svc_log->info(mod_ctx, line);
}

void endConvoy(Convoy& c, int every) {
    if (!dropWagon(c.wagon)) return;   // still loading: next frame
    putAway(c.crew);
    releaseCrew(c.crew);
    c.active = false;
    c.real = false;
    c.nextAt = gameMinutes() + every;
}

bool kindOpen(Kind k) {
    if (k == K_GORGE) return travellers::allTwilightCleared();
    return travellers::allTwilightCleared() && travellers::eldinBridgeWarpedBack();
}

int kindEvery(Kind k) { return k == K_ROYAL ? kRoyalEvery : (k == K_WEST ? kWestEvery : kGorgeEvery); }

void scareTick(std::vector<Member>& crew);   // (below, with the bombs)

void tickConvoy(Convoy& c) {
    const s32 now = gameMinutes();
    if (!c.active) {
        if (!kindOpen(c.kind)) return;
        if (c.nextAt < 0) {
            // the first time this kind is on the roads in a play session: one is already out there,
            // somewhere along its road (not all waiting at their gates for the clock)
            startConvoy(c, c.kind, roadLength(c.road) * (0.1f + 0.8f * static_cast<f32>(rnd() % 1000u) / 1000.0f));
            return;
        }
        // a new day's date can come back smaller (a new file): don't wait for ever
        if (c.nextAt - now > kindEvery(c.kind) * 2) c.nextAt = now;
        if (now >= c.nextAt) startConvoy(c, c.kind);
        return;
    }
    cXyz pos;
    s16 yaw;
    s8 room;
    roadAt(c.road, c.reverse, c.s, &pos, &yaw, &room);
    // (only in the stage Link is in: crossing a loading zone it drives through it, like he does)
    const bool open = worldOpen() && stageAt(c.road, c.reverse, c.s) == curStage();
    // an event (Epona's call, a door, a talk) only holds it where it is: dropping the wagon then made
    // wrecks and whole convoys blink out of the world while Link stood beside them
    const bool held = dComIfGp_event_runCheck();
    if ((g_frame % 300) == 0) {
        travellers::diagLine("convoy  %s %.0f/%.0f at (%.0f %.0f) room %d stage %d/%d link %.0f open %d disp %d real %d halted %d wrecked %d",
                             c.kind == K_GORGE ? "gorge" : (c.kind == K_WEST ? "west" : "royal"), c.s, roadLength(c.road),
                             pos.x, pos.z, room, stageAt(c.road, c.reverse, c.s), curStage(), linkDist(pos), open,
                             dComIfGp_roomControl_checkRoomDisp(room), c.real, c.halted, c.wrecked);
    }
    psrWagon_c* w = wagonOf(c.wagon);

    // out in the world, or back on paper
    if (held) return;
    if (!c.real) {
        if (open && dComIfGp_roomControl_checkRoomDisp(room) && linkDist(pos) < kShowRadius && outOfSight(pos)) {
            c.wagon = makeWagon(c.kind == K_ROYAL ? PSR_WAGON_ROYAL : PSR_WAGON_TRAVELLER, true, pos, yaw, room);
            c.real = c.wagon != fpcM_ERROR_PROCESS_ID_e;
            c.halted = false;
            c.calm = 0;
        }
    } else if (!open || linkDist(pos) > kHideRadius || (w == nullptr && !fpcM_IsCreating(c.wagon))) {
        if (!dropWagon(c.wagon)) return;   // still loading: next frame
        putAway(c.crew);
        c.real = false;
        w = nullptr;
        if (c.wrecked || c.abandoned) {   // the wreck (or the empty wagon) is left behind out of sight: that convoy is over
            endConvoy(c, kindEvery(c.kind));
            return;
        }
    }

    if (c.real && w != nullptr && (c.wrecked || w->wrecked())) {
        if (!c.wrecked) {
            // burnt out or smashed: everyone jumps down and runs (the fighters may stand), the horse
            // breaks loose and bolts on down the road
            c.wrecked = true;
            c.horseS = c.s;
            for (Member& m : c.crew) {
                travellers::crewHold(m.slot, false);
                if (auto* a = travellers::crewActor(m.slot)) {
                    if (m.role == R_RIDER || !travellers::isFighter(a)) travellers::startFlee(a, w->middle());
                    else travellers::resumeWalk(a);
                }
            }
            svc_log->info(mod_ctx, "convoys: a wagon is wrecked");
        }
        if (!w->mHorseGone) {
            c.horseS += 18.0f;
            cXyz hp;
            s16 hy;
            roadAt(c.road, c.reverse, c.horseS, &hp, &hy, nullptr);
            w->driveTo(hp, hy, 18.0f);
            if (c.horseS - c.s > 2500.0f && outOfSight(hp)) w->mHorseGone = true;   // gone, out of sight
        }
        return;
    }
    if (c.real && w != nullptr && !c.abandoned) {
        // every escort down (killed, or never came): the drivers jump down and run, the wagon stands
        bool anyEscort = false, escortUp = false;
        for (const Member& m : c.crew) {
            if (m.role != R_ESCORT) continue;
            anyEscort = true;
            if (!travellers::crewDead(m.slot)) escortUp = true;
        }
        if (anyEscort && !escortUp) {
            c.abandoned = true;
            for (Member& m : c.crew) {
                if (m.role != R_RIDER) continue;
                travellers::crewHold(m.slot, false);
                if (auto* a = travellers::crewActor(m.slot)) travellers::startFlee(a, w->middle());
            }
            svc_log->info(mod_ctx, "convoys: the escort is down, the drivers run");
        }
    }
    if (c.real && w != nullptr && c.abandoned) {
        return;   // (the wagon just stands where they left it)
    }
    if (c.real && w != nullptr) {
        bringOut(c.crew, w, room);
        scareTick(c.crew);
        // Monsters: the wagon never stops (its drivers aren't scared, they keep driving); the escorts
        // go for them, and once it's quiet, or they've fallen too far behind, they run back to their
        // places at the wagon's side.
        if ((g_frame % 10) == 0) {
            const cXyz mid = wagonFrame(w, 0.0f, 0.0f, -150.0f);
            if (foeNear(mid, kFightRadius)) {
                c.halted = true;   // (now: "under attack")
                c.calm = 0;
            } else if (c.halted && !foeNear(mid, kCalmRadius)) {
                c.calm += 10;
            }
            if (c.halted && c.calm >= kCalmFrames) c.halted = false;
            // a foe right by them that nobody on foot can get at (up on a ledge, down a drop): the
            // archers shoot it while the rest hold back behind their shields; no archer up: everyone
            // keeps together and the wagon drives on, fast, out of it
            c.fleeing = false;
            if (c.halted) {
                if (fopAc_ac_c* f = foeAt(mid, kFightRadius); f != nullptr && !travellers::reachableOnFoot(mid, f)) {
                    bool bow = false;
                    for (const Member& m : c.crew) {
                        if (m.archer && m.role == R_ESCORT && travellers::crewActor(m.slot) != nullptr && !travellers::crewDead(m.slot)) bow = true;
                    }
                    c.fleeing = !bow;
                }
            }
            // monsters right at the wagon (battering it, whether or not a blow of theirs lands on a
            // hit box): a knock every second and a half, where the nearest one is
            if (c.halted && (g_frame % 45) == 0) {
                if (fopAc_ac_c* m = foeAt(w->middle(), 450.0f)) {
                    cXyz at = w->middle();
                    cXyz d = m->current.pos - at;
                    d.y = 0.0f;
                    const f32 len = d.absXZ();
                    if (len > 1.0f) { at.x += d.x / len * 100.0f; at.z += d.z / len * 100.0f; }
                    at.y = std::max(at.y, m->current.pos.y + 70.0f);
                    w->hitFx(at);
                    w->addDamage(9);
                }
            }
            for (Member& m : c.crew) {
                if (m.role != R_ESCORT || m.scared > 0) continue;   // (running from a bomb: not now)
                auto* a = travellers::crewActor(m.slot);
                const f32 behind = a != nullptr ? a->current.pos.absXZ(wagonFrame(w, m.x, 0.0f, m.z)) : 0.0f;
                // (and the royal escort, all of them, while they're after Link over his bombs)
                const bool fight = (c.halted && !c.fleeing && behind < kLeash) || (c.kind == K_ROYAL && grudge::squadHostile(2));
                if (m.fighting == fight) continue;
                m.fighting = fight;
                travellers::crewHold(m.slot, !fight);
                // let go: its own fighting takes over from where it stands (not from where it came out);
                // taken back into the march: weapons away (shield and spear on the back)
                if (fight && a != nullptr) travellers::resumeWalk(a);
                if (!fight && a != nullptr) fighter_anim::drop(static_cast<daNpcPasser_c*>(a));
                m.anim = 0xFF;
            }
        }
        const f32 v = c.fleeing ? kSpeed * 2.0f : kSpeed;
        c.s += v;
        roadAt(c.road, c.reverse, c.s, &pos, &yaw, &room);
        // the hitch and the rear axle on the road behind the horse (so the wagon takes the road's own
        // bends and never swings wide into a cliff); the horse faces along the road where it is
        cXyz hitch, rear, ahead;
        roadAt(c.road, c.reverse, c.s - psrWagon_c::hitchBack(), &hitch, nullptr, nullptr);
        roadAt(c.road, c.reverse, c.s - psrWagon_c::hitchBack() - psrWagon_c::rearBack(), &rear, nullptr, nullptr);
        roadAt(c.road, c.reverse, c.s + 120.0f, &ahead, nullptr, nullptr);
        const s16 face = cM_atan2s(ahead.x - hitch.x, ahead.z - hitch.z);
        w->driveOnRoad(pos, face, hitch, rear, v);
    } else if (!c.real) {
        c.s += kSpeed;   // on paper it keeps going
    }

    if (c.s >= roadLength(c.road)) {
        // at the end of its road (a loading zone, Faron Woods' or Castle Town's): it goes through
        endConvoy(c, kindEvery(c.kind));
    }
}

// Link's bombs: everyone but the drivers runs (let go from their places; their own fear does the
// running), and comes back to their place a few seconds after there's no bomb near any more.
constexpr f32 kBombFear = 650.0f;
constexpr int kBombScared = 150;
void scareTick(std::vector<Member>& crew) {
    for (Member& m : crew) {
        if (m.role == R_RIDER) continue;
        auto* a = travellers::crewActor(m.slot);
        if (a == nullptr) continue;
        if (travellers::liveBombNear(a->current.pos, kBombFear) != nullptr) {
            if (m.scared == 0) {
                travellers::crewHold(m.slot, false);
                m.anim = 0xFF;
            }
            m.scared = kBombScared;
        } else if (m.scared > 0 && --m.scared == 0) {
            travellers::crewHold(m.slot, m.role == R_ESCORT ? !m.fighting : true);   // back to the wagon
            m.anim = 0xFF;
        }
    }
}

// --- the parked wagons ----------------------------------------------------------------------
void tickNod() {
    // which wall's spot: the Faron-Eldin wall while Faron's lit and Eldin isn't, then the next one
    int want = -1;
    if (dComIfGs_isDarkClearLV(0) == TRUE && !travellers::eldinCleared()) want = 0;
    else if (travellers::eldinCleared() && !travellers::lanayruCleared()) want = 1;

    psrWagon_c* w = wagonOf(g_nod.wagon);
    const bool open = worldOpen() && curStage() == 0;   // both walls are in the field
    if (dComIfGp_event_runCheck()) return;   // an event holds things as they are (never drops the wagon)
    if (g_nod.which >= 0) {
        const Parked& p = kParked[g_nod.which];
        const bool far = linkDist(p.pos) > kHideRadius;
        if (!open || want != g_nod.which || far || (w == nullptr && !fpcM_IsCreating(g_nod.wagon))) {
            if (!dropWagon(g_nod.wagon)) return;   // still loading: next frame
            putAway(g_nod.crew);
            g_nod.which = -1;
            if (want < 0) releaseCrew(g_nod.crew);   // both walls are gone: so are they
            return;
        }
        if (w != nullptr) {
            bringOut(g_nod.crew, w, p.room);
            scareTick(g_nod.crew);
        }
        return;
    }
    if (want < 0 || !open) return;
    const Parked& p = kParked[want];
    if (!dComIfGp_roomControl_checkRoomDisp(p.room) || linkDist(p.pos) > kShowRadius || !outOfSight(p.pos)) return;
    if (g_nod.crew.empty()) {
        // the same three at both walls: they stand beside the wagon, looking up the road at the wall
        addMember(g_nod.crew, kRiderCast[0], R_BYSTANDER, 260.0f, -80.0f, 0x2000);
        addMember(g_nod.crew, kRiderCast[2], R_BYSTANDER, 330.0f, -330.0f, 0x1000);
        addMember(g_nod.crew, kEscortCast[rnd() % kEscortCast.size()], R_BYSTANDER, 200.0f, 260.0f, -0x1000);
    }
    cXyz at = p.pos;
    if (const f32 g = groundAt(at.x, at.y + 1000.0f, at.z); g != -G_CM3D_F_INF) at.y = g;
    const s16 yaw = cM_atan2s(kWall[want].x - at.x, kWall[want].z - at.z);   // facing the wall
    g_nod.wagon = makeWagon(PSR_WAGON_TRAVELLER, false, at, yaw, p.room);
    if (g_nod.wagon != fpcM_ERROR_PROCESS_ID_e) g_nod.which = want;
}

Member* memberOf(int slot, psrWagon_c** wagon) {
    for (Convoy& c : g_convoys) {
        if (!c.active) continue;
        for (Member& m : c.crew) {
            if (m.slot == slot) { *wagon = wagonOf(c.wagon); return &m; }
        }
    }
    for (Member& m : g_nod.crew) {
        if (m.slot == slot) { *wagon = wagonOf(g_nod.wagon); return &m; }
    }
    return nullptr;
}

int slotOfActor(daNpcPasser_c* p) {
    const int pathId = (fopAcM_GetParam(p) >> 16) & 0xFF;
    return pathId - travellers::kWalkPathBase;
}

void setAnim(daNpcPasser_c* p, Member& m, u8 idx, f32 rate) {
    if (m.anim != idx) {
        m.anim = idx;
        auto* a = static_cast<J3DAnmTransformKey*>(p->getAnmP(idx, idx == 14 ? 0 : p->getObjNum()));
        // a spearman walks and stands with his spear as a staff: the men's own stick walk and wait
        if (travellers::guardStyle(p) == travellers::kStyleSpear && (idx == 2 || idx == 0)) {
            if (auto* stick = static_cast<J3DAnmTransformKey*>(
                    dComIfG_getObjectRes("Mspecial", idx == 2 ? "m_walk_stick.bck" : "m_wait_stick.bck"))) {
                a = stick;
            }
        }
        if (a != nullptr) p->setAnm(a, rate, 8.0f, J3DFrameCtrl::EMode_LOOP, 0, -1);
    }
    if (p->mpMorf != nullptr) p->mpMorf->setPlaySpeed(rate);
}
}  // namespace

void update() {
    if (!g_ready) {
        buildRoads();
        g_rng ^= static_cast<u32>(gameMinutes() * 2654435761u);
        g_ready = true;
    }
    // Link is talking to one of them: everything holds still (other events put the convoys away)
    if (dialogue::talking()) return;
    ++g_frame;
    for (int k = 0; k < K_NUM; ++k) {
        g_convoys[k].kind = static_cast<Kind>(k);
        tickConvoy(g_convoys[k]);
    }
    tickNod();
}

bool convoyFleeing(psrWagon_c* w) {
    for (const Convoy& c : g_convoys) {
        if (c.active && c.real && wagonOf(c.wagon) == w) return c.fleeing;
    }
    return false;
}

void placeHeld(daNpcPasser_c* p) {
    psrWagon_c* w = nullptr;
    Member* m = memberOf(slotOfActor(p), &w);
    if (m == nullptr || w == nullptr) return;
    p->speedF = 0.0f;
    p->speed.y = 0.0f;
    if (m->role != R_RIDER && dialogue::talker() == p) {
        // talking with Link: standing, turned to him
        if (auto* link = dComIfGp_getPlayer(0)) {
            cLib_addCalcAngleS2(&p->shape_angle.y, cLib_targetAngleY(&p->current.pos, &link->current.pos), 4, 0x800);
            p->current.angle.y = p->shape_angle.y;
        }
        p->old.pos = p->current.pos;
        setAnim(p, *m, 0, 1.0f);
        p->setBaseMtx();
        if (p->mpMorf != nullptr) p->mpMorf->modelCalc();
        return;
    }
    if (m->role == R_RIDER) {
        // on the driver's bench, facing the way the wagon goes
        cXyz seat = w->benchSeat(m->x < 0.0f ? 0 : 1);
        seat.y -= kSitDrop;
        p->current.pos = seat;
        p->old.pos = seat;
        p->current.angle.y = p->shape_angle.y = w->coachYaw();
        setAnim(p, *m, 14, 1.0f);
    } else {
        // walking at the wagon's side (or standing by it): head for the spot, at most a brisk walk
        cXyz to = escortSpot(w, *m);
        cXyz d = to - p->current.pos;
        d.y = 0.0f;
        const f32 dist = d.absXZ();
        const bool running = dist > 300.0f || convoyFleeing(w);   // fallen behind, or the wagon's off fast
        f32 step = std::min(dist, kSpeed * (running ? 3.2f : 1.6f));
        if (dist > 1.0f) {
            // never off an edge: no ground where the step lands, or a drop, and he waits there
            const f32 nx = p->current.pos.x + d.x / dist * std::max(step, 30.0f);
            const f32 nz = p->current.pos.z + d.z / dist * std::max(step, 30.0f);
            const f32 g = groundAt(nx, p->current.pos.y + 60.0f, nz);
            if (g == -G_CM3D_F_INF || p->current.pos.y - g > 120.0f || g - p->current.pos.y > 80.0f) step = 0.0f;
            p->current.pos.x += d.x / dist * step;
            p->current.pos.z += d.z / dist * step;
        }
        if (const f32 g = groundAt(p->current.pos.x, p->current.pos.y, p->current.pos.z); g != -G_CM3D_F_INF) {
            p->current.pos.y = g;
        }
        p->old.pos = p->current.pos;
        // walking is how far he really goes each frame: keeping up with a rolling wagon the gap stays a
        // few units, so "far from his spot" never said walk and he slid along in his standing pose
        const f32 moved = dist > 1.0f ? step : 0.0f;
        const bool moving = moved > (m->anim == 2 || m->anim == 4 ? 0.6f : 1.2f);   // (a little hysteresis)
        const s16 face = moving && dist > 120.0f ? cM_atan2s(d.x, d.z)
                         : moving ? static_cast<s16>(w->coachYaw()) : static_cast<s16>(w->coachYaw() + m->face);
        cLib_addCalcAngleS2(&p->shape_angle.y, face, 4, 0x800);
        p->current.angle.y = p->shape_angle.y;
        if (moving && running) setAnim(p, *m, 4, 1.0f);
        else if (moving) setAnim(p, *m, 2, Cd2_HIO_walkAnmPlaySpeed(p->m_type) * std::clamp(moved / kSpeed, 0.6f, 1.6f));
        else setAnim(p, *m, 0, 1.0f);
    }
    p->setBaseMtx();
    if (p->mpMorf != nullptr) p->mpMorf->modelCalc();
}
bool isRider(int slot) {
    for (const Convoy& c : g_convoys) {
        if (!c.active) continue;
        for (const Member& m : c.crew) if (m.slot == slot) return m.role == R_RIDER;
    }
    return false;
}

void scare(int slot) {
    auto run = [&](std::vector<Member>& crew) {
        for (Member& m : crew) {
            if (m.slot != slot || m.role == R_RIDER) continue;
            if (m.scared == 0) { travellers::crewHold(m.slot, false); m.anim = 0xFF; }
            m.scared = kBombScared;
        }
    };
    for (Convoy& c : g_convoys) if (c.active) run(c.crew);
    run(g_nod.crew);
}

int crewSpot(int slot) {
    if (slot < 0) return -1;
    for (const Convoy& c : g_convoys) {
        if (!c.active) continue;
        for (const Member& m : c.crew) if (m.slot == slot) return c.kind;
    }
    for (const Member& m : g_nod.crew) if (m.slot == slot) return 10 + std::max(0, g_nod.which);
    return -1;
}
}  // namespace convoys
