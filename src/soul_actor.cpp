#include "global.h"

#include <cmath>

#include "soul_actor.hpp"

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
constexpr const char* kTex = "psr_soul.bti";
constexpr int kFrames = 4;               // flipbook frames across the sheet
constexpr f32 kFps = 30.0f;
constexpr int kAppear = 10;              // a third of a second into the puff
constexpr int kFadeIn = 10;
constexpr int kLife = 105;               // gone three and a half seconds after the puff
constexpr f32 kWidth = 70.0f;            // the sprite (128 x 256 texels a frame)
constexpr f32 kHeight = 140.0f;
constexpr f32 kStartY = 70.0f;           // starts with its tail at the ground, over the body
constexpr f32 kRise = 450.0f;
constexpr f32 kSway = 18.0f;
constexpr f32 kWiggle = 7.0f;            // tail flicker speed (radians a second, as in the mock-up)

f32 lifeT(int frame) { return static_cast<f32>(frame) / kFps; }
}  // namespace

psrSoul_c::~psrSoul_c() {
    dComIfG_resDelete(&mPhase, kArc);
}

cPhs_Step psrSoul_c::create() {
    fopAcM_ct(this, psrSoul_c);
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
    mDoMtx_stack_c::transS(mBase.x, mBase.y, mBase.z);
    cMtx_copy(mDoMtx_stack_c::get(), mMtx);
    fopAcM_SetMtx(this, mMtx);
    fopAcM_setCullSizeBox(this, -120.0f, -20.0f, -120.0f, 120.0f, kStartY + kRise + kHeight, 120.0f);
    attention_info.flags = 0;
    return step;
}

int psrSoul_c::Delete() {
    this->~psrSoul_c();
    return 1;
}

int psrSoul_c::Execute() {
    if (++mFrame >= kLife) {
        fopAcM_delete(this);
    }
    return 1;
}

int psrSoul_c::Draw() {
    if (!mTexReady || mFrame < kAppear) return 1;
    cXyz centre = mBase;
    centre.y += kStartY;
    dComIfGd_entryZSortXluList(&mPacket, centre);
    return 1;
}

void psrSoulPacket_c::draw() {
    psrSoul_c* s = mpOwner;
    if (s == nullptr || !s->mTexReady) return;

    const f32 t = lifeT(s->mFrame);
    const f32 u = static_cast<f32>(s->mFrame - kAppear) / static_cast<f32>(kLife - kAppear);
    if (u < 0.0f || u >= 1.0f) return;
    const f32 fadeIn = std::fmin(1.0f, static_cast<f32>(s->mFrame - kAppear) / kFadeIn);
    const f32 alpha = fadeIn * std::pow(1.0f - u, 1.2f);

    // Where it is now, in the camera's frame: up from the body, swaying side to side.
    cXyz world = s->mBase;
    world.y += kStartY + kRise * u;
    Vec view;
    cMtx_multVec(j3dSys.getViewMtx(), &world, &view);
    view.x += kSway * std::sin(t * 3.0f);

    // Which two flipbook frames, and how far between them.
    const f32 phase = t * kWiggle / (2.0f * 3.14159265f) * kFrames;
    const int f0 = static_cast<int>(std::floor(phase)) % kFrames;
    const int f1 = (f0 + 1) % kFrames;
    const f32 mix = phase - std::floor(phase);

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

    const f32 hw = kWidth * 0.5f, hh = kHeight * 0.5f;
    auto quad = [&](int frame, f32 a) {
        if (a <= 0.004f) return;
        const u8 al = static_cast<u8>(std::fmin(255.0f, a * 255.0f));
        const f32 s0 = static_cast<f32>(frame) / kFrames, s1 = static_cast<f32>(frame + 1) / kFrames;
        GXBegin(GX_QUADS, GX_VTXFMT0, 4);
        GXPosition3f32(view.x - hw, view.y + hh, view.z); GXColor4u8(255, 255, 255, al); GXTexCoord2f32(s0, 0.0f);
        GXPosition3f32(view.x + hw, view.y + hh, view.z); GXColor4u8(255, 255, 255, al); GXTexCoord2f32(s1, 0.0f);
        GXPosition3f32(view.x + hw, view.y - hh, view.z); GXColor4u8(255, 255, 255, al); GXTexCoord2f32(s1, 1.0f);
        GXPosition3f32(view.x - hw, view.y - hh, view.z); GXColor4u8(255, 255, 255, al); GXTexCoord2f32(s0, 1.0f);
        GXEnd();
    };
    // cross-fade the two frames so the flicker runs smooth
    // (the second one's strength is picked so that together they show exactly `alpha`)
    const f32 a0 = alpha * (1.0f - mix);
    quad(f0, a0);
    quad(f1, a0 < 0.999f ? alpha * mix / (1.0f - a0) : 0.0f);

    J3DShape::resetVcdVatCache();
}

namespace {
cPhs_Step psrSoul_Create(void* p) { return static_cast<psrSoul_c*>(p)->create(); }
int psrSoul_Delete(void* p) { return static_cast<psrSoul_c*>(p)->Delete(); }
int psrSoul_Execute(void* p) { return static_cast<psrSoul_c*>(p)->Execute(); }
int psrSoul_Draw(void* p) { return static_cast<psrSoul_c*>(p)->Draw(); }
int psrSoul_IsDelete(void*) { return 1; }
}  // namespace

s16 psrSoul_c::sProcName = -1;
ActorHandle psrSoul_c::sActorHandle = -1;
const ActorProfileDesc psrSoul_c::sProfile = {
    .name = PSR_SOUL_NAME,
    .priority_group = 7,
    .process_size = sizeof(psrSoul_c),
    .draw_priority = fpcDwPi_OBJ_LBOX_e,
    .status = fopAcStts_UNK_0x40000_e | fopAcStts_UNK_0x4000_e | fopAcStts_CULL_e,
    .group = fopAc_ACTOR_e,
    .cull_type = fopAc_CULLBOX_CUSTOM_e,
    .create_function = psrSoul_Create,
    .delete_function = psrSoul_Delete,
    .execute_function = psrSoul_Execute,
    .is_delete_function = psrSoul_IsDelete,
    .draw_function = psrSoul_Draw,
};

namespace psr_soul {
void spawn(const cXyz& pos, int room) {
    if (psrSoul_c::sProcName < 0) return;
    fopAcM_create(psrSoul_c::sProcName, 0, &pos, room, nullptr, nullptr, -1);
}

ModResult registerActor() {
    return svc_actor->register_actor(mod_ctx, &psrSoul_c::sProfile, &psrSoul_c::sProcName,
                                     &psrSoul_c::sActorHandle);
}
}  // namespace psr_soul
