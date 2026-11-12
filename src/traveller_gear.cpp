// What travellers wear: their own retextured bodies (P_<model>.arc) and their packs (PsrPack.arc).
//
// Only travellers are touched. Castle Town's townsfolk use the same actor and the same vanilla
// archives, and stay exactly as they are.
//
// Bodies: the four resource lookups the townsfolk actor makes (load, unload, body model, eye
// texture animation) are redirected from MON_a.arc etc. to P_MON_a.arc etc. The shared
// animation archives (Mgeneral/Wgeneral, Mspecial/Wspecial) and "object" stay vanilla.
//
// Packs: each traveller also loads PsrPack.arc, makes its pack model in its own heap while the
// game builds it (so it is freed with it), and draws it glued to the backbone joint every frame,
// lit like the traveller (the same way the game draws the things townsfolk carry).
#include "global.h"

#include "d/actor/d_a_npc_cd2.h"
#include "d/actor/d_a_npc_passer.h"
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "f_op/f_op_actor_mng.h"
#include "m_Do/m_Do_ext.h"
#include "m_Do/m_Do_mtx.h"
#include "mods/service.hpp"
#include "mods/svc/hook.hpp"

#include "travellers.hpp"
#include "traveller_gear.hpp"

#include <array>
#include <cstdio>



namespace {

constexpr const char* kPackArc = "PsrPack";
constexpr int kBackbone = 1;   // joint 1 on every townsfolk skeleton (center, backbone, neck, head, ...)

// Per type (stock passer type id): retextured body archive, its model and eye-animation files,
// and the pack model. nullptr = not in the cast (vanilla).
struct Look {
    const char* arc;
    const char* bmd;
    const char* btp;   // nullptr: this body has no eye animation
    const char* pack;
};
const Look* lookFor(int type) {
    static const Look kLooks[30] = {
        {}, {}, {},
        {"P_MON_a", "mon_a.bmd", "mon_a.btp", "pack_mon_a.bmd"},       // 3
        {"P_MAN_b", "man_b.bmd", nullptr, "pack_man_b.bmd"},           // 4
        {},
        {"P_MAS_a", "mas_a.bmd", "mas_a.btp", "pack_mas_a.bmd"},       // 6
        {"P_MBN_a", "mbn_a.bmd", "mbn_a.btp", "pack_mbn_a.bmd"},       // 7
        {"P_MAN_a2", "man_a2.bmd", "man_a2.btp", "pack_man_a2.bmd"},   // 8
        {"P_MAD_a2", "mad_a2.bmd", "mad_a2.btp", "pack_mad_a2.bmd"},   // 9
        {},
        {"P_MON_a2", "mon_a2.bmd", "mon_a2.btp", "pack_mon_a2.bmd"},   // 11
        {},
        {"P_MAN_c2", "man_c2.bmd", "man_c2.btp", "pack_man_c2.bmd"},   // 13
        {"P_MAS_a2", "mas_a2.bmd", "mas_a2.btp", "pack_mas_a2.bmd"},   // 14
        {"P_MBN_a2", "mbn_a2.bmd", "mbn_a2.btp", "pack_mbn_a2.bmd"},   // 15
        {}, {}, {}, {},
        {"P_WON_a", "won_a.bmd", "won_a.btp", "pack_won_a.bmd"},       // 20
        {}, {}, {}, {}, {}, {},
        {"P_WON_a2", "won_a2.bmd", "won_a2.btp", "pack_won_a2.bmd"},   // 27
        {"P_WGN_a2", "wgn_a2.bmd", "wgn_a2.btp", "pack_wgn_a2.bmd"},   // 28
        {"P_WAN_b2", "wan_b2.bmd", "wan_b2.btp", "pack_wan_b2.bmd"},   // 29
    };
    if (type < 0 || type >= 30 || kLooks[type].arc == nullptr) return nullptr;
    return &kLooks[type];
}

// A traveller in the light world wearing one of our looks, or nullptr.
const Look* travellerLook(daNpcCd2_c* npc) {
    if (npc == nullptr || npc->mIsDarkWorld || !travellers::isTraveller(npc)) return nullptr;
    return lookFor(npc->m_type);
}

const char* generalArc(int type) { return type < 16 ? "Mgeneral" : "Wgeneral"; }
const char* specialArc(int type) { return type < 16 ? "Mspecial" : "Wspecial"; }

struct Gear {
    fpc_ProcID owner = fpcM_ERROR_PROCESS_ID_e;
    request_of_phase_process_class packPhase{};
    bool packLoaded = false;
    J3DModel* pack = nullptr;
};
std::array<Gear, 32> g_gear{};

Gear* gearFor(daNpcCd2_c* npc) {
    const int slot = travellers::slotIndex(npc);
    return slot >= 0 && slot < static_cast<int>(g_gear.size()) ? &g_gear[slot] : nullptr;
}

DEFINE_HOOK(&daNpcCd2_c::loadResrc, Cd2LoadResrc);
DEFINE_HOOK(&daNpcCd2_c::removeResrc, Cd2RemoveResrc);
DEFINE_HOOK(&daNpcCd2_c::getNpcMdlDataP, Cd2GetModel);
DEFINE_HOOK(&daNpcCd2_c::getTexAnmP, Cd2GetEyeAnim);
DEFINE_HOOK(&daNpcPasser_c::createHeap, PasserCreateHeap);
DEFINE_HOOK(&daNpcPasser_c::draw, PasserDraw);

HookAction onLoadResrcPre(ModContext*, void* args, void* retval, void*) {
    auto* npc = mods::arg<daNpcCd2_c*>(args, 0);
    const Look* look = travellerLook(npc);
    if (look == nullptr || retval == nullptr) return HOOK_CONTINUE;
    const int type = npc->m_type;
    int result = dComIfG_resLoad(&npc->mPhase1, look->arc);
    if (result == cPhs_COMPLEATE_e) result = dComIfG_resLoad(&npc->mPhase4, "object");
    if (result == cPhs_COMPLEATE_e) result = dComIfG_resLoad(&npc->mPhase2, generalArc(type));
    if (result == cPhs_COMPLEATE_e) result = dComIfG_resLoad(&npc->mPhase3, specialArc(type));
    if (result == cPhs_COMPLEATE_e) {
        if (Gear* g = gearFor(npc)) {
            result = dComIfG_resLoad(&g->packPhase, kPackArc);
            g->packLoaded = result == cPhs_COMPLEATE_e;
            g->owner = fopAcM_GetID(npc);
            g->pack = nullptr;
        }
    }
    *static_cast<int*>(retval) = result;
    return HOOK_SKIP_ORIGINAL;
}

HookAction onRemoveResrcPre(ModContext*, void* args, void* retval, void*) {
    auto* npc = mods::arg<daNpcCd2_c*>(args, 0);
    const Look* look = travellerLook(npc);
    if (look == nullptr || retval == nullptr) return HOOK_CONTINUE;
    const int type = npc->m_type;
    dComIfG_resDelete(&npc->mPhase1, look->arc);
    dComIfG_resDelete(&npc->mPhase4, "object");
    dComIfG_resDelete(&npc->mPhase2, generalArc(type));
    dComIfG_resDelete(&npc->mPhase3, specialArc(type));
    if (Gear* g = gearFor(npc); g != nullptr && g->owner == fopAcM_GetID(npc)) {
        if (g->packLoaded) dComIfG_resDelete(&g->packPhase, kPackArc);
        *g = Gear{};
    }
    *static_cast<int*>(retval) = 1;
    return HOOK_SKIP_ORIGINAL;
}

HookAction onGetModelPre(ModContext*, void* args, void* retval, void*) {
    auto* npc = mods::arg<daNpcCd2_c*>(args, 0);
    const Look* look = travellerLook(npc);
    if (look == nullptr || retval == nullptr) return HOOK_CONTINUE;
    *static_cast<J3DModelData**>(retval) = static_cast<J3DModelData*>(dComIfG_getObjectRes(look->arc, look->bmd));
    return HOOK_SKIP_ORIGINAL;
}

HookAction onGetEyeAnimPre(ModContext*, void* args, void* retval, void*) {
    auto* npc = mods::arg<daNpcCd2_c*>(args, 0);
    const Look* look = travellerLook(npc);
    if (look == nullptr || retval == nullptr) return HOOK_CONTINUE;
    *static_cast<J3DAnmTexPattern**>(retval) =
        look->btp != nullptr ? static_cast<J3DAnmTexPattern*>(dComIfG_getObjectRes(look->arc, look->btp)) : nullptr;
    return HOOK_SKIP_ORIGINAL;
}

// Runs inside the traveller's own heap setup, so the pack lives (and dies) with it.
void onCreateHeapPost(ModContext*, void* args, void* retval, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    if (retval == nullptr || *static_cast<int*>(retval) == 0) return;
    const Look* look = travellerLook(passer);
    Gear* g = look != nullptr ? gearFor(passer) : nullptr;
    if (g == nullptr || !g->packLoaded) return;
    auto* data = static_cast<J3DModelData*>(dComIfG_getObjectRes(kPackArc, look->pack));
    g->pack = data != nullptr ? mDoExt_J3DModel__create(data, 0x80000, 0x11000084) : nullptr;
    g->owner = fopAcM_GetID(passer);
}

void onDrawPost(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    if (travellerLook(passer) == nullptr || passer->mpMorf == nullptr) return;
    Gear* g = gearFor(passer);
    if (g == nullptr || g->pack == nullptr || g->owner != fopAcM_GetID(passer)) return;
    g_env_light.setLightTevColorType_MAJI(g->pack, &passer->tevStr);
    mDoMtx_copy(passer->mpMorf->getModel()->getAnmMtx(kBackbone), mDoMtx_stack_c::now);
    g->pack->setBaseTRMtx(mDoMtx_stack_c::now);
    mDoExt_modelUpdateDL(g->pack);
}

}  // namespace

namespace traveller_gear {

bool install() {
    return mods::hook::add_pre<Cd2LoadResrc>(onLoadResrcPre) == MOD_OK &&
           mods::hook::add_pre<Cd2RemoveResrc>(onRemoveResrcPre) == MOD_OK &&
           mods::hook::add_pre<Cd2GetModel>(onGetModelPre) == MOD_OK &&
           mods::hook::add_pre<Cd2GetEyeAnim>(onGetEyeAnimPre) == MOD_OK &&
           mods::hook::add_post<PasserCreateHeap>(onCreateHeapPost) == MOD_OK &&
           mods::hook::add_post<PasserDraw>(onDrawPost) == MOD_OK;
}

}  // namespace traveller_gear
