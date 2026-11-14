// Fighters' own animations, played through the traveller's own body animation (its morf).
//
// The animations live in PsrPack.arc (bck/), which every traveller already loads:
//   psr_surprised       startled hop back when an enemy turns up (its hop is moved onto the actor)
//   psr_draw_<body>     hip draw, per body (the hand takes the grip on frame 6)
//   psr_squareup        on guard, weapon out (loop); psr_squareup_step a guarded shuffle
//   psr_s_fight_a       a two-blow attack (blows land on frames 4 and 20)
//   psr_run             running with the weapon out (chasing)
//   psr_injury_run      fleeing on the last heart: a staggering limp-run, clutching its side
//   psr_injury_wait     catching breath after getting away; psr_injury_walk limping on
//   psr_sheathe_<body>  putting it back (the hand lets go on frame 15, 13 for belt-loop weapons)
//
// Only the men fighters have them (the women's skeleton has 21 joints, the men's 19, and #29 has
// no draw of her own yet): everyone else keeps exactly what they did before.
#include "global.h"

#include "d/actor/d_a_npc_passer.h"
#include "d/actor/d_a_npc_cd2.h"
#include "JSystem/J3DGraphAnimator/J3DJoint.h"
#include "mods/service.hpp"
#include "mods/svc/hook.hpp"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"
#include "m_Do/m_Do_mtx.h"
#include "SSystem/SComponent/c_math.h"

#include "fighter_anim.hpp"
#include "travellers.hpp"
#include "camp_cook.hpp"
#include "traveller_gear.hpp"
#include "JSystem/JUtility/JUTNameTab.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace {

constexpr const char* kArc = "PsrPack";
constexpr int kHandR = 11;

struct Body {
    const char* model;
    const char* draw;
    const char* sheathe;
    Mtx grip;     // weapon grip frame in the right hand
    f32 slide;    // belt-loop weapons: the hand takes the handle this far up from the grip
};
const Body kBodies[] = {
#include "fighter_grips.inc"
};

// Stock passer type -> body (the men fighters: 3, 6, 8, 9, 11, 13, 14).
const Body* bodyFor(int type) {
    const char* model = nullptr;
    switch (type) {
        case 3: model = "MON_a"; break;
        case 6: model = "MAS_a"; break;
        case 8: model = "MAN_a2"; break;
        case 9: model = "MAD_a2"; break;
        case 11: model = "MON_a2"; break;
        case 13: model = "MAN_c2"; break;
        case 14: model = "MAS_a2"; break;
        default: return nullptr;
    }
    for (const Body& b : kBodies) {
        if (std::strcmp(b.model, model) == 0) return &b;
    }
    return nullptr;
}

constexpr int kGrabFrame = 6;
constexpr int kOneShotMax = 90;       // the longest one-shot (the swing) is 38 frames
constexpr int kSwordRelease = 15;
constexpr int kLoopRelease = 13;
constexpr int kLoopClear = 12;        // draw: the weapon is up out of its loop
constexpr int kLoopTakeUp = 7;        // sheathe: the hand has slid up the handle
constexpr f32 kHitFrames[] = {4.0f, 20.0f};

// The startled hop, moved from the animation's centre onto the actor (MAN_a2 units, forward +).
constexpr f32 kSurprisedRoot[] = {
    0.0f,   2.01f,  4.05f,   5.96f,   7.59f,   8.77f,   9.74f,   10.75f,  11.69f, 12.47f,
    12.97f, 13.05f, 12.5f,   11.12f,  8.67f,   4.33f,   -2.45f,  -10.75f, -19.49f, -27.56f,
    -33.87f, -37.63f, -39.35f, -39.94f, -39.86f, -39.2f, -38.33f, -37.62f,
};
constexpr int kSurprisedFrames = sizeof(kSurprisedRoot) / sizeof(kSurprisedRoot[0]);

enum class Phase : u8 { Sheathed, Surprised, Drawing, Ready, Swinging, Sheathing };

struct Slot {
    fpc_ProcID owner = fpcM_ERROR_PROCESS_ID_e;
    Phase phase = Phase::Sheathed;
    J3DAnmTransformKey* oneShot = nullptr;   // the one-shot this phase is playing (startle, draw, swing, sheathe)
    int phaseFrames = 0;                     // frames in this phase (a one-shot never takes this long)
    J3DAnmTransform* underlying = nullptr;   // what the game had on before we took over
    f32 prevFrame = 0.0f;
    f32 hopDone = 0.0f;                      // how much of the startled hop has moved the actor
};
std::array<Slot, travellers::kSlots> g_slots{};

Slot* slotFor(const daNpcPasser_c* passer, bool claim) {
    const int i = travellers::slotIndex(passer);
    if (i < 0 || i >= static_cast<int>(g_slots.size())) return nullptr;
    Slot& s = g_slots[i];
    if (s.owner != fopAcM_GetID(passer)) {
        if (!claim) return nullptr;
        s = Slot{};
        s.owner = fopAcM_GetID(passer);
    }
    return &s;
}

J3DAnmTransformKey* anm(const char* name) {
    return static_cast<J3DAnmTransformKey*>(dComIfG_getObjectRes(kArc, name));
}

bool ours(const Body* b, J3DAnmTransform* a) {
    if (a == nullptr) return false;
    static const char* const kNames[] = {"psr_surprised.bck", "psr_squareup.bck", "psr_squareup_step.bck",
                                         "psr_s_fight_a.bck", "psr_run.bck", "psr_injury_wait.bck",
                                         "psr_injury_walk.bck", "psr_injury_run.bck"};
    for (const char* n : kNames) {
        if (a == anm(n)) return true;
    }
    return a == anm(b->draw) || a == anm(b->sheathe);
}


// --- forced animations: hit reactions and the fall (any traveller, borrowed from the Bokoblins) ---
// One per traveller. While one is on it beats everything else: whatever the game (or the fight code)
// puts on is replaced again from the same frame, so it plays through exactly once.
struct Forced {
    fpc_ProcID owner = fpcM_ERROR_PROCESS_ID_e;
    J3DAnmTransformKey* a = nullptr;
    J3DAnmTransform* before = nullptr;   // a hit hands this back when it's over
    f32 speed = 1.0f;
    f32 frame = 0.0f;
    bool restore = false;                // hit: give back `before` at the end; the fall: hold the last frame
    cXyz pin;                            // a hit stops it where it stands
    s16 pinYaw = 0;
};
std::array<Forced, travellers::kSlots> g_forced{};
constexpr f32 kHitSpeed = 1.3f;          // the Bokoblin's 37 frames in just under a second

Forced* forcedFor(const daNpcPasser_c* passer) {
    const int i = travellers::slotIndex(passer);
    if (i < 0 || i >= static_cast<int>(g_forced.size())) return nullptr;
    return &g_forced[i];
}
Forced* activeForced(const daNpcPasser_c* passer) {
    Forced* f = forcedFor(passer);
    return f != nullptr && f->a != nullptr && f->owner == fopAcM_GetID(passer) ? f : nullptr;
}

// This body's version of a borrowed animation: psr_m_* for the men's 19-joint skeleton, psr_w_* for
// the women's 21.
J3DAnmTransformKey* borrowed(const daNpcPasser_c* passer, const char* what) {
    if (passer->mpMorf == nullptr) return nullptr;
    const int joints = passer->mpMorf->getModel()->getModelData()->getJointNum();
    char name[32];
    if (joints == 19) std::snprintf(name, sizeof(name), "psr_m_%s.bck", what);
    else if (joints == 21) std::snprintf(name, sizeof(name), "psr_w_%s.bck", what);
    else return nullptr;
    return anm(name);
}

bool forcedAnm(J3DAnmTransform* a) {
    if (a == nullptr) return false;
    static const char* const kNames[] = {"psr_m_hit_f.bck", "psr_m_hit_l.bck", "psr_m_hit_r.bck", "psr_m_fly.bck",
                                         "psr_m_land.bck",  "psr_w_hit_f.bck", "psr_w_hit_l.bck", "psr_w_hit_r.bck",
                                         "psr_w_fly.bck",   "psr_w_land.bck"};
    for (const char* n : kNames) {
        if (a == anm(n)) return true;
    }
    return false;
}

bool startForced(daNpcPasser_c* passer, J3DAnmTransformKey* a, f32 speed, bool restore, f32 morf) {
    Forced* f = forcedFor(passer);
    if (f == nullptr || a == nullptr || passer->mpMorf == nullptr) return false;
    J3DAnmTransform* current = passer->mpMorf->getAnm();
    const bool already = f->a != nullptr && f->owner == fopAcM_GetID(passer);
    if (!already || !forcedAnm(current)) f->before = forcedAnm(current) ? nullptr : current;
    f->owner = fopAcM_GetID(passer);
    f->a = a;
    f->speed = speed;
    f->frame = 0.0f;
    f->restore = restore;
    f->pin = passer->current.pos;
    f->pinYaw = passer->shape_angle.y;
    passer->mpMorf->setAnm(a, J3DFrameCtrl::EMode_NONE, morf, speed, 0, -1);
    return true;
}

// Start an animation (a different one only: the same one keeps playing).
void play(daNpcPasser_c* passer, J3DAnmTransformKey* a, bool loop, f32 morf) {
    if (a == nullptr || passer->mpMorf == nullptr) return;
    if (passer->mpMorf->getAnm() == a) return;
    passer->mpMorf->setAnm(a, loop ? J3DFrameCtrl::EMode_LOOP : J3DFrameCtrl::EMode_NONE, morf, 1.0f, 0, -1);
}
// Start it from the top even if it is already on (a second swing).
void restart(daNpcPasser_c* passer, J3DAnmTransformKey* a, bool loop, f32 morf) {
    if (a == nullptr || passer->mpMorf == nullptr) return;
    passer->mpMorf->setAnm(a, loop ? J3DFrameCtrl::EMode_LOOP : J3DFrameCtrl::EMode_NONE, morf, 1.0f, 0, -1);
}

bool finished(daNpcPasser_c* passer) { return passer->mpMorf->isStop(); }
f32 frame(daNpcPasser_c* passer) { return passer->mpMorf->getFrame(); }

// Back to whatever the game had playing (its walk, idle, sit...).
void giveBack(daNpcPasser_c* passer, Slot& s) {
    s.phase = Phase::Sheathed;
    s.oneShot = nullptr;
    if (s.underlying != nullptr && passer->mpMorf != nullptr) {
        passer->mpMorf->setAnm(s.underlying, J3DFrameCtrl::EMode_LOOP, 8.0f, 1.0f, 0, -1);
    }
    if (travellers::campInfo(passer, nullptr, nullptr, nullptr)) travellers::refreshIdle(passer);
}

f32 hopScale(const daNpcPasser_c* passer) {
    // the game scales the centre's animated forward move for the heavyset men (type 1 and 9)
    return passer->m_type == 1 || passer->m_type == 9 ? 2.173357f : 1.0f;
}

void hop(daNpcPasser_c* passer, Slot& s) {
    const int f = std::clamp(static_cast<int>(frame(passer)), 0, kSurprisedFrames - 1);
    const f32 want = kSurprisedRoot[f] * hopScale(passer);
    const f32 step = want - s.hopDone;
    s.hopDone = want;
    passer->current.pos.x += cM_ssin(passer->shape_angle.y) * step;
    passer->current.pos.z += cM_scos(passer->shape_angle.y) * step;
    passer->mAcch.CrrPos(dComIfG_Bgsp());
}

// Start a one-shot for a phase (the phase then waits for it to finish).
void startPhase(daNpcPasser_c* passer, Slot& s, Phase ph, J3DAnmTransformKey* a, f32 morf) {
    s.phase = ph;
    s.oneShot = a;
    s.phaseFrames = 0;
    s.prevFrame = 0.0f;
    restart(passer, a, false, morf);
}

void startDraw(daNpcPasser_c* passer, Slot& s, const Body* b) { startPhase(passer, s, Phase::Drawing, anm(b->draw), 4.0f); }

void startSheathe(daNpcPasser_c* passer, Slot& s, const Body* b) {
    startPhase(passer, s, Phase::Sheathing, anm(b->sheathe), 6.0f);
}


// --- layering: legs that keep walking, an arm that holds a light ---------------------------------
// Per traveller, worked out once a frame (layerTick) and applied joint by joint as the body is
// posed (the joint callback below). Weights fade in and out so nothing snaps.
struct Layer {
    fpc_ProcID owner = fpcM_ERROR_PROCESS_ID_e;
    f32 speed = 0.0f;                   // smoothed ground speed (units a frame)
    f32 legW = 0.0f;                    // 0: the legs do what the animation says; 1: walk/run legs
    J3DAnmTransformKey* loco = nullptr; // the walk or run the legs take
    f32 locoFrame = 0.0f;
    f32 armW = 0.0f;                    // 0: free arm; 1: holding the light up
    f32 armFrame = 0.0f;
};
std::array<Layer, travellers::kSlots> g_layer{};
constexpr f32 kLegFade = 1.0f / 6.0f;
constexpr f32 kArmFade = 1.0f / 10.0f;
constexpr f32 kMovingSpeed = 0.9f;      // faster than this (smoothed) counts as on the move

// Joint roles per skeleton (men 19, women 21 joints), found by name: parents and which are legs
// and which the light arm.
enum : u8 { kRoleNone, kRoleLeg, kRoleArm };
struct Skel {
    const J3DModelData* data = nullptr;
    int n = 0;
    s8 parent[32];
    u8 role[32];
};
std::array<Skel, 8> g_skel{};

void walkTree(Skel& k, J3DJoint* j, int parent) {
    for (; j != nullptr; j = j->getYounger()) {
        const int no = j->getJntNo();
        if (no < 32) k.parent[no] = static_cast<s8>(parent);
        walkTree(k, j->getChild(), no);
    }
}

const Skel* skelFor(J3DModelData* data) {
    for (Skel& k : g_skel) {
        if (k.data == data) return &k;
    }
    for (Skel& k : g_skel) {
        if (k.data != nullptr) continue;
        k.data = data;
        k.n = std::min<int>(data->getJointNum(), 32);
        std::fill(std::begin(k.parent), std::end(k.parent), -1);
        std::fill(std::begin(k.role), std::end(k.role), kRoleNone);
        walkTree(k, data->getJointNodePointer(0), -1);
        static const char* const kLegs[] = {"waist", "legL1", "legL2", "footL", "legR1",
                                            "legR2", "footR", "skirtL", "skirtR"};
        static const char* const kArm[] = {"shoulderL", "armL1", "armL2", "handL"};
        JUTNameTab* names = data->getJointName();
        for (int i = 0; i < k.n && names != nullptr; ++i) {
            const char* nm = names->getName(static_cast<u16>(i));
            if (nm == nullptr) continue;
            for (const char* l : kLegs) if (std::strcmp(nm, l) == 0) k.role[i] = kRoleLeg;
            for (const char* a : kArm) if (std::strcmp(nm, a) == 0) k.role[i] = kRoleArm;
        }
        return &k;
    }
    return nullptr;   // (never more than a handful of bodies)
}

// Small matrix helpers (3x4, rotation part orthonormal up to a uniform scale).
void mtxMul(const Mtx a, const Mtx b, Mtx out) {
    Mtx r;
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 4; ++j) {
            r[i][j] = a[i][0] * b[0][j] + a[i][1] * b[1][j] + a[i][2] * b[2][j] + (j == 3 ? a[i][3] : 0.0f);
        }
    }
    std::memcpy(out, r, sizeof(Mtx));
}
bool mtxInv(const Mtx m, Mtx out) {
    const f32 a = m[0][0], b = m[0][1], c = m[0][2], d = m[1][0], e = m[1][1], f = m[1][2], g = m[2][0],
              h = m[2][1], i = m[2][2];
    const f32 A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    const f32 det = a * A + b * B + c * C;
    if (std::fabs(det) < 1e-12f) return false;
    const f32 k = 1.0f / det;
    Mtx r;
    r[0][0] = A * k; r[0][1] = -(b * i - c * h) * k; r[0][2] = (b * f - c * e) * k;
    r[1][0] = B * k; r[1][1] = (a * i - c * g) * k;  r[1][2] = -(a * f - c * d) * k;
    r[2][0] = C * k; r[2][1] = -(a * h - b * g) * k; r[2][2] = (a * e - b * d) * k;
    for (int row = 0; row < 3; ++row) {
        r[row][3] = -(r[row][0] * m[0][3] + r[row][1] * m[1][3] + r[row][2] * m[2][3]);
    }
    std::memcpy(out, r, sizeof(Mtx));
    return true;
}
struct Quat { f32 w, x, y, z; };
Quat toQuat(const Mtx m) {
    // normalise the columns first (in case of scale)
    f32 c[3][3];
    for (int j = 0; j < 3; ++j) {
        const f32 l = std::sqrt(m[0][j] * m[0][j] + m[1][j] * m[1][j] + m[2][j] * m[2][j]);
        const f32 k = l > 1e-6f ? 1.0f / l : 1.0f;
        for (int i = 0; i < 3; ++i) c[i][j] = m[i][j] * k;
    }
    Quat q;
    const f32 tr = c[0][0] + c[1][1] + c[2][2];
    if (tr > 0.0f) {
        const f32 s = std::sqrt(tr + 1.0f) * 2.0f;
        q = {0.25f * s, (c[2][1] - c[1][2]) / s, (c[0][2] - c[2][0]) / s, (c[1][0] - c[0][1]) / s};
    } else if (c[0][0] > c[1][1] && c[0][0] > c[2][2]) {
        const f32 s = std::sqrt(1.0f + c[0][0] - c[1][1] - c[2][2]) * 2.0f;
        q = {(c[2][1] - c[1][2]) / s, 0.25f * s, (c[0][1] + c[1][0]) / s, (c[0][2] + c[2][0]) / s};
    } else if (c[1][1] > c[2][2]) {
        const f32 s = std::sqrt(1.0f + c[1][1] - c[0][0] - c[2][2]) * 2.0f;
        q = {(c[0][2] - c[2][0]) / s, (c[0][1] + c[1][0]) / s, 0.25f * s, (c[1][2] + c[2][1]) / s};
    } else {
        const f32 s = std::sqrt(1.0f + c[2][2] - c[0][0] - c[1][1]) * 2.0f;
        q = {(c[1][0] - c[0][1]) / s, (c[0][2] + c[2][0]) / s, (c[1][2] + c[2][1]) / s, 0.25f * s};
    }
    return q;
}
Quat nlerp(Quat a, Quat b, f32 t) {
    if (a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z < 0.0f) b = {-b.w, -b.x, -b.y, -b.z};
    Quat q = {a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
    const f32 l = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    return {q.w / l, q.x / l, q.y / l, q.z / l};
}
void fromQuat(const Quat& q, const Vec& t, Mtx out) {
    const f32 w = q.w, x = q.x, y = q.y, z = q.z;
    out[0][0] = 1 - 2 * (y * y + z * z); out[0][1] = 2 * (x * y - z * w); out[0][2] = 2 * (x * z + y * w);
    out[1][0] = 2 * (x * y + z * w); out[1][1] = 1 - 2 * (x * x + z * z); out[1][2] = 2 * (y * z - x * w);
    out[2][0] = 2 * (x * z - y * w); out[2][1] = 2 * (y * z + x * w); out[2][2] = 1 - 2 * (x * x + y * y);
    out[0][3] = t.x; out[1][3] = t.y; out[2][3] = t.z;
}
// local matrix of joint `jnt` in animation `a` at `frame`
void animLocal(J3DAnmTransformKey* a, f32 frame, int jnt, Mtx out) {
    J3DTransformInfo info;
    a->calcTransform(frame, static_cast<u16>(jnt), &info);
    J3DGetTranslateRotateMtx(info, out);
}

// Applied inside the joint callback, before the posture tweak: replaces this joint's matrix with
// the layered one (parent's final matrix x blended local).
void applyLayer(daNpcCd2_c* npc, J3DModel* model, int jnt) {
    const int slot = travellers::slotIndex(npc);
    if (slot < 0 || slot >= static_cast<int>(g_layer.size())) return;
    const Layer& L = g_layer[slot];
    if (L.owner != fopAcM_GetID(npc) || (L.legW <= 0.001f && L.armW <= 0.001f)) return;
    const Skel* k = skelFor(model->getModelData());
    if (k == nullptr || jnt >= k->n || k->parent[jnt] < 0) return;
    const u8 role = k->role[jnt];
    const bool leg = role == kRoleLeg && L.legW > 0.001f && L.loco != nullptr;
    J3DAnmTransformKey* torch = role == kRoleArm && L.armW > 0.001f ? anm("psr_wait_torch.bck") : nullptr;
    if (!leg && torch == nullptr) return;
    Mtx P, Pinv, La, Lo, Lb, out;
    std::memcpy(P, model->getAnmMtx(k->parent[jnt]), sizeof(Mtx));
    if (!mtxInv(P, Pinv)) return;
    mtxMul(Pinv, model->getAnmMtx(jnt), La);   // what the animation itself gives this joint
    Vec t;
    f32 w;
    if (leg) {
        animLocal(L.loco, L.locoFrame, jnt, Lo);
        w = L.legW;
        t = {La[0][3] + (Lo[0][3] - La[0][3]) * w, La[1][3] + (Lo[1][3] - La[1][3]) * w,
             La[2][3] + (Lo[2][3] - La[2][3]) * w};
    } else {
        animLocal(torch, L.armFrame, jnt, Lo);   // the men's joint numbers: the arm is 4..7 on both
        w = L.armW;
        t = {La[0][3], La[1][3], La[2][3]};      // its own bone lengths
    }
    fromQuat(nlerp(toQuat(La), toQuat(Lo), w), t, Lb);
    mtxMul(P, Lb, out);
    model->setAnmMtx(jnt, out);
    cMtx_copy(out, J3DSys::mCurrentMtx);
}

bool isLoco(daNpcPasser_c* passer, J3DAnmTransform* a) {
    if (a == nullptr) return false;
    const int obj = passer->getObjNum();
    for (int idx : {2, 3, 4, 5}) {
        if (a == passer->getAnmP(idx, obj)) return true;
    }
    return a == anm("psr_run.bck") || a == anm("psr_injury_run.bck") || a == anm("psr_injury_walk.bck");
}
}  // namespace

namespace fighter_anim {

bool has(const daNpcPasser_c* passer) {
    if (passer == nullptr || passer->mpMorf == nullptr || !travellers::isFighter(passer)) return false;
    const Body* b = bodyFor(passer->m_type);
    return b != nullptr && anm(b->draw) != nullptr && anm(b->sheathe) != nullptr &&
           anm("psr_s_fight_a.bck") != nullptr;
}

void drop(daNpcPasser_c* passer) {
    Slot* s = slotFor(passer, false);
    if (s == nullptr || s->phase == Phase::Sheathed) return;
    giveBack(passer, *s);
}

bool busy(const daNpcPasser_c* passer) {
    const Slot* s = slotFor(passer, false);
    return s != nullptr && (s->phase == Phase::Surprised || s->phase == Phase::Drawing ||
                            s->phase == Phase::Sheathing || s->phase == Phase::Swinging);
}

bool weaponOut(const daNpcPasser_c* passer) {
    const Slot* s = slotFor(passer, false);
    return s != nullptr && s->phase != Phase::Sheathed && s->phase != Phase::Surprised;
}

int update(daNpcPasser_c* passer, Want want, bool swing, bool turning) {
    if (!has(passer)) return 0;
    if (activeForced(passer) != nullptr) return 0;   // being hit (or falling): that comes first
    Slot* sp = slotFor(passer, true);
    if (sp == nullptr) return 0;
    Slot& s = *sp;
    const Body* b = bodyFor(passer->m_type);

    // Remember what the game itself last put on, to hand it back afterwards.
    J3DAnmTransform* current = passer->mpMorf->getAnm();
    if (!ours(b, current)) s.underlying = current;
    if (want == Want::Keep) return 0;

    // Something else (the game's own code) put another animation on mid one-shot: put ours back,
    // or it would wait forever for an animation that isn't playing. And never stay in a one-shot
    // longer than any of them lasts.
    if (s.oneShot != nullptr) {
        ++s.phaseFrames;
        if (current != s.oneShot && s.phaseFrames < kOneShotMax) {
            restart(passer, s.oneShot, false, 2.0f);
            s.prevFrame = 0.0f;
        } else if (s.phaseFrames >= kOneShotMax) {
            if (s.phase == Phase::Sheathing) {
                giveBack(passer, s);
            } else {
                s.phase = Phase::Ready;
                s.oneShot = nullptr;
            }
        }
    }

    int hits = 0;
    const f32 now = frame(passer);
    const bool armed = want == Want::Ready || want == Want::Chase;

    switch (s.phase) {
        case Phase::Sheathed:
            if (armed) {
                s.hopDone = 0.0f;
                startPhase(passer, s, Phase::Surprised, anm("psr_surprised.bck"), 3.0f);
            } else if (want == Want::Flee) {
                play(passer, anm("psr_injury_run.bck"), true, 6.0f);
            } else if (want == Want::HurtWait) {
                play(passer, anm("psr_injury_wait.bck"), true, 8.0f);
            } else if (want == Want::HurtWalk) {
                play(passer, anm("psr_injury_walk.bck"), true, 8.0f);
            } else if (ours(b, current)) {
                giveBack(passer, s);   // limp or catch-breath over: the game's own animation again
            }
            break;

        case Phase::Surprised:
            hop(passer, s);
            if (!armed) {
                giveBack(passer, s);   // the enemy went away (or it has to run) before it drew
            } else if (finished(passer)) {
                startDraw(passer, s, b);
            }
            break;

        case Phase::Drawing:
            if (finished(passer)) {
                s.phase = Phase::Ready;
                s.oneShot = nullptr;
            } else if (!armed && now < kGrabFrame) {
                giveBack(passer, s);   // hadn't touched it yet
            }
            break;

        case Phase::Ready:
            if (want == Want::Ready) {
                if (swing) {
                    startPhase(passer, s, Phase::Swinging, anm("psr_s_fight_a.bck"), 3.0f);
                } else if (turning) {
                    play(passer, anm("psr_squareup_step.bck"), true, 4.0f);   // shuffles round to face it
                } else {
                    play(passer, anm("psr_squareup.bck"), true, 6.0f);
                }
            } else if (want == Want::Chase) {
                play(passer, anm("psr_run.bck"), true, 6.0f);
            } else if (want == Want::Flee) {
                play(passer, anm("psr_injury_run.bck"), true, 6.0f);   // weapon still in hand
            } else if (want == Want::HurtWait) {
                play(passer, anm("psr_injury_wait.bck"), true, 8.0f);
            } else {
                startSheathe(passer, s, b);   // Calm or HurtWalk
            }
            break;

        case Phase::Swinging:
            for (f32 hit : kHitFrames) {
                if (s.prevFrame < hit && now >= hit) ++hits;
            }
            if (finished(passer)) {
                s.phase = Phase::Ready;
                s.oneShot = nullptr;
                play(passer, anm("psr_squareup.bck"), true, 6.0f);
            }
            break;

        case Phase::Sheathing:
            if (armed && now < (b->slide > 0.0f ? kLoopRelease : kSwordRelease)) {
                s.phase = Phase::Ready;   // still in hand: back on guard
                s.oneShot = nullptr;
            } else if (armed) {
                startDraw(passer, s, b);
            } else if (finished(passer)) {
                s.phase = Phase::Sheathed;
                if (want == Want::HurtWalk) play(passer, anm("psr_injury_walk.bck"), true, 8.0f);
                else if (want == Want::HurtWait) play(passer, anm("psr_injury_wait.bck"), true, 8.0f);
                else giveBack(passer, s);
            }
            break;
    }
    s.prevFrame = frame(passer);
    return std::min(hits, 1);
}

bool ownSpeed(const daNpcPasser_c* passer) {
    const Slot* s = slotFor(passer, false);
    if (s == nullptr || passer->mpMorf == nullptr) return false;
    const Body* b = bodyFor(passer->m_type);
    J3DAnmTransform* a = passer->mpMorf->getAnm();
    if (b == nullptr || !ours(b, a)) return false;
    if (a == anm("psr_injury_walk.bck") || a == anm("psr_injury_run.bck")) return false;
    return !(a == anm("psr_run.bck") && passer->mActionIdx == daNpcPasser_c::MODE_1);
}

bool heldMtx(const daNpcPasser_c* passer, Mtx out) {
    const Body* b = passer != nullptr ? bodyFor(passer->m_type) : nullptr;
    if (b == nullptr || passer->mpMorf == nullptr) return false;
    mDoMtx_copy(passer->mpMorf->getModel()->getAnmMtx(kHandR), mDoMtx_stack_c::now);
    mDoMtx_stack_c::concat(b->grip);
    mDoMtx_copy(mDoMtx_stack_c::now, out);
    return true;
}

bool weaponMtx(const daNpcPasser_c* passer, Mtx out) {
    const Slot* s = slotFor(passer, false);
    if (s == nullptr || passer->mpMorf == nullptr) return false;
    const Body* b = bodyFor(passer->m_type);
    if (b == nullptr) return false;
    const f32 f = passer->mpMorf->getFrame();
    const bool looped = b->slide > 0.0f;
    f32 slide = 0.0f;   // how far the hand is up the handle from the grip
    auto smooth = [](f32 x) { x = std::clamp(x, 0.0f, 1.0f); return x * x * (3.0f - 2.0f * x); };
    switch (s->phase) {
        case Phase::Sheathed:
        case Phase::Surprised:
            return false;
        case Phase::Drawing:
            if (f < kGrabFrame) return false;
            if (looped) {
                const f32 end = passer->mpMorf->getEndFrame() - 1.0f;
                slide = b->slide * (1.0f - smooth((f - kLoopClear) / std::max(1.0f, end - kLoopClear)));
            }
            break;
        case Phase::Sheathing:
            if (f >= (looped ? kLoopRelease : kSwordRelease)) return false;
            if (looped) slide = b->slide * smooth(f / kLoopTakeUp);
            break;
        default:
            break;
    }
    J3DModel* body = passer->mpMorf->getModel();
    mDoMtx_copy(body->getAnmMtx(kHandR), mDoMtx_stack_c::now);
    mDoMtx_stack_c::concat(b->grip);
    mDoMtx_stack_c::transM(0.0f, 0.0f, -slide);
    mDoMtx_copy(mDoMtx_stack_c::now, out);
    return true;
}


void hit(daNpcPasser_c* passer, const cXyz& from) {
    if (passer == nullptr || passer->mpMorf == nullptr) return;
    // which way the blow came from, against the way it faces (+ is its left)
    const s16 rel = static_cast<s16>(cLib_targetAngleY(&passer->current.pos, &from) - passer->shape_angle.y);
    const int a = std::abs(static_cast<int>(rel));
    const char* what = (a < 0x2800 || a > 0x5800) ? "hit_f" : (rel > 0 ? "hit_l" : "hit_r");
    J3DAnmTransformKey* anim = borrowed(passer, what);
    if (anim == nullptr) return;
    // a fighter mid-swing loses the swing (it goes back on guard after)
    if (Slot* s = slotFor(passer, false); s != nullptr && s->phase == Phase::Swinging) {
        s->phase = Phase::Ready;
        s->oneShot = nullptr;
    }
    startForced(passer, anim, kHitSpeed, true, 2.0f);
}

bool fall(daNpcPasser_c* passer, bool landed) {
    if (passer == nullptr) return false;
    J3DAnmTransformKey* anim = borrowed(passer, landed ? "land" : "fly");
    return startForced(passer, anim, 1.0f, false, landed ? 2.0f : 4.0f);
}

bool forcedTick(daNpcPasser_c* passer) {
    Forced* f = activeForced(passer);
    if (f == nullptr || passer->mpMorf == nullptr) return false;
    mDoExt_McaMorfSO* morf = passer->mpMorf;
    if (morf->getAnm() != f->a) {
        // something else went on (it started running away, say): that is what it goes back to
        // afterwards, not what it was doing before the hit (a stood-still pose sliding along).
        // Then ours again, from where it had got to.
        if (!forcedAnm(morf->getAnm())) f->before = morf->getAnm();
        morf->setAnm(f->a, J3DFrameCtrl::EMode_NONE, 0.0f, f->speed, 0, -1);
        morf->setFrameF(f->frame);
    }
    f->frame = morf->getFrame();
    const bool done = morf->isStop() || f->frame >= morf->getEndFrame() - 1.0f;
    if (f->restore) {
        // a hit holds it on the spot, facing the same way
        passer->current.pos.x = f->pin.x;
        passer->current.pos.z = f->pin.z;
        passer->shape_angle.y = passer->current.angle.y = f->pinYaw;
        passer->speedF = 0.0f;
        if (done) {
            if (f->before != nullptr) morf->setAnm(f->before, J3DFrameCtrl::EMode_LOOP, 6.0f, 1.0f, 0, -1);
            f->a = nullptr;
            if (travellers::campInfo(passer, nullptr, nullptr, nullptr)) travellers::refreshIdle(passer);
            return false;
        }
    }
    return true;
}

bool forcedDone(const daNpcPasser_c* passer) {
    const Forced* f = activeForced(passer);
    if (f == nullptr || passer->mpMorf == nullptr) return true;
    return passer->mpMorf->getAnm() == f->a &&
           (passer->mpMorf->isStop() || passer->mpMorf->getFrame() >= passer->mpMorf->getEndFrame() - 1.0f);
}

bool hitting(const daNpcPasser_c* passer) {
    const Forced* f = activeForced(passer);
    return f != nullptr && f->restore;
}
void layerTick(daNpcPasser_c* passer, bool carryLight) {
    const int slot = travellers::slotIndex(passer);
    if (slot < 0 || slot >= static_cast<int>(g_layer.size()) || passer->mpMorf == nullptr) return;
    Layer& L = g_layer[slot];
    if (L.owner != fopAcM_GetID(passer)) {
        L = Layer{};
        L.owner = fopAcM_GetID(passer);
    }
    const f32 moved = passer->current.pos.absXZ(passer->old.pos);
    L.speed = L.speed * 0.6f + std::min(moved, 40.0f) * 0.4f;
    J3DAnmTransform* cur = passer->mpMorf->getAnm();
    const bool forced = forcedAnm(cur) || cur == anm("psr_surprised.bck");
    const bool sitting = traveller_gear::sitting(slot);

    // legs: on the move with something that isn't a walk or run
    const bool wantLegs = L.speed > kMovingSpeed && !isLoco(passer, cur) && !forced && !sitting;
    const f32 walkMax = Cd2_HIO_walkMaxSpeed(passer->m_type);
    const bool run = L.speed > walkMax * 1.25f;
    auto* loco = static_cast<J3DAnmTransformKey*>(passer->getAnmP(run ? 4 : 2, passer->getObjNum()));
    if (loco != nullptr && loco != L.loco) {
        // keep the step's phase when switching between walk and run
        const f32 phase = L.loco != nullptr ? L.locoFrame / std::max(1.0f, static_cast<f32>(L.loco->getFrameMax())) : 0.0f;
        L.loco = loco;
        L.locoFrame = phase * static_cast<f32>(loco->getFrameMax());
    }
    if (L.loco != nullptr) {
        const f32 top = run ? Cd2_HIO_maxSpeed(passer->m_type) * 1.5f : walkMax;
        const f32 play = run ? Cd2_HIO_anmPlaySpeed(passer->m_type) * 1.5f : Cd2_HIO_walkAnmPlaySpeed(passer->m_type);
        const f32 rate = std::clamp(play * std::min(1.0f, L.speed / std::max(0.1f, top)), 0.6f, 2.0f);
        const f32 end = std::max(1.0f, static_cast<f32>(L.loco->getFrameMax()));
        L.locoFrame = std::fmod(L.locoFrame + rate, end);
    }
    L.legW = std::clamp(L.legW + (wantLegs ? kLegFade : -kLegFade), 0.0f, 1.0f);

    // the light arm
    const bool wantArm = carryLight && !forced && !sitting && anm("psr_wait_torch.bck") != nullptr;
    L.armW = std::clamp(L.armW + (wantArm ? kArmFade : -kArmFade), 0.0f, 1.0f);
    if (auto* t = anm("psr_wait_torch.bck")) L.armFrame = std::fmod(L.armFrame + 1.0f, std::max(1.0f, static_cast<f32>(t->getFrameMax())));
}

bool leftHandMtx(const daNpcPasser_c* passer, Mtx out) {
    if (passer == nullptr || passer->mpMorf == nullptr) return false;
    J3DModel* m = passer->mpMorf->getModel();
    const Skel* k = skelFor(m->getModelData());
    if (k == nullptr) return false;
    for (int i = 0; i < k->n; ++i) {
        if (k->role[i] == kRoleArm && (i + 1 >= k->n || k->role[i + 1] != kRoleArm)) {   // the last arm joint: the hand
            mDoMtx_copy(m->getAnmMtx(i), out);
            return true;
        }
    }
    return false;
}

}  // namespace fighter_anim


namespace {

DEFINE_HOOK(&daNpcCd2_c::jntNodeCB, Cd2JointTweak);

// How much of the body's own tweak to apply: 1 normally, 0 while one of our animations plays
// (faded over a few frames so it doesn't snap). Slots 0..31 travellers, 32 the cook.
std::array<f32, travellers::kSlots + 1> g_tweak{};
std::array<fpc_ProcID, travellers::kSlots + 1> g_tweakOwner{};
constexpr f32 kTweakFade = 1.0f / 8.0f;

int tweakSlot(daNpcCd2_c* npc) {
    if (cook::isBody(npc)) return travellers::kSlots;
    if (!travellers::isTraveller(npc)) return -1;
    return travellers::slotIndex(npc);
}

// The game's own posture value for this body and joint (the same conversion the game uses).
void gameTweak(int type, int jnt, cXyz* tr, csXyz* rot) {
    const auto& t = type < 16 ? l_Cd2_HIO.field_0x0004[type].field_0x004[jnt].field_0x04
                              : l_Cd2_HIO.field_0x20c4[type - 16].field_0x004[jnt].field_0x04;
    tr->set(10.0f * t.field_0x00[0], 10.0f * t.field_0x00[1], 10.0f * t.field_0x00[2]);
    rot->set(DEG2S(t.field_0x0C[0]), DEG2S(t.field_0x0C[1]), DEG2S(t.field_0x0C[2]));
}

HookAction onJointTweakPre(ModContext*, void* args, void* retval, void*) {
    auto* npc = mods::arg<daNpcCd2_c*>(args, 0);
    auto* joint = mods::arg<J3DJoint*>(args, 1);
    auto* model = mods::arg<J3DModel*>(args, 2);
    if (npc == nullptr || joint == nullptr || model == nullptr || npc->mpMorf == nullptr) {
        return HOOK_CONTINUE;
    }
    const int slot = tweakSlot(npc);
    if (slot < 0 || slot > travellers::kSlots) return HOOK_CONTINUE;
    const Body* b = npc->m_type < 16 ? bodyFor(npc->m_type) : nullptr;
    const int jnt = joint->getJntNo();
    if (slot < travellers::kSlots) applyLayer(npc, model, jnt);   // running legs, the light arm
    if (g_tweakOwner[slot] != fopAcM_GetID(npc)) {
        g_tweakOwner[slot] = fopAcM_GetID(npc);
        g_tweak[slot] = 1.0f;
    }
    if (jnt == 0) {
        // once a frame (the first joint): fade the body's posture tweak toward none while one of
        // our finished-pose animations plays (fight moves, hit reactions, the fall), back to full after
        const bool mine = (b != nullptr && ours(b, npc->mpMorf->getAnm())) || forcedAnm(npc->mpMorf->getAnm());
        const f32 target = mine ? 0.0f : 1.0f;
        f32& w = g_tweak[slot];
        w = w < target ? std::min(target, w + kTweakFade) : std::max(target, w - kTweakFade);
    }
    const f32 w = g_tweak[slot];
    if (w >= 0.999f) return HOOK_CONTINUE;   // the game's own, untouched
    cXyz tr;
    csXyz rot;
    gameTweak(npc->m_type, jnt, &tr, &rot);
    tr *= w;
    rot.set(static_cast<s16>(rot.x * w), static_cast<s16>(rot.y * w), static_cast<s16>(rot.z * w));
    mDoMtx_stack_c::copy(model->getAnmMtx(jnt));
    mDoMtx_stack_c::ZXYrotM(rot);
    mDoMtx_stack_c::transM(tr);
    model->setAnmMtx(jnt, mDoMtx_stack_c::get());
    cMtx_copy(mDoMtx_stack_c::get(), J3DSys::mCurrentMtx);
    if (retval != nullptr) *static_cast<int*>(retval) = 1;
    return HOOK_SKIP_ORIGINAL;
}

}  // namespace

namespace fighter_anim {
bool install() { return mods::hook::add_pre<Cd2JointTweak>(onJointTweakPre) == MOD_OK; }

}  // namespace fighter_anim
