#include "global.h"

#include <algorithm>
#include <cmath>

#include "tent_actor.hpp"

#include "SSystem/SComponent/c_math.h"
#include "d/d_bg_s_gnd_chk.h"
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_draw_priority.h"
#include "f_pc/f_pc_manager.h"
#include "m_Do/m_Do_ext.h"
#include "m_Do/m_Do_mtx.h"
#include "mods/service.hpp"
#include "mods/svc/log.h"

IMPORT_SERVICE(ActorService, svc_actor);

namespace {
const char* kArcName = "PsrTent";
const char* kModel[PSR_TENT_KIND_NUM] = {"a_tent.bmd", "wall_tent.bmd", "beedle_tent.bmd"};
const char* kCollision[PSR_TENT_KIND_NUM] = {"a_tent.dzb", "wall_tent.dzb", "beedle_tent.dzb"};
const char* kRoom[2] = {"beedle_room.bmd", "beedle_room_n.bmd"};   // the baked room, by day and by night
// Room for the model instance, its display lists and the collider (the tents are big meshes).
constexpr u32 kHeapSize = 0x80000;

// Beedle's tent: props from the game's own archives standing on the little stage behind the curtain,
// plus the entry lantern. Positions are in the tent's own space (doorway toward +z).
struct StageProp {
    const char* arc;
    const char* model;
    f32 x, y, z, scale;
};
constexpr StageProp kStage[] = {
    {"Obj_kntr", "ob_kantera.bmd", 0.0f, 205.0f, 282.5f, 1.6f},    // the entry lantern
};
static_assert(sizeof(kStage) / sizeof(kStage[0]) <= psrTent_c::kStageMax);

bool isNight() {
    const int hour = dKy_getdaytime_hour();
    return hour >= 19 || hour < 6;
}
}  // namespace

psrTent_c::~psrTent_c() {
    if (mpCollider != NULL) {
        dComIfG_Bgsp().Release(mpCollider);
    }
    dComIfG_resDelete(&mPhase, kArcName);
    if ((fopAcM_GetParam(this) & 0xFF) == PSR_TENT_BEEDLE) {
        dComIfG_resDelete(&mPhaseLamp, "Obj_kntr");
    }
}

cPhs_Step psrTent_c::create() {
    fopAcM_ct(this, psrTent_c);
    cPhs_Step step = dComIfG_resLoad(&mPhase, kArcName);
    if (step != cPhs_COMPLEATE_e) {
        return step;
    }
    if ((fopAcM_GetParam(this) & 0xFF) == PSR_TENT_BEEDLE) {
        // The stage props come straight from the disc archives, so they match the player's mods/HD packs.
        const cPhs_Step l = dComIfG_resLoad(&mPhaseLamp, "Obj_kntr");
        if (l != cPhs_COMPLEATE_e) {
            return l;
        }
    }
    mpModel = NULL;
    mpRoom[0] = mpRoom[1] = NULL;
    mpCollider = NULL;
    mGrounded = false;
    mStageNum = 0;
    mKind = static_cast<u8>(fopAcM_GetParam(this) & 0xFF);
    if (mKind >= PSR_TENT_KIND_NUM) {
        mKind = PSR_TENT_A;
    }
    // Sit on the ground before the tent's own collision exists, so the probe can't hit the roof.
    snapToGround();
    if (!fopAcM_entrySolidHeap(this, createHeapCallBack, kHeapSize)) {
        return cPhs_ERROR_e;
    }
    if (mpCollider != NULL && dComIfG_Bgsp().Regist(mpCollider, this) == true) {
        return cPhs_ERROR_e;
    }
    fopAcM_SetMtx(this, mpModel->getBaseTRMtx());
    // Generous cull box: Beedle's banner pole stands about 5 m tall, guy ropes reach 3 m out.
    fopAcM_setCullSizeBox(this, -860.0f, -40.0f, -860.0f, 860.0f, 900.0f, 860.0f);
    Execute();
    return step;
}

constexpr f32 kMaxTilt = 0.30f;   // about 17 degrees: steeper than that it leans this far and sinks in

static f32 groundAt(f32 x, f32 y, f32 z, dBgS_ObjGndChk* out = nullptr) {
    dBgS_ObjGndChk gnd;
    cXyz probe(x, y + 600.0f, z);
    gnd.SetPos(&probe);
    const f32 g = dComIfG_Bgsp().GroundCross(&gnd);
    if (out != nullptr) *out = gnd;
    return g;
}

// Stand the tent on the ground: probe the middle and the four corners of its footprint, lean it to
// match the slope, then lower it until no corner hangs in the air (it sinks a little instead).
void psrTent_c::snapToGround() {
    dBgS_ObjGndChk centre;
    const f32 mid = groundAt(current.pos.x, current.pos.y, current.pos.z, &centre);
    if (mid == -G_CM3D_F_INF) return;
    const f32 hx = psr_tent::kFootX[mKind], hz = psr_tent::kFootZ[mKind];
    const f32 c = cM_scos(shape_angle.y), sn = cM_ssin(shape_angle.y);
    f32 h[4];   // corners: (-x,-z) (+x,-z) (-x,+z) (+x,+z)
    for (int i = 0; i < 4; i++) {
        const f32 lx = (i & 1) ? hx : -hx;
        const f32 lz = (i & 2) ? hz : -hz;
        const f32 g = groundAt(current.pos.x + lx * c + lz * sn, mid, current.pos.z - lx * sn + lz * c);
        h[i] = g == -G_CM3D_F_INF ? mid : g;
    }
    // slope across and along the tent, limited to a sensible lean
    f32 sx = ((h[1] + h[3]) - (h[0] + h[2])) / (4.0f * hx);
    f32 sz = ((h[2] + h[3]) - (h[0] + h[1])) / (4.0f * hz);
    sx = std::max(-kMaxTilt, std::min(kMaxTilt, sx));
    sz = std::max(-kMaxTilt, std::min(kMaxTilt, sz));
    f32 y = (h[0] + h[1] + h[2] + h[3]) * 0.25f;
    // lower it until neither the middle nor any corner of the leaning floor is above the ground
    f32 sink = std::max(0.0f, y - mid);
    for (int i = 0; i < 4; i++) {
        const f32 lx = (i & 1) ? hx : -hx;
        const f32 lz = (i & 2) ? hz : -hz;
        sink = std::max(sink, (y + sx * lx + sz * lz) - h[i]);
    }
    y -= sink;
    current.pos.y = home.pos.y = y;
    mTiltZ = static_cast<s16>(cM_atan2s(sx, 1.0f));    // +x side up
    mTiltX = static_cast<s16>(-cM_atan2s(sz, 1.0f));   // +z side up
    mGrounded = true;
    tevStr.room_no = dComIfG_Bgsp().GetRoomId(centre);
    tevStr.YukaCol = dComIfG_Bgsp().GetPolyColor(centre);
}

int psrTent_c::CreateHeap() {
    auto* data = static_cast<J3DModelData*>(dComIfG_getObjectRes(kArcName, kModel[mKind]));
    if (data == NULL) {
        return 0;
    }
    mpModel = mDoExt_J3DModel__create(data, 0x80000, 0x11000084);
    if (mpModel == NULL) {
        return 0;
    }
    mpRoom[0] = mpRoom[1] = NULL;
    mStageNum = 0;
    if (mKind == PSR_TENT_BEEDLE) {
        for (const StageProp& prop : kStage) {
            auto* d = static_cast<J3DModelData*>(dComIfG_getObjectRes(prop.arc, prop.model));
            if (d == NULL) {
                continue;
            }
            J3DModel* m = mDoExt_J3DModel__create(d, 0x80000, 0x11000084);
            if (m != NULL) {
                mpStage[mStageNum++] = m;
            }
        }
        for (int i = 0; i < 2; i++) {
            auto* room = static_cast<J3DModelData*>(dComIfG_getObjectRes(kArcName, kRoom[i]));
            mpRoom[i] = room != NULL ? mDoExt_J3DModel__create(room, 0x80000, 0x11000084) : NULL;
        }
    }
    mpCollider = JKR_NEW dBgW();
    if (mpCollider == NULL) {
        return 0;
    }
    auto* dzb = static_cast<cBgD_t*>(dComIfG_getObjectRes(kArcName, kCollision[mKind]));
    if (dzb == NULL || mpCollider->Set(dzb, 1, &mColliderMtx) == true) {
        return 0;
    }
    mpCollider->SetCrrFunc(dBgS_MoveBGProc_Typical);
    return 1;
}

int psrTent_c::createHeapCallBack(fopAc_ac_c* actor) {
    return static_cast<psrTent_c*>(actor)->CreateHeap();
}

int psrTent_c::Delete() {
    this->~psrTent_c();
    return 1;
}

int psrTent_c::Execute() {
    if (!mGrounded) {
        // the room's ground may load after the tent: probe without the tent's own floor and roof
        if (mpCollider != NULL) dComIfG_Bgsp().Release(mpCollider);
        snapToGround();
        if (mpCollider != NULL) dComIfG_Bgsp().Regist(mpCollider, this);
    }
    mDoMtx_stack_c::transS(current.pos.x, current.pos.y, current.pos.z);
    mDoMtx_stack_c::YrotM(shape_angle.y);
    mDoMtx_stack_c::XrotM(mTiltX);
    mDoMtx_stack_c::ZrotM(mTiltZ);
    const f32 k = psr_tent::kScale[mKind];
    mDoMtx_stack_c::scaleM(k, k, k);
    mpModel->setBaseTRMtx(mDoMtx_stack_c::get());
    for (J3DModel* room : mpRoom) {
        if (room != NULL) room->setBaseTRMtx(mDoMtx_stack_c::get());
    }
    for (int i = 0; i < mStageNum; i++) {
        const StageProp& prop = kStage[i];
        mDoMtx_stack_c::transS(current.pos.x, current.pos.y, current.pos.z);
        mDoMtx_stack_c::YrotM(shape_angle.y);
        mDoMtx_stack_c::XrotM(mTiltX);
        mDoMtx_stack_c::ZrotM(mTiltZ);
        mDoMtx_stack_c::scaleM(k, k, k);
        mDoMtx_stack_c::transM(prop.x, prop.y, prop.z);
        mDoMtx_stack_c::scaleM(prop.scale, prop.scale, prop.scale);
        mpStage[i]->setBaseTRMtx(mDoMtx_stack_c::get());
    }
    if (mpCollider != NULL) {
        PSMTXCopy(mpModel->getBaseTRMtx(), mColliderMtx);
        mpCollider->Move();
    }
    eyePos = attention_info.position = current.pos;
    attention_info.flags = 0;
    return 1;
}

int psrTent_c::Draw() {
    g_env_light.settingTevStruct(0x20, &current.pos, &tevStr);
    g_env_light.setLightTevColorType_MAJI(mpModel, &tevStr);
    dComIfGd_setListBG();
    mDoExt_modelUpdateDL(mpModel);
    dComIfGd_setList();
    for (int i = 0; i < mStageNum; i++) {
        g_env_light.setLightTevColorType_MAJI(mpStage[i], &tevStr);
        mDoExt_modelUpdateDL(mpStage[i]);
    }
    // The room is baked with its own lamplight, so it's drawn without the scene's lighting: the day
    // room as painted, the night room glowing out through the door and windows.
    if (J3DModel* room = mpRoom[isNight() ? 1 : 0]) {
        mDoExt_modelUpdateDL(room);
    }
    return 1;
}

namespace {
cPhs_Step psrTent_Create(void* i_this) { return static_cast<psrTent_c*>(i_this)->create(); }
int psrTent_Delete(void* i_this) { return static_cast<psrTent_c*>(i_this)->Delete(); }
int psrTent_Execute(void* i_this) { return static_cast<psrTent_c*>(i_this)->Execute(); }
int psrTent_Draw(void* i_this) { return static_cast<psrTent_c*>(i_this)->Draw(); }
int psrTent_IsDelete(void*) { return 1; }
}  // namespace

s16 psrTent_c::sProcName = -1;
ActorHandle psrTent_c::sActorHandle = -1;
const ActorProfileDesc psrTent_c::sProfile = {
    .name = PSR_TENT_NAME,
    .priority_group = 7,
    .process_size = sizeof(psrTent_c),
    .draw_priority = fpcDwPi_OBJ_LBOX_e,
    .status = fopAcStts_UNK_0x40000_e | fopAcStts_UNK_0x4000_e | fopAcStts_CULL_e,
    .group = fopAc_ACTOR_e,
    .cull_type = fopAc_CULLBOX_CUSTOM_e,
    .create_function = psrTent_Create,
    .delete_function = psrTent_Delete,
    .execute_function = psrTent_Execute,
    .is_delete_function = psrTent_IsDelete,
    .draw_function = psrTent_Draw,
};

namespace psr_tent {
bool pushOff(fopAc_ac_c* npc, dBgS_Acch& acch) {
    if (npc == nullptr || !acch.ChkGroundHit()) return false;
    // The game's own lookup stops the whole game (an assert) if the ground's index is out of range,
    // which it can be for a frame (e.g. while new collision is being added): check it ourselves first.
    const int bg = acch.m_gnd.GetBgIndex();
    if (bg < 0 || bg >= 256 || dComIfG_Bgsp().GetBgWBasePointer(acch.m_gnd) == nullptr) return false;
    fopAc_ac_c* under = dComIfG_Bgsp().GetActorPointer(acch.m_gnd);
    if (under == nullptr || fopAcM_GetName(under) != psrTent_c::sProcName) return false;
    auto* tent = static_cast<psrTent_c*>(under);
    // where it stands, in the tent's own frame (x across, z front to back)
    const f32 c = cM_scos(tent->shape_angle.y), sn = cM_ssin(tent->shape_angle.y);
    const f32 dx = npc->current.pos.x - tent->current.pos.x, dz = npc->current.pos.z - tent->current.pos.z;
    f32 lx = dx * c - dz * sn;
    f32 lz = dx * sn + dz * c;
    const f32 hx = kFootX[tent->mKind] + 45.0f, hz = kFootZ[tent->mKind] + 45.0f;
    // out over whichever edge is nearest
    if (hx - std::abs(lx) < hz - std::abs(lz)) lx = lx < 0.0f ? -hx : hx;
    else lz = lz < 0.0f ? -hz : hz;
    const f32 x = tent->current.pos.x + lx * c + lz * sn;
    const f32 z = tent->current.pos.z - lx * sn + lz * c;
    const f32 g = groundAt(x, tent->current.pos.y + 60.0f, z);
    npc->current.pos.set(x, g == -G_CM3D_F_INF ? tent->current.pos.y : g, z);
    npc->old.pos = npc->current.pos;
    npc->speed.y = 0.0f;
    return true;
}

namespace {
// Does the segment a->b (tent-local x/z) pass through the box |x|<hx, |z|<hz? (slab test)
bool segHitsBox(f32 ax, f32 az, f32 bx, f32 bz, f32 hx, f32 hz) {
    f32 t0 = 0.0f, t1 = 1.0f;
    const f32 d[2] = {bx - ax, bz - az}, o[2] = {ax, az}, h[2] = {hx, hz};
    for (int i = 0; i < 2; ++i) {
        if (std::abs(d[i]) < 1e-4f) {
            if (std::abs(o[i]) >= h[i]) return false;
            continue;
        }
        f32 u = (-h[i] - o[i]) / d[i], v = (h[i] - o[i]) / d[i];
        if (u > v) std::swap(u, v);
        t0 = std::max(t0, u);
        t1 = std::min(t1, v);
        if (t0 > t1) return false;
    }
    return true;
}

struct DetourSearch {
    cXyz from, to;
    f32 bestDist;
    fopAc_ac_c* tent;
    cXyz corner;
};

void* detourSub(void* proc, void* data) {
    auto* a = static_cast<fopAc_ac_c*>(proc);
    auto* s = static_cast<DetourSearch*>(data);
    if (a == nullptr || fopAcM_GetName(a) != psrTent_c::sProcName) return nullptr;
    auto* tent = static_cast<psrTent_c*>(a);
    const f32 dist = s->from.absXZ(tent->current.pos);
    if (dist > 1500.0f || dist >= s->bestDist) return nullptr;
    const f32 c = cM_scos(tent->shape_angle.y), sn = cM_ssin(tent->shape_angle.y);
    auto local = [&](const cXyz& p, f32& lx, f32& lz) {
        const f32 dx = p.x - tent->current.pos.x, dz = p.z - tent->current.pos.z;
        lx = dx * c - dz * sn;
        lz = dx * sn + dz * c;
    };
    f32 fx, fz, tx, tz;
    local(s->from, fx, fz);
    local(s->to, tx, tz);
    const f32 hx = psr_tent::kFootX[tent->mKind] + 50.0f, hz = psr_tent::kFootZ[tent->mKind] + 50.0f;
    if (!segHitsBox(fx, fz, tx, tz, hx, hz)) return nullptr;
    // the four corners, well clear of the canvas; the one that makes the shortest way round and
    // can be walked to straight
    const f32 cx = hx + 70.0f, cz = hz + 70.0f;
    const f32 corners[4][2] = {{cx, cz}, {-cx, cz}, {cx, -cz}, {-cx, -cz}};
    f32 best = G_CM3D_F_INF;
    int pick = -1;
    for (int i = 0; i < 4; ++i) {
        const f32 kx = corners[i][0], kz = corners[i][1];
        if (segHitsBox(fx, fz, kx, kz, hx, hz) && !(std::abs(fx) < hx && std::abs(fz) < hz)) continue;
        const f32 len = std::hypot(kx - fx, kz - fz) + std::hypot(tx - kx, tz - kz);
        if (len < best) { best = len; pick = i; }
    }
    if (pick < 0) return nullptr;
    const f32 kx = corners[pick][0], kz = corners[pick][1];
    s->corner.set(tent->current.pos.x + kx * c + kz * sn, tent->current.pos.y,
                  tent->current.pos.z - kx * sn + kz * c);
    s->tent = tent;
    s->bestDist = dist;
    return nullptr;
}
}  // namespace

fopAc_ac_c* detour(const cXyz& from, const cXyz& to, cXyz* corner) {
    DetourSearch s{from, to, G_CM3D_F_INF, nullptr, cXyz::Zero};
    fpcM_Search(detourSub, &s);
    if (s.tent != nullptr && corner != nullptr) *corner = s.corner;
    return s.tent;
}

ModResult registerActor() {
    return svc_actor->register_actor(mod_ctx, &psrTent_c::sProfile, &psrTent_c::sProcName,
                                     &psrTent_c::sActorHandle);
}
}  // namespace psr_tent
