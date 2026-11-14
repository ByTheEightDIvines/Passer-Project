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
#include "d/d_particle_name.h"
#include "f_op/f_op_actor_mng.h"
#include "m_Do/m_Do_ext.h"
#include "m_Do/m_Do_mtx.h"
#include "mods/service.hpp"
#include "mods/svc/hook.hpp"

#include "travellers.hpp"
#include "traveller_gear.hpp"
#include "camp_cook.hpp"
#include "fighter_anim.hpp"

#include <array>
#include <cstdio>
#include <cstring>



namespace {

constexpr const char* kPackArc = "PsrPack";
constexpr int kBackbone = 1;
constexpr int kHead = 3;       // head joint on the men's skeleton
constexpr int kHandR = 11;     // right hand   // joint 1 on every townsfolk skeleton (center, backbone, neck, head, ...)
constexpr int kWaist = 12;     // joint 12 on both the men's and the women's skeleton

// Per type (stock passer type id): retextured body archive, its model and eye-animation files,
// and the pack model. nullptr = not in the cast (vanilla).
struct Look {
    const char* arc;
    const char* bmd;
    const char* btp;   // nullptr: this body has no eye animation
    const char* pack;
};

// Fighters: belt (glued to the waist) and the weapon in its scabbard. The sheath matrix places the
// weapon's grip frame relative to the waist joint (fitted on the bind pose, approved renders).
struct Kit {
    const char* model;
    const char* belt;
    const char* weapon;
    Mtx sheath;
};
const Kit kKits[] = {
#include "traveller_kits.inc"
};
const Kit* kitFor(const Look* look) {
    if (look == nullptr) return nullptr;
    for (const Kit& k : kKits) {
        if (std::strcmp(k.model, look->arc + 2) == 0) return &k;   // "P_MON_a" -> "MON_a"
    }
    return nullptr;
}
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

// The camp cook (camp_cook.cpp): #9's own model and clothes, straight from the game.
const Look kCookLook = {"MAD_a2", "mad_a2.bmd", "mad_a2.btp", nullptr};

// A traveller in the light world wearing one of our looks (or the cook), or nullptr.
const Look* travellerLook(daNpcCd2_c* npc) {
    if (npc != nullptr && !npc->mIsDarkWorld && cook::isBody(npc)) return &kCookLook;
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
    J3DModel* belt = nullptr;
    J3DModel* weapon = nullptr;
    J3DModel* seat = nullptr;     // a log round, drawn under it while it sits at a camp fire
    J3DModel* hat = nullptr;      // the cook's chef's hat
    J3DModel* spoon = nullptr;    // the cook's wooden spoon
    bool sitting = false;
    J3DModel* light = nullptr;    // its torch or lantern (copies of Rusl's torch and Link's lantern)
    bool torch = false;           // which of the two
    bool carrying = false;
    bool lit = false;             // its point light is registered
    LIGHT_INFLUENCE glow{};
    u32 flame = 0;                // the flame particle
};
std::array<Gear, 32> g_gear{};

Gear g_cookGear;

Gear* gearFor(daNpcCd2_c* npc) {
    if (cook::isBody(npc)) return &g_cookGear;
    const int slot = travellers::slotIndex(npc);
    return slot >= 0 && slot < static_cast<int>(g_gear.size()) ? &g_gear[slot] : nullptr;
}

// Which look (and gear slot) each traveller actor loaded, remembered by its process ID. Removal must
// undo exactly what loading did, but by the time the game removes a dying or despawned traveller
// its slot has already been let go (so it no longer counts as a traveller); without this the game's
// own removal ran instead and released the wrong archives, one of which another NPC was still
// using (a crash in that NPC's eye animation a little later).
struct Loaded {
    fpc_ProcID id = fpcM_ERROR_PROCESS_ID_e;
    const Look* look = nullptr;
    Gear* gear = nullptr;
};
std::array<Loaded, 48> g_loaded{};

Loaded* loadedFor(fpc_ProcID id) {
    for (auto& l : g_loaded) {
        if (l.id == id && l.look != nullptr) return &l;
    }
    return nullptr;
}
void rememberLoaded(fpc_ProcID id, const Look* look, Gear* gear) {
    Loaded* l = loadedFor(id);
    if (l == nullptr) {
        for (auto& e : g_loaded) {
            if (e.look == nullptr) { l = &e; break; }
        }
    }
    if (l == nullptr) return;   // (never more than a few dozen at once)
    *l = {id, look, gear};
}

// Torch: Rusl's own, held the way he holds it (his hand's frame turned onto ours: a quarter turn
// about x); the flame sits on its tip. Lantern: hangs upright from the hand by its handle.
const Mtx kTorchGrip = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 1.0f, 0.0f}, {0.0f, -1.0f, 0.0f, 0.0f}};
const Vec kTorchTip = {11.0f, 76.0f, 4.0f};      // in the hand's frame
constexpr f32 kLanternDrop = 47.0f;              // handle (top) to the model's origin (its base)
constexpr f32 kLanternFlame = 24.0f;             // the flame, this far below the hand

void cutLight(Gear& g) {
    if (g.lit) dKy_plight_cut(&g.glow);
    g.lit = false;
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
    Gear* g = gearFor(npc);
    if (result == cPhs_COMPLEATE_e) {
        if (g != nullptr) {
            result = dComIfG_resLoad(&g->packPhase, kPackArc);
            g->packLoaded = result == cPhs_COMPLEATE_e;
            g->owner = fopAcM_GetID(npc);
            g->pack = nullptr;
        }
    }
    rememberLoaded(fopAcM_GetID(npc), look, g);
    *static_cast<int*>(retval) = result;
    return HOOK_SKIP_ORIGINAL;
}

HookAction onRemoveResrcPre(ModContext*, void* args, void* retval, void*) {
    auto* npc = mods::arg<daNpcCd2_c*>(args, 0);
    if (npc == nullptr || retval == nullptr) return HOOK_CONTINUE;
    // what this actor loaded (still known after its traveller slot is gone)
    Loaded* rec = loadedFor(fopAcM_GetID(npc));
    const Look* look = rec != nullptr ? rec->look : travellerLook(npc);
    if (look == nullptr) return HOOK_CONTINUE;   // not one of ours: the game's own
    Gear* g = rec != nullptr ? rec->gear : gearFor(npc);
    if (rec != nullptr) *rec = Loaded{};
    const int type = npc->m_type;
    dComIfG_resDelete(&npc->mPhase1, look->arc);
    dComIfG_resDelete(&npc->mPhase4, "object");
    dComIfG_resDelete(&npc->mPhase2, generalArc(type));
    dComIfG_resDelete(&npc->mPhase3, specialArc(type));
    if (g != nullptr && g->owner == fopAcM_GetID(npc)) {
        cutLight(*g);
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
    auto make = [](const char* file) -> J3DModel* {
        if (file == nullptr) return nullptr;
        auto* data = static_cast<J3DModelData*>(dComIfG_getObjectRes(kPackArc, file));
        return data != nullptr ? mDoExt_J3DModel__create(data, 0x80000, 0x11000084) : nullptr;
    };
    g->pack = make(look->pack);
    if (cook::isBody(passer)) {
        // the cook: his hat and spoon, no pack, belt or weapon
        g->hat = make("psr_chefhat.bmd");
        g->spoon = make("psr_spoon.bmd");
        g->owner = fopAcM_GetID(passer);
        return;
    }
    g->seat = make("psr_seat.bmd");
    g->sitting = false;
    // fighters carry a torch, the others half a torch, half a lantern
    const int slot = travellers::slotIndex(passer);
    g->torch = travellers::isFighter(passer) || (slot & 1) != 0;
    g->light = make(g->torch ? "psr_torch.bmd" : "psr_lantern.bmd");
    g->carrying = false;
    g->lit = false;
    if (const Kit* kit = kitFor(look)) {
        g->belt = make(kit->belt);
        g->weapon = make(kit->weapon);
    }
    g->owner = fopAcM_GetID(passer);
}

void onDrawPost(ModContext*, void* args, void*, void*) {
    auto* passer = mods::arg<daNpcPasser_c*>(args, 0);
    if (travellerLook(passer) == nullptr || passer->mpMorf == nullptr) return;
    Gear* g = gearFor(passer);
    if (g == nullptr || g->owner != fopAcM_GetID(passer)) return;
    J3DModel* body = passer->mpMorf->getModel();
    auto drawOn = [&](J3DModel* m, int joint, const Mtx* offset) {
        if (m == nullptr) return;
        g_env_light.setLightTevColorType_MAJI(m, &passer->tevStr);
        mDoMtx_copy(body->getAnmMtx(joint), mDoMtx_stack_c::now);
        if (offset != nullptr) mDoMtx_stack_c::concat(*offset);
        m->setBaseTRMtx(mDoMtx_stack_c::now);
        mDoExt_modelUpdateDL(m);
    };
    if (g->seat != nullptr && g->sitting && travellers::campInfo(passer, nullptr, nullptr, nullptr)) {
        // the vanilla stools put the seat 37.2 (men) / 55.9 (women) up, scaled like the person
        const f32 scale = Cd2_HIO_objScale(passer->m_type);
        const f32 height = (passer->m_type < 16 ? 37.2f : 55.9f) * scale;
        mDoMtx_stack_c::transS(passer->current.pos.x, passer->current.pos.y, passer->current.pos.z);
        mDoMtx_stack_c::YrotM(passer->shape_angle.y);
        J3DModel* seat = g->seat;
        mDoMtx_stack_c::scaleM(scale * 1.1f, height, scale * 1.1f);
        seat->setBaseTRMtx(mDoMtx_stack_c::now);
        g_env_light.setLightTevColorType_MAJI(seat, &passer->tevStr);
        mDoExt_modelUpdateDL(seat);
    }
    if (g->hat != nullptr) {
        drawOn(g->hat, kHead, nullptr);   // made in his head's own space
    }
    Mtx held;
    if (g->spoon != nullptr && cook::spoonOut() && fighter_anim::heldMtx(passer, held)) {
        // in his right hand, held like the fighters hold their maces (the spoon is modelled along +X)
        mDoMtx_copy(held, mDoMtx_stack_c::now);
        mDoMtx_stack_c::YrotM(-0x4000);
        g->spoon->setBaseTRMtx(mDoMtx_stack_c::now);
        g_env_light.setLightTevColorType_MAJI(g->spoon, &passer->tevStr);
        mDoExt_modelUpdateDL(g->spoon);
    }
    Mtx hand;
    if (g->light != nullptr && g->carrying && fighter_anim::leftHandMtx(passer, hand)) {
        if (g->torch) {
            mDoMtx_copy(hand, mDoMtx_stack_c::now);
            mDoMtx_stack_c::concat(kTorchGrip);
        } else {
            mDoMtx_stack_c::transS(hand[0][3], hand[1][3] - kLanternDrop, hand[2][3]);
            mDoMtx_stack_c::YrotM(passer->shape_angle.y);
        }
        g->light->setBaseTRMtx(mDoMtx_stack_c::now);
        g_env_light.setLightTevColorType_MAJI(g->light, &passer->tevStr);
        mDoExt_modelUpdateDL(g->light);
    }
    drawOn(g->pack, kBackbone, nullptr);
    drawOn(g->belt, kWaist, nullptr);
    Mtx inHand;
    if (g->weapon != nullptr && fighter_anim::weaponMtx(passer, inHand)) {
        // out of its sheath, in the right hand (fighter_anim.cpp)
        g_env_light.setLightTevColorType_MAJI(g->weapon, &passer->tevStr);
        g->weapon->setBaseTRMtx(inHand);
        mDoExt_modelUpdateDL(g->weapon);
    } else if (const Kit* kit = kitFor(travellerLook(passer))) {
        drawOn(g->weapon, kWaist, &kit->sheath);
    }
}

}  // namespace

namespace traveller_gear {

bool sitting(int slot) {
    return slot >= 0 && slot < static_cast<int>(g_gear.size()) && g_gear[slot].sitting && g_gear[slot].seat != nullptr;
}

bool sit(daNpcPasser_c* passer, bool on) {
    Gear* g = travellerLook(passer) != nullptr ? gearFor(passer) : nullptr;
    if (g == nullptr || g->owner != fopAcM_GetID(passer)) on = false;
    if (on) {
        // the townsfolk "sit and wait" loop (the heavyset men have their own); asked for with no
        // held object so the game doesn't swap in its standing fallback
        const bool stocky = passer->m_type == 1 || passer->m_type == 9;   // the heavyset men (MAD)
        auto* anm = static_cast<J3DAnmTransformKey*>(passer->getAnmP(stocky ? 17 : 14, 0));
        if (anm == nullptr) on = false;
        else passer->setAnm(anm, 1.0f, 12.0f, J3DFrameCtrl::EMode_LOOP, 0, -1);
    }
    if (g != nullptr) g->sitting = on;
    return on;
}

bool lightTick(daNpcPasser_c* passer, bool wanted) {
    Gear* g = travellerLook(passer) != nullptr ? gearFor(passer) : nullptr;
    if (g == nullptr || g->owner != fopAcM_GetID(passer)) return false;
    Mtx hand;
    g->carrying = wanted && g->light != nullptr && fighter_anim::leftHandMtx(passer, hand);
    if (!g->carrying) {
        cutLight(*g);
        return false;
    }
    cXyz flame;
    if (g->torch) {
        mDoMtx_copy(hand, mDoMtx_stack_c::now);
        mDoMtx_stack_c::concat(kTorchGrip);
        mDoMtx_stack_c::multVec(&kTorchTip, &flame);
    } else {
        flame.set(hand[0][3], hand[1][3] - kLanternFlame, hand[2][3]);
    }
    // the flame (Link's lantern fire; a bigger one on a torch) and a warm light round it
    const cXyz scale = g->torch ? cXyz(1.8f, 1.8f, 1.8f) : cXyz(1.0f, 1.0f, 1.0f);
    g->flame = dComIfGp_particle_set(g->flame, ID_ZI_J_KANTERA_FIRE, &flame, &passer->tevStr, nullptr, &scale, 0xFF,
                                     nullptr, -1, nullptr, nullptr, nullptr);
    g->glow.mPosition = flame;
    g->glow.mColor.r = 188;
    g->glow.mColor.g = 110;
    g->glow.mColor.b = 60;
    g->glow.mPow = g->torch ? 300.0f : 220.0f;
    g->glow.mFluctuation = g->torch ? 1.0f : 0.5f;
    if (!g->lit) {
        dKy_plight_set(&g->glow);
        g->lit = true;
    }
    return true;
}

bool install() {
    return mods::hook::add_pre<Cd2LoadResrc>(onLoadResrcPre) == MOD_OK &&
           mods::hook::add_pre<Cd2RemoveResrc>(onRemoveResrcPre) == MOD_OK &&
           mods::hook::add_pre<Cd2GetModel>(onGetModelPre) == MOD_OK &&
           mods::hook::add_pre<Cd2GetEyeAnim>(onGetEyeAnimPre) == MOD_OK &&
           mods::hook::add_post<PasserCreateHeap>(onCreateHeapPost) == MOD_OK &&
           mods::hook::add_post<PasserDraw>(onDrawPost) == MOD_OK;
}

}  // namespace traveller_gear
