#include "global.h"

#include <cmath>

#include "note_actor.hpp"

#include "JSystem/J3DGraphBase/J3DShape.h"
#include "JSystem/J3DGraphBase/J3DSys.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_draw_priority.h"
#include "m_Do/m_Do_lib.h"
#include "m_Do/m_Do_mtx.h"
#include "mods/service.hpp"
#include "SSystem/SComponent/c_math.h"


namespace {
constexpr const char* kArc = "PsrPack";
constexpr const char* kTex = "psr_notes.bti";
constexpr int kFrames = 4;               // four notes across the sheet
constexpr f32 kFps = 30.0f;
constexpr int kAppear = 0;
constexpr int kFadeIn = 6;
constexpr int kLife = 60;                // two seconds
constexpr f32 kRise = 110.0f;
constexpr f32 kSway = 7.0f;

f32 lifeT(int frame) { return static_cast<f32>(frame) / kFps; }
}  // namespace

psrNote_c::~psrNote_c() {
    dComIfG_resDelete(&mPhase, kArc);
}

cPhs_Step psrNote_c::create() {
    fopAcM_ct(this, psrNote_c);
    const cPhs_Step step = dComIfG_resLoad(&mPhase, kArc);
    if (step != cPhs_COMPLEATE_e) {
        return step;
    }
    mPacket.mpOwner = this;
    auto* res = static_cast<ResTIMG*>(dComIfG_getObjectRes(kArc, kTex));
    if (res != nullptr) {
        mDoLib_setResTimgObj(res, &mTex, 0, nullptr);
        mTexReady = true;
    }
    mBase = current.pos;
    mFrame = 0;
    mKind = static_cast<int>(cM_rndF(4.0f)) & 3;
    mDrift = cM_rndFX(0.6f);
    mSize = 22.0f + cM_rndF(8.0f);
    mSwayPhase = cM_rndF(6.28f);
    mDoMtx_stack_c::transS(mBase.x, mBase.y, mBase.z);
    cMtx_copy(mDoMtx_stack_c::get(), mMtx);
    fopAcM_SetMtx(this, mMtx);
    fopAcM_setCullSizeBox(this, -80.0f, -20.0f, -80.0f, 80.0f, kRise + 60.0f, 80.0f);
    attention_info.flags = 0;
    return step;
}

int psrNote_c::Delete() {
    this->~psrNote_c();
    return 1;
}

int psrNote_c::Execute() {
    if (++mFrame >= kLife) {
        fopAcM_delete(this);
    }
    return 1;
}

int psrNote_c::Draw() {
    if (!mTexReady || mFrame < kAppear) return 1;
    cXyz centre = mBase;
    dComIfGd_entryZSortXluList(&mPacket, centre);
    return 1;
}

void psrNotePacket_c::draw() {
    psrNote_c* s = mpOwner;
    if (s == nullptr || !s->mTexReady) return;

    const f32 t = lifeT(s->mFrame);
    const f32 u = static_cast<f32>(s->mFrame) / static_cast<f32>(kLife);
    if (u < 0.0f || u >= 1.0f) return;
    const f32 alpha = std::fmin(1.0f, static_cast<f32>(s->mFrame) / kFadeIn) * std::pow(1.0f - u, 0.8f);

    // up off the head, drifting aside and swaying, in the camera's frame
    cXyz world = s->mBase;
    world.y += kRise * u;
    Vec view;
    cMtx_multVec(j3dSys.getViewMtx(), &world, &view);
    view.x += s->mDrift * s->mFrame + kSway * std::sin(t * 4.0f + s->mSwayPhase);
    const f32 tilt = 0.25f * std::sin(t * 3.0f + s->mSwayPhase);   // a little rocking
    const f32 ct = std::cos(tilt), st = std::sin(tilt);

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
    GXSetZMode(GX_TRUE, GX_LEQUAL, GX_FALSE);   // hidden behind things, but writes no depth
    GXSetCullMode(GX_CULL_NONE);
    GXSetClipMode(GX_CLIP_ENABLE);
    GXSetFog(GX_FOG_NONE, 0.0f, 0.0f, 0.0f, 0.0f, COMPOUND_LITERAL(GXColor){0, 0, 0, 0});
    Mtx ident;
    MTXIdentity(ident);
    GXLoadPosMtxImm(ident, GX_PNMTX0);
    GXSetCurrentMtx(GX_PNMTX0);

    const f32 hs = s->mSize * 0.5f;
    const u8 al = static_cast<u8>(std::fmin(255.0f, alpha * 255.0f));
    const f32 s0 = static_cast<f32>(s->mKind) / kFrames, s1 = static_cast<f32>(s->mKind + 1) / kFrames;
    auto corner = [&](f32 x, f32 y, f32 tu, f32 tv) {
        GXPosition3f32(view.x + x * ct - y * st, view.y + x * st + y * ct, view.z);
        GXColor4u8(255, 255, 255, al);
        GXTexCoord2f32(tu, tv);
    };
    GXBegin(GX_QUADS, GX_VTXFMT0, 4);
    corner(-hs, hs, s0, 0.0f);
    corner(hs, hs, s1, 0.0f);
    corner(hs, -hs, s1, 1.0f);
    corner(-hs, -hs, s0, 1.0f);
    GXEnd();

    J3DShape::resetVcdVatCache();
}

namespace {
cPhs_Step psrNote_Create(void* p) { return static_cast<psrNote_c*>(p)->create(); }
int psrNote_Delete(void* p) { return static_cast<psrNote_c*>(p)->Delete(); }
int psrNote_Execute(void* p) { return static_cast<psrNote_c*>(p)->Execute(); }
int psrNote_Draw(void* p) { return static_cast<psrNote_c*>(p)->Draw(); }
int psrNote_IsDelete(void*) { return 1; }
}  // namespace

s16 psrNote_c::sProcName = -1;
ActorHandle psrNote_c::sActorHandle = -1;
const ActorProfileDesc psrNote_c::sProfile = {
    .name = PSR_NOTE_NAME,
    .priority_group = 7,
    .process_size = sizeof(psrNote_c),
    .draw_priority = fpcDwPi_OBJ_LBOX_e,
    .status = fopAcStts_UNK_0x40000_e | fopAcStts_UNK_0x4000_e | fopAcStts_CULL_e,
    .group = fopAc_ACTOR_e,
    .cull_type = fopAc_CULLBOX_CUSTOM_e,
    .create_function = psrNote_Create,
    .delete_function = psrNote_Delete,
    .execute_function = psrNote_Execute,
    .is_delete_function = psrNote_IsDelete,
    .draw_function = psrNote_Draw,
};

namespace psr_note {
void spawn(const cXyz& head, int room) {
    if (psrNote_c::sProcName < 0) return;
    fopAcM_create(psrNote_c::sProcName, 0, &head, room, nullptr, nullptr, -1);
}

ModResult registerActor() {
    return svc_actor->register_actor(mod_ctx, &psrNote_c::sProfile, &psrNote_c::sProcName,
                                     &psrNote_c::sActorHandle);
}
}  // namespace psr_note
