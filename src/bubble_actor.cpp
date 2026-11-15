#include "global.h"

#include <cmath>

#include "bubble_actor.hpp"

#include "JSystem/J3DGraphBase/J3DShape.h"
#include "JSystem/J3DGraphBase/J3DSys.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_draw_priority.h"
#include "m_Do/m_Do_lib.h"
#include "m_Do/m_Do_mtx.h"
#include "mods/service.hpp"

namespace {
constexpr const char* kArc = "PsrPack";
constexpr const char* kTex = "psr_bubbles.bti";
constexpr int kCols = 5;
constexpr f32 kCell = 96.0f, kTexW = 512.0f, kTexH = 384.0f;   // icons are 96 px cells in a 512 x 384 sheet
constexpr int kPop = 8;          // frames to pop in
constexpr int kFade = 14;        // frames to fade out
constexpr f32 kSize = 46.0f;     // world units across
constexpr f32 kNear = 1500.0f, kFar = 2300.0f;   // fades out between these distances from Link
}  // namespace

psrBubble_c::~psrBubble_c() { dComIfG_resDelete(&mPhase, kArc); }

cPhs_Step psrBubble_c::create() {
    fopAcM_ct(this, psrBubble_c);
    const cPhs_Step step = dComIfG_resLoad(&mPhase, kArc);
    if (step != cPhs_COMPLEATE_e) return step;
    mPacket.mpOwner = this;
    if (auto* res = static_cast<ResTIMG*>(dComIfG_getObjectRes(kArc, kTex))) {
        mDoLib_setResTimgObj(res, &mTex, 0, nullptr);
        mTexReady = true;
    }
    mIcon = static_cast<int>(fopAcM_GetParam(this) & 0xFF) % psr_bubble::kCount;
    mLife = psr_bubble::isThought(mIcon) ? 90 : 70;
    mFrame = 0;
    mDoMtx_stack_c::transS(current.pos.x, current.pos.y, current.pos.z);
    cMtx_copy(mDoMtx_stack_c::get(), mMtx);
    fopAcM_SetMtx(this, mMtx);
    fopAcM_setCullSizeBox(this, -60.0f, -60.0f, -60.0f, 60.0f, 60.0f, 60.0f);
    attention_info.flags = 0;
    return step;
}

int psrBubble_c::Delete() {
    this->~psrBubble_c();
    return 1;
}

int psrBubble_c::Execute() {
    if (++mFrame >= mLife) fopAcM_delete(this);
    mDoMtx_stack_c::transS(current.pos.x, current.pos.y, current.pos.z);
    cMtx_copy(mDoMtx_stack_c::get(), mMtx);
    return 1;
}

int psrBubble_c::Draw() {
    if (!mTexReady) return 1;
    dComIfGd_entryZSortXluList(&mPacket, current.pos);
    return 1;
}

void psrBubblePacket_c::draw() {
    psrBubble_c* s = mpOwner;
    if (s == nullptr || !s->mTexReady) return;
    const int f = s->mFrame;
    // pop: grows to a touch too big, settles; fades at the end; and with distance from Link
    f32 scale = 1.0f;
    if (f < kPop) {
        const f32 u = static_cast<f32>(f + 1) / kPop;
        scale = u < 0.7f ? u / 0.7f * 1.15f : 1.15f - 0.15f * (u - 0.7f) / 0.3f;
    }
    f32 alpha = std::fmin(1.0f, static_cast<f32>(s->mLife - f) / kFade);
    if (auto* link = dComIfGp_getPlayer(0)) {
        const f32 d = s->current.pos.abs(link->current.pos);
        alpha *= std::fmax(0.0f, std::fmin(1.0f, (kFar - d) / (kFar - kNear)));
    }
    if (alpha <= 0.01f) return;
    const f32 bob = 1.5f * std::sin(static_cast<f32>(f) * 0.12f);

    Vec view;
    cMtx_multVec(j3dSys.getViewMtx(), &s->current.pos, &view);
    view.y += bob;

    j3dSys.reinitGX();
    GXSetNumIndStages(0);
    GXClearVtxDesc();
    GXSetVtxDesc(GX_VA_POS, GX_DIRECT);
    GXSetVtxDesc(GX_VA_CLR0, GX_DIRECT);
    GXSetVtxDesc(GX_VA_TEX0, GX_DIRECT);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XYZ, GX_F32, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
    GXSetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
    GXLoadTexObj(&s->mTex, GX_TEXMAP0);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0A0, GX_FALSE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetNumTexGens(1);
    GXSetTexCoordGen2(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY, GX_FALSE, GX_PTIDENTITY);
    GXSetNumTevStages(1);
    GXSetTevSwapMode(GX_TEVSTAGE0, GX_TEV_SWAP0, GX_TEV_SWAP0);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_MODULATE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetZCompLoc(GX_TRUE);
    GXSetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);
    GXSetCullMode(GX_CULL_NONE);
    GXSetClipMode(GX_CLIP_ENABLE);
    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, COMPOUND_LITERAL(GXColor){0, 0, 0, 0});
    Mtx ident;
    MTXIdentity(ident);
    GXLoadPosMtxImm(ident, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);

    const f32 hs = kSize * 0.5f * scale;
    const u8 al = static_cast<u8>(std::fmin(255.0f, alpha * 255.0f));
    const int cx = s->mIcon % kCols, cy = s->mIcon / kCols;
    const f32 u0 = cx * kCell / kTexW, u1 = (cx + 1) * kCell / kTexW;
    const f32 v0 = cy * kCell / kTexH, v1 = (cy + 1) * kCell / kTexH;
    auto corner = [&](f32 x, f32 y, f32 tu, f32 tv) {
        GXPosition3f32(view.x + x, view.y + y, view.z);
        GXColor4u8(255, 255, 255, al);
        GXTexCoord2f32(tu, tv);
    };
    // the bubble's tail points down at the head, so it sits above with its bottom edge at the anchor
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    corner(-hs, 2.0f * hs, u0, v0);
    corner(hs, 2.0f * hs, u1, v0);
    corner(hs, 0.0f, u1, v1);
    corner(-hs, 0.0f, u0, v1);
    GXEnd();
    J3DShape::resetVcdVatCache();
}

namespace {
cPhs_Step psrBubble_Create(void* p) { return static_cast<psrBubble_c*>(p)->create(); }
int psrBubble_Delete(void* p) { return static_cast<psrBubble_c*>(p)->Delete(); }
int psrBubble_Execute(void* p) { return static_cast<psrBubble_c*>(p)->Execute(); }
int psrBubble_Draw(void* p) { return static_cast<psrBubble_c*>(p)->Draw(); }
int psrBubble_IsDelete(void*) { return 1; }
}  // namespace

s16 psrBubble_c::sProcName = -1;
ActorHandle psrBubble_c::sActorHandle = -1;
const ActorProfileDesc psrBubble_c::sProfile = {
    .name = PSR_BUBBLE_NAME,
    .priority_group = 7,
    .process_size = sizeof(psrBubble_c),
    .draw_priority = fpcDwPi_OBJ_LBOX_e,
    .status = fopAcStts_UNK_0x40000_e | fopAcStts_UNK_0x4000_e | fopAcStts_CULL_e,
    .group = fopAc_ACTOR_e,
    .cull_type = fopAc_CULLBOX_CUSTOM_e,
    .create_function = psrBubble_Create,
    .delete_function = psrBubble_Delete,
    .execute_function = psrBubble_Execute,
    .is_delete_function = psrBubble_IsDelete,
    .draw_function = psrBubble_Draw,
};

namespace psr_bubble {
bool isThought(int icon) {
    switch (icon) {
    case kSleepy: case kHungry: case kThirsty: case kTired: case kMusic: case kCook: case kTravel:
    case kFire: case kHeal: return true;
    default: return false;
    }
}
fpc_ProcID spawn(const cXyz& pos, int room, int icon) {
    if (psrBubble_c::sProcName < 0) return fpcM_ERROR_PROCESS_ID_e;
    return fopAcM_create(psrBubble_c::sProcName, static_cast<u32>(icon), &pos, room, nullptr, nullptr, -1);
}
bool follow(fpc_ProcID id, const cXyz& pos) {
    if (id == fpcM_ERROR_PROCESS_ID_e) return false;
    auto* b = static_cast<psrBubble_c*>(fopAcM_SearchByID(id));
    if (b == nullptr) return false;
    b->current.pos = pos;
    return true;
}
void remove(fpc_ProcID id) {
    if (id == fpcM_ERROR_PROCESS_ID_e) return;
    if (auto* b = fopAcM_SearchByID(id)) fopAcM_delete(b);
}
ModResult registerActor() {
    return svc_actor->register_actor(mod_ctx, &psrBubble_c::sProfile, &psrBubble_c::sProcName,
                                     &psrBubble_c::sActorHandle);
}
}  // namespace psr_bubble
