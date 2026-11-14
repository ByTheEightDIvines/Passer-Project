// The old women's freeze spell (see freeze.hpp).
#include "global.h"

#include "freeze.hpp"

#include "d/d_cc_d.h"
#include "d/d_com_inf_game.h"
#include "d/d_particle.h"
#include "f_op/f_op_actor_mng.h"
#include "m_Do/m_Do_audio.h"
#include "m_Do/m_Do_ext.h"
#include "JSystem/JParticle/JPAEmitter.h"
#include "JSystem/JParticle/JPAEmitterManager.h"
#include "JSystem/JParticle/JPAResourceManager.h"
#include "JSystem/JKernel/JKRSolidHeap.h"
#include "mods/service.hpp"
#include "mods/svc/hook.hpp"

#include "travellers.hpp"

#include <array>
#include <cstring>

namespace {

constexpr u8 kOurPackSlot = 2;              // the particle manager's slots: 0 always-loaded, 1 the stage's, 2 ours
constexpr u16 kIceBlock = 0x881A;           // LK_FREEZ_A: the ice round Link
constexpr u16 kIceBreak[2] = {0x8818, 0x8819};   // LK_FREEZEND_A/B: it breaking away
constexpr int kFrozenFrames = 180;          // six seconds
constexpr f32 kLinkHeight = 140.0f;         // what the effect is sized for (Link: 1 x 1.8 x 1)
const GXColorS10 kFrozenTint = {25, 30, 40, 0};   // Link's own frozen tint

JKRSolidHeap* g_heap = nullptr;
JPAResourceManager* g_pack = nullptr;
JPAEmitterManager* g_patched = nullptr;     // the manager we gave a third slot to

// The particle manager has two pack slots; give it a third (once per manager) holding ours.
bool ensureSlot() {
    JPAEmitterManager* em = dPa_control_c::mEmitterMng;
    if (em == nullptr || g_pack == nullptr) return false;
    if (em == g_patched) return true;
    if (em->ridMax < kOurPackSlot + 1) {
        auto** slots = static_cast<JPAResourceManager**>(g_heap->alloc(sizeof(JPAResourceManager*) * 4, 4));
        if (slots == nullptr) return false;
        for (int i = 0; i < 4; ++i) slots[i] = i < em->ridMax ? em->pResMgrAry[i] : nullptr;
        em->pResMgrAry = slots;
        em->ridMax = kOurPackSlot + 1;
    }
    em->pResMgrAry[kOurPackSlot] = g_pack;
    g_patched = em;
    return true;
}

// The game's own draw-group choice for an effect (dPa_group_id_change, a file-local helper).
u8 drawGroup(u32 work) {
    u8 g = 0;
    if (work & 1) g = 0;
    else if (work & 2) g = 1;
    else if (work & 4) g = 2;
    else if (work & 8) g = 3;
    else if (work & 0x4000) g = 4;
    else if (work & 0x8000) g = 5;
    if (work & 0x10) g = static_cast<u8>(g + 8);   // the same groups, drawn later (0..3 -> 8..11)
    return g;
}

JPABaseEmitter* emit(u16 id, const cXyz& pos, s16 yaw, const cXyz& scale) {
    if (!ensureSlot()) return nullptr;
    const u32 work = g_pack->getResUserWork(id);
    JGeometry::TVec3<f32> at(pos.x, pos.y, pos.z);
    JPABaseEmitter* e = dPa_control_c::mEmitterMng->createSimpleEmitterID(at, id, drawGroup(work), kOurPackSlot,
                                                                          nullptr, nullptr);
    if (e == nullptr) return nullptr;
    e->setGlobalRotation(JGeometry::TVec3<s16>(0, yaw, 0));
    e->setGlobalScale(JGeometry::TVec3<f32>(scale.x, scale.y, scale.z));
    e->setGlobalAlpha(0xFF);
    return e;
}

// --- frozen monsters --------------------------------------------------------------------------
// While frozen the monster's own update doesn't run at all (it stands exactly as it was, drawn as
// usual, tinted like frozen Link). Its own collision isn't registered then, so the ice carries one:
// a blow from Link or a traveller on it shatters the ice and the monster with it.
struct Frozen {
    fpc_ProcID id = fpcM_ERROR_PROCESS_ID_e;
    int frames = 0;
    JPABaseEmitter* ice = nullptr;
    cXyz centre;
    cXyz scale;
    f32 height = 0.0f;
    GXColorS10 tintBefore{};
    dCcD_Stts stts;
    dCcD_Cyl cyl;
    bool cylReady = false;
};
std::array<Frozen, 8> g_frozen{};

const dCcD_SrcCyl kIceCyl = {
    {
        {0x0, {{0x0, 0x0, 0x0}, {0xD8FBFDFF, 0x1B}, 0x0}},   // takes Link's and the travellers' blows
        {dCcD_SE_NONE, 0x0, 0x0, 0x0, 0x0},
        {dCcD_SE_HARD_BODY, 0x0, 0x0, 0x0, 0x0},
        {0x0},
    },
    {{{0.0f, 0.0f, 0.0f}, 60.0f, 140.0f}},
};

Frozen* frozenFor(const fopAc_ac_c* a) {
    if (a == nullptr) return nullptr;
    const fpc_ProcID id = fopAcM_GetID(a);
    for (Frozen& f : g_frozen) {
        if (f.id == id) return &f;
    }
    return nullptr;
}

void breakFree(fopAc_ac_c* enemy, Frozen& f, bool shattered) {
    if (f.ice != nullptr) f.ice->becomeInvalidEmitter();
    for (u16 id : kIceBreak) emit(id, f.centre, enemy->shape_angle.y, f.scale);
    enemy->tevStr.TevColor = f.tintBefore;
    const s8 reverb = dComIfGp_getReverb(fopAcM_GetRoomNo(enemy));
    if (shattered) {
        mDoAud_seStart(Z2SE_OBJ_ICE_BREAK, &f.centre, 0, reverb);
        fopAcM_createDisappear(enemy, &enemy->current.pos, 10, 0, 0);
        travellers::noteBlow(enemy, "frozen monster shattered");
        fopAcM_delete(enemy);
    } else {
        mDoAud_seStart(Z2SE_AL_FREEZE_RECOVER, &f.centre, 0, reverb);
    }
    f = Frozen{};
}

DEFINE_HOOK_SYMBOL("fopAc_Execute", int(void*), ActorExecute);

HookAction onActorExecutePre(ModContext*, void* args, void* retval, void*) {
    auto* ac = mods::arg<fopAc_ac_c*>(args, 0);
    Frozen* f = frozenFor(ac);
    if (f == nullptr) return HOOK_CONTINUE;
    if (f->cylReady && f->cyl.ChkTgHit()) {
        fopAc_ac_c* by = f->cyl.GetTgHitAc();
        f->cyl.ResetTgHit();
        if (by != nullptr && (by == dComIfGp_getPlayer(0) || travellers::isTraveller(by))) {
            breakFree(ac, *f, true);
            if (retval != nullptr) *static_cast<int*>(retval) = 1;
            return HOOK_SKIP_ORIGINAL;
        }
    }
    if (--f->frames <= 0) {
        breakFree(ac, *f, false);
        return HOOK_CONTINUE;   // thawed: back to its own update this very frame
    }
    ac->tevStr.TevColor = kFrozenTint;
    // the ice's own collision, round the monster
    if (!f->cylReady) {
        f->stts.Init(0xFF, 0xFF, ac);
        f->cyl.Set(kIceCyl);
        f->cyl.SetStts(&f->stts);
        f->cylReady = true;
    }
    f->cyl.SetC(ac->current.pos);
    f->cyl.SetR(f->height * 0.42f);
    f->cyl.SetH(f->height);
    dComIfG_Ccsp()->Set(&f->cyl);
    if (retval != nullptr) *static_cast<int*>(retval) = 1;
    return HOOK_SKIP_ORIGINAL;   // frozen: its own update doesn't run
}

}  // namespace

namespace psr_freeze {

void loadEffects() {
    if (g_pack != nullptr) return;
    const void* src = dComIfG_getObjectRes("PsrPack", "psr_fx.jpc");
    if (src == nullptr) return;
    constexpr u32 size = 15456;   // psr_fx.jpc, exactly: Link's freeze (Snowpeak's pack) and the chest puff (dungeons')
    if (std::memcmp(src, "JPAC2-10", 8) != 0) return;
    g_heap = mDoExt_createSolidHeapFromSystem(0x10000, 0x20);
    if (g_heap == nullptr) return;
    void* copy = g_heap->alloc(size, 0x20);   // ours to keep: the effects point into it
    if (copy == nullptr) return;
    std::memcpy(copy, src, size);
    g_pack = JKR_NEW_ARGS(g_heap, 0) JPAResourceManager(copy, g_heap);
}

bool isFrozen(const fopAc_ac_c* actor) { return frozenFor(actor) != nullptr; }

bool effect(u16 id, const cXyz& pos, s16 yaw, const cXyz& scale) { return emit(id, pos, yaw, scale) != nullptr; }

bool freeze(fopAc_ac_c* enemy) {
    if (enemy == nullptr || isFrozen(enemy) || !ensureSlot()) return false;
    Frozen* slot = nullptr;
    for (Frozen& f : g_frozen) {
        if (f.id == fpcM_ERROR_PROCESS_ID_e || fopAcM_SearchByID(f.id) == nullptr) {
            slot = &f;
            break;
        }
    }
    if (slot == nullptr) return false;
    *slot = Frozen{};
    // sized to the monster: its eyes' height (Link's effect is made for his 140)
    const f32 h = std::max(60.0f, enemy->eyePos.y - enemy->current.pos.y + 25.0f);
    const f32 s = h / kLinkHeight;
    slot->id = fopAcM_GetID(enemy);
    slot->frames = kFrozenFrames;
    slot->height = h;
    slot->scale.set(s, 1.8f * s, s);
    slot->centre.set(enemy->current.pos.x, enemy->current.pos.y + h * 0.45f, enemy->current.pos.z);
    slot->tintBefore = enemy->tevStr.TevColor;
    slot->ice = emit(kIceBlock, slot->centre, enemy->shape_angle.y, slot->scale);
    mDoAud_seStart(Z2SE_AL_FREEZE, &slot->centre, 0, dComIfGp_getReverb(fopAcM_GetRoomNo(enemy)));
    return true;
}

bool install() { return mods::hook::add_pre<ActorExecute>(onActorExecutePre) == MOD_OK; }

}  // namespace psr_freeze
