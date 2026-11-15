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
#include "SSystem/SComponent/c_math.h"
#include "d/d_particle_name.h"
#include "f_op/f_op_actor_mng.h"
#include "m_Do/m_Do_ext.h"
#include "JSystem/JKernel/JKRSolidHeap.h"
#include "m_Do/m_Do_mtx.h"
#include "mods/service.hpp"
#include "mods/svc/hook.hpp"

#include "travellers.hpp"
#include "traveller_gear.hpp"
#include "camp_cook.hpp"
#include "fighter_anim.hpp"

#include <algorithm>
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
    JKRSolidHeap* extras = nullptr;   // the torch/lantern and staff live here: the actor's own heap has no room
    J3DModel* light = nullptr;    // its torch or lantern (copies of Rusl's torch and Link's lantern)
    J3DModel* staff = nullptr;    // the old women's hooked staff (their lantern hangs from it)
    // Food and drink (Castle Town's own, cut from its rooms): a ham or a loaf (Telma's bar, the bakery
    // One of each kind is picked per meal (see kFoodGrip for how each kind sits in the hand).
    J3DModel* eats[6] = {};       // held by one end: salmon kebab, veggie kebab, carrot, bread, meat, corn
    J3DModel* nibbles[4] = {};    // in the palm: apple, baked potato, pastry, cheese wedge
    J3DModel* drinks[2] = {};     // Telma's brown and green bottles
    J3DModel* goblet = nullptr;   // Telma's
    J3DModel* tankard = nullptr;  // ours, from the Castle Town stein's wood and iron
    J3DModel* mealItem = nullptr;   // in hand for this meal
    J3DModel* guitar = nullptr;   // the bard's: the game's own j_guitar (the Castle Town band's)
    bool playing = false;
    bool torch = false;           // fighters: a torch; everyone else a lantern
    bool carrying = false;        // in hand (and lit, at night)
    f32 staffGrip = 114.0f;       // the old women: hand height over the ground while walking with the staff
    bool onGround = false;        // a fighter's torch, put down to fight
    cXyz groundPos;               // where it lies (its handle end)
    s16 groundYaw = 0;
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
const Vec kTorchTip = {11.0f, -3.7f, 74.0f};    // the flame, on the torch model's own tip
constexpr f32 kLanternDrop = 47.0f;              // handle (top) to the model's origin (its base)
constexpr f32 kLanternFlame = 23.0f;             // the flame, this far up from the lantern's base
const Vec kStaffLantern = {2.0f, 104.3f, 25.6f}; // the lantern's base, hanging from the staff's hook
// Where a fighter puts its torch down: in front of it and a little to the left, the spot its pick-up
// reaches for (body units, scaled with the body).
struct Spot { int type; f32 x, z; };
const Spot kTorchSpots[] = {{3, 14.2f, 37.8f}, {6, 10.6f, 28.3f}, {8, 14.9f, 39.8f}, {9, 18.6f, 49.6f},
                            {11, 14.2f, 37.8f}, {13, 12.8f, 34.1f}, {14, 10.6f, 28.3f}, {29, 12.1f, 32.4f}};
bool torchSpot(int type, f32* x, f32* z) {
    for (const Spot& sp : kTorchSpots) {
        if (sp.type == type) {
            const f32 k = Cd2_HIO_objScale(type);
            *x = sp.x * k;
            *z = sp.z * k;
            return true;
        }
    }
    return false;
}
bool isOldWoman(int type) { return type == 20 || type == 27; }
// Item -> left hand, by meal kind (the per-body loops are fitted to exactly these):
//   1 eat: held by one end, the food sticking up out of the fist (every one 26 long, the bite 21 out)
//   2 drink: a bottle round its middle (every bottle 40 tall)   3 sip: the goblet by its stem
//   4 nibble: cupped in the palm                                 5 quaff: the tankard by its handle
const Mtx kFoodGrip[6] = {
    {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}},
    {{1, 0, 0, 7.0f}, {0, -1, 0, 0}, {0, 0, -1, 0}},
    {{1, 0, 0, 7.0f}, {0, 1, 0, -18.0f}, {0, 0, 1, 0}},
    {{1, 0, 0, 7.0f}, {0, 1, 0, -7.0f}, {0, 0, 1, 0}},
    {{1, 0, 0, 7.0f}, {0, 1, 0, 2.0f}, {0, 0, 1, 0}},
    {{1, 0, 0, 7.0f}, {0, 1, 0, -9.0f}, {0, 0, 1, 10.3f}},
};
constexpr int kBardType = 4;   // MAN_b, the tall hat: the bard
const Vec kStaffHook = {2.0f, 148.5f, 25.6f};   // where the lantern's handle hangs on the crook

// The staff stands on the ground where the hand holds it; while she casts it is lifted with her hand
// and tips forward toward the monster. Out: the staff's matrix, and the lantern's (hanging upright).
void staffMtx(daNpcPasser_c* passer, const Mtx hand, f32 grip, Mtx staffOut, Mtx lanternOut) {
    const f32 reach = fighter_anim::castReach(passer);
    if (reach > 0.0f) {
        mDoMtx_stack_c::transS(hand[0][3], hand[1][3], hand[2][3]);
        mDoMtx_stack_c::YrotM(passer->shape_angle.y);
        mDoMtx_stack_c::XrotM(static_cast<s16>(reach * 0x13E8));   // up to 28 degrees
        mDoMtx_stack_c::transM(0.0f, -grip, 0.0f);
    } else {
        mDoMtx_stack_c::transS(hand[0][3], passer->current.pos.y, hand[2][3]);
        mDoMtx_stack_c::YrotM(passer->shape_angle.y);
    }
    mDoMtx_copy(mDoMtx_stack_c::now, staffOut);
    cXyz hook;
    mDoMtx_stack_c::multVec(&kStaffHook, &hook);
    mDoMtx_stack_c::transS(hook.x, hook.y - 44.2f, hook.z);
    mDoMtx_stack_c::YrotM(passer->shape_angle.y);
    mDoMtx_copy(mDoMtx_stack_c::now, lanternOut);
}

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
        if (g->extras != nullptr) mDoExt_destroySolidHeap(g->extras);
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
    // fighters carry a torch, everyone else a lantern; the old women hang theirs on a hooked staff
    g->torch = travellers::isFighter(passer);
    // The game sizes a townsfolk's heap for its own body; the torch, lantern and staff don't fit in
    // it (they silently failed to be made). They get a heap of their own, freed with the traveller.
    if (g->extras != nullptr) mDoExt_destroySolidHeap(g->extras);   // (made twice: the game retried)
    g->extras = mDoExt_createSolidHeapFromGame(0x40000, 0x20);
    if (g->extras != nullptr) {
        JKRHeap* before = mDoExt_setCurrentHeap(g->extras);
        g->light = make(g->torch ? "psr_torch.bmd" : "psr_lantern.bmd");
        g->staff = isOldWoman(passer->m_type) ? make("psr_staff.bmd") : nullptr;
        static const char* const kEats[6] = {"psr_food_kebab.bmd", "psr_food_veggie.bmd", "psr_food_carrot.bmd",
                                             "psr_food_bread.bmd", "psr_food_meat.bmd", "psr_food_corn.bmd"};
        static const char* const kNibbles[4] = {"psr_nib_apple.bmd", "psr_nib_potato.bmd", "psr_nib_pastry.bmd",
                                                "psr_nib_cheese.bmd"};
        for (int i = 0; i < 6; ++i) g->eats[i] = make(kEats[i]);
        for (int i = 0; i < 4; ++i) g->nibbles[i] = make(kNibbles[i]);
        g->drinks[0] = make("psr_drink_brown.bmd");
        g->drinks[1] = make("psr_drink_green.bmd");
        g->goblet = make("psr_goblet.bmd");
        g->tankard = make("psr_cup_tankard.bmd");
        g->mealItem = nullptr;
        if (passer->m_type == kBardType) {   // the townsfolk's own object archive, already loaded
            if (auto* data = static_cast<J3DModelData*>(dComIfG_getObjectRes("object", "j_guitar.bmd"))) {
                g->guitar = mDoExt_J3DModel__create(data, 0x80000, 0x11000084);
            }
        }
        mDoExt_setCurrentHeap(before);
        mDoExt_adjustSolidHeap(g->extras);
    }
    g->carrying = false;
    g->onGround = false;
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
    const bool seated = !cook::isBody(passer) && g->sitting && travellers::campInfo(passer, nullptr, nullptr, nullptr);
    if (g->seat != nullptr && seated) {
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
    auto drawModel = [&](J3DModel* m) {
        m->setBaseTRMtx(mDoMtx_stack_c::now);
        g_env_light.setLightTevColorType_MAJI(m, &passer->tevStr);
        mDoExt_modelUpdateDL(m);
    };
    Mtx hand;
    // eating or drinking on the stool: the item fixed in the left hand, as the loops were fitted
    if (const int kind = fighter_anim::meal(passer); kind > 0 && g->mealItem != nullptr &&
                                                      fighter_anim::leftHandMtx(passer, hand)) {
        mDoMtx_copy(hand, mDoMtx_stack_c::now);
        mDoMtx_stack_c::concat(kFoodGrip[kind]);
        drawModel(g->mealItem);
    }
    // at a camp the staff is put down, except while she casts with it
    const bool atFire = (g->sitting || travellers::campInfo(passer, nullptr, nullptr, nullptr)) &&
                        fighter_anim::castFrame(passer) < 0.0f;
    if (g->staff != nullptr && !atFire && fighter_anim::leftHandMtx(passer, hand)) {
        // the staff stands upright on the ground where the hand holds it, the hook to the front
        Mtx staffAt, lanternAt;
        staffMtx(passer, hand, g->staffGrip, staffAt, lanternAt);
        mDoMtx_copy(staffAt, mDoMtx_stack_c::now);
        drawModel(g->staff);
        if (g->light != nullptr) {
            mDoMtx_copy(lanternAt, mDoMtx_stack_c::now);
            drawModel(g->light);
        }
    } else if (g->light != nullptr && g->onGround) {
        mDoMtx_stack_c::transS(g->groundPos.x, g->groundPos.y + 10.0f, g->groundPos.z);   // lying flat
        mDoMtx_stack_c::YrotM(g->groundYaw);
        mDoMtx_stack_c::transM(-10.0f, 0.0f, 0.0f);
        drawModel(g->light);
    } else if (g->light != nullptr && g->carrying && fighter_anim::leftHandMtx(passer, hand)) {
        if (g->torch) {
            mDoMtx_copy(hand, mDoMtx_stack_c::now);
            mDoMtx_stack_c::concat(kTorchGrip);
        } else {
            mDoMtx_stack_c::transS(hand[0][3], hand[1][3] - kLanternDrop, hand[2][3]);
            mDoMtx_stack_c::YrotM(passer->shape_angle.y);
        }
        drawModel(g->light);
    }
    if (g->guitar != nullptr && g->playing) {
        // where the game puts it on a band member: the chest joint, at the body's object scale
        const f32 k = Cd2_HIO_objScale(passer->m_type);
        mDoMtx_copy(body->getAnmMtx(kBackbone), mDoMtx_stack_c::now);
        mDoMtx_stack_c::scaleM(k, k, k);
        g->guitar->setBaseTRMtx(mDoMtx_stack_c::now);
        g_env_light.setLightTevColorType_MAJI(g->guitar, &passer->tevStr);
        mDoExt_modelUpdateDL(g->guitar);
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

void pickMeal(daNpcPasser_c* passer, int kind) {
    Gear* g = travellerLook(passer) != nullptr ? gearFor(passer) : nullptr;
    if (g == nullptr || g->owner != fopAcM_GetID(passer)) return;
    auto pick = [](J3DModel* const* from, int n) { return from[std::min(n - 1, static_cast<int>(cM_rndF(static_cast<f32>(n))))]; };
    if (kind == 1) g->mealItem = pick(g->eats, 6);
    else if (kind == 2) g->mealItem = pick(g->drinks, 2);
    else if (kind == 3) g->mealItem = g->goblet;
    else if (kind == 4) g->mealItem = pick(g->nibbles, 4);
    else if (kind == 5) g->mealItem = g->tankard;
    else g->mealItem = nullptr;
}

bool sitting(int slot) {
    return slot >= 0 && slot < static_cast<int>(g_gear.size()) && g_gear[slot].sitting && g_gear[slot].seat != nullptr;
}

bool sit(daNpcPasser_c* passer, bool on) {
    Gear* g = travellerLook(passer) != nullptr ? gearFor(passer) : nullptr;
    if (g == nullptr || g->owner != fopAcM_GetID(passer)) on = false;
    if (on) {
        // the townsfolk "sit and wait" loop (the heavyset men have their own); asked for with no
        // held object so the game doesn't swap in its standing fallback
        // the same sit as everyone (the heavyset men too)
        auto* anm = static_cast<J3DAnmTransformKey*>(passer->getAnmP(14, 0));
        if (anm == nullptr) on = false;
        else passer->setAnm(anm, 1.0f, 12.0f, J3DFrameCtrl::EMode_LOOP, 0, -1);
    }
    if (g != nullptr) g->sitting = on;
    return on;
}

bool lightTick(daNpcPasser_c* passer, bool night, bool atCamp) {
    Gear* g = travellerLook(passer) != nullptr ? gearFor(passer) : nullptr;
    if (g == nullptr || g->owner != fopAcM_GetID(passer)) return false;
    Mtx hand;
    const bool haveHand = fighter_anim::leftHandMtx(passer, hand);
    if (!night) g->onGround = false;   // morning: put away
    g->carrying = night && !atCamp && !g->onGround && g->light != nullptr && haveHand;
    const bool staffOut = g->staff != nullptr && (!atCamp || fighter_anim::castFrame(passer) >= 0.0f) && haveHand;
    if (staffOut && fighter_anim::castReach(passer) <= 0.0f && fighter_anim::castFrame(passer) < 0.0f) {
        g->staffGrip = std::max(40.0f, hand[1][3] - passer->current.pos.y);   // how high she holds it
    }
    // the flame: on the torch's tip, in the lantern (in hand or on the staff's hook)
    cXyz flame;
    bool burning = night && g->light != nullptr && (g->carrying || g->onGround);
    if (burning && g->onGround) {
        mDoMtx_stack_c::transS(g->groundPos.x, g->groundPos.y + 10.0f, g->groundPos.z);
        mDoMtx_stack_c::YrotM(g->groundYaw);
        mDoMtx_stack_c::transM(-10.0f, 0.0f, 0.0f);
        mDoMtx_stack_c::multVec(&kTorchTip, &flame);
    } else if (burning && staffOut) {
        Mtx staffAt, lanternAt;
        staffMtx(passer, hand, g->staffGrip, staffAt, lanternAt);
        flame.set(lanternAt[0][3], lanternAt[1][3] + kLanternFlame, lanternAt[2][3]);
    } else if (burning && g->torch) {
        mDoMtx_copy(hand, mDoMtx_stack_c::now);
        mDoMtx_stack_c::concat(kTorchGrip);
        mDoMtx_stack_c::multVec(&kTorchTip, &flame);
    } else if (burning) {
        flame.set(hand[0][3], hand[1][3] - kLanternDrop + kLanternFlame, hand[2][3]);
    }
    if (!burning) {
        cutLight(*g);
    } else {
        // Link's lantern fire (a bigger one on a torch) and a warm light round it
        const cXyz scale = g->torch ? cXyz(1.8f, 1.8f, 1.8f) : cXyz(1.0f, 1.0f, 1.0f);
        g->flame = dComIfGp_particle_set(g->flame, ID_ZI_J_KANTERA_FIRE, &flame, &passer->tevStr, nullptr, &scale,
                                         0xFF, nullptr, -1, nullptr, nullptr, nullptr);
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
    }
    // the arm holds up whatever is in it: the staff all day, a torch or lantern at night
    return staffOut || g->carrying;
}

bool lanternMtx(daNpcPasser_c* passer, Mtx out) {
    Gear* g = travellerLook(passer) != nullptr ? gearFor(passer) : nullptr;
    Mtx hand, staffAt;
    if (g == nullptr || g->owner != fopAcM_GetID(passer) || g->staff == nullptr ||
        !fighter_anim::leftHandMtx(passer, hand)) return false;
    staffMtx(passer, hand, g->staffGrip, staffAt, out);
    return true;
}

bool isBard(const daNpcPasser_c* passer) { return passer != nullptr && passer->m_type == kBardType; }

void setPlaying(daNpcPasser_c* passer, bool on) {
    Gear* g = travellerLook(passer) != nullptr ? gearFor(passer) : nullptr;
    if (g != nullptr && g->owner == fopAcM_GetID(passer)) g->playing = on && g->guitar != nullptr;
}

bool torchInHand(daNpcPasser_c* passer) {
    Gear* g = travellerLook(passer) != nullptr ? gearFor(passer) : nullptr;
    return g != nullptr && g->owner == fopAcM_GetID(passer) && g->torch && g->carrying;
}

bool torchOnGround(daNpcPasser_c* passer, cXyz* standAt, s16* yaw) {
    Gear* g = travellerLook(passer) != nullptr ? gearFor(passer) : nullptr;
    if (g == nullptr || g->owner != fopAcM_GetID(passer) || !g->onGround) return false;
    f32 x = 0.0f, z = 0.0f;
    torchSpot(passer->m_type, &x, &z);
    // stand so the spot is where the pick-up reaches: (x left, z ahead) of its feet
    const s16 a = g->groundYaw;
    if (standAt != nullptr) {
        standAt->set(g->groundPos.x - (cM_scos(a) * x + cM_ssin(a) * z), g->groundPos.y,
                     g->groundPos.z - (-cM_ssin(a) * x + cM_scos(a) * z));
    }
    if (yaw != nullptr) *yaw = a;
    return true;
}

void dropTorch(daNpcPasser_c* passer) {
    Gear* g = travellerLook(passer) != nullptr ? gearFor(passer) : nullptr;
    if (g == nullptr || g->owner != fopAcM_GetID(passer) || !g->torch || !g->carrying) return;
    f32 x = 0.0f, z = 0.0f;
    if (!torchSpot(passer->m_type, &x, &z)) return;
    const s16 a = passer->shape_angle.y;
    g->groundYaw = a;
    g->groundPos.set(passer->current.pos.x + cM_scos(a) * x + cM_ssin(a) * z, passer->current.pos.y,
                     passer->current.pos.z - cM_ssin(a) * x + cM_scos(a) * z);
    g->onGround = true;
    g->carrying = false;
}

void takeTorch(daNpcPasser_c* passer) {
    Gear* g = travellerLook(passer) != nullptr ? gearFor(passer) : nullptr;
    if (g != nullptr && g->owner == fopAcM_GetID(passer)) g->onGround = false;
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
