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
#include "mod_lights.hpp"

#include "d/actor/d_a_npc_cd2.h"
#include "d/actor/d_a_npc_passer.h"
#include "d/actor/d_a_e_arrow.h"
#include "d/actor/d_a_alink.h"
#include "JSystem/J3DGraphAnimator/J3DJoint.h"
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
#include <cmath>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <cctype>
#include "people.hpp"



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
        // "MON_a" against "mon_a.bmd" (the recolours have their own archive names, the same model file)
        const size_t n = std::strlen(k.model);
        bool same = look->bmd != nullptr && std::strlen(look->bmd) > n && look->bmd[n] == '.';
        for (size_t i = 0; same && i < n; ++i) {
            same = std::tolower(static_cast<unsigned char>(k.model[i])) == std::tolower(static_cast<unsigned char>(look->bmd[i]));
        }
        if (same) return &k;
    }
    return nullptr;
}
#include "guard_gear.inc"
#include "guard_quiver.inc"

const Look* lookFor(int type) {
    static const Look kLooks[30] = {
        {}, {}, {},
        {"P_MON_a", "mon_a.bmd", "mon_a.btp", "pack_mon_a.bmd"},       // 3
        {"P_MAN_b", "man_b.bmd", nullptr, "pack_man_b.bmd"},           // 4
        {},
        {"P_MAS_a", "mas_a.bmd", "mas_a.btp", "pack_mas_a.bmd"},       // 6
        {"P_MBN_a", "mbn_a.bmd", "mbn_a.btp", "pack_mbn_a.bmd"},       // 7
        {"P_MAN_a2", "man_a2.bmd", "man_a2.btp", "pack_man_a2.bmd"},   // 8 (the bowman: quiver and bow under it)
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
    // the guards: Castle Town's own guard bodies, no pack (their arms are drawn from guard_gear.inc)
    static const Look kGuardLooks[2] = {{"MAN_a", "man_a.bmd", nullptr, nullptr}, {"MAD_a", "mad_a.bmd", nullptr, nullptr}};
    if (type == 0 || type == 1) return &kGuardLooks[type];
    if (type < 0 || type >= 30 || kLooks[type].arc == nullptr) return nullptr;
    return &kLooks[type];
}

// The recoloured outfits (tools/looks: recolor.py, pack_variants.py; approved renders 2026-11-19).
// Each is the same model, eye animation and pack as its body, from its own archive PV<tt><v>.
struct Variants { int type; const char* arcs[5]; };
constexpr Variants kVariants[] = {
    {3, {"PV031"}}, {4, {"PV041", "PV042", "PV043", "PV044", "PV045"}}, {6, {"PV061"}}, {7, {"PV071"}},
    {13, {"PV131"}}, {14, {"PV141"}}, {15, {"PV151"}}, {20, {"PV201"}}, {27, {"PV271"}}, {28, {"PV281"}},
    {29, {"PV291"}},
};
const Variants* variantsOf(int type) {
    for (const Variants& v : kVariants) if (v.type == type) return &v;
    return nullptr;
}
int variantCount(const Variants* v) {
    int n = 0;
    while (v != nullptr && n < 5 && v->arcs[n] != nullptr) ++n;
    return n;
}
// A person's look: the body's own, or one of its recolours.
const Look* lookFor(int type, int variant) {
    const Look* base = lookFor(type);
    const Variants* vs = variantsOf(type);
    if (base == nullptr || variant <= 0 || variant > variantCount(vs)) return base;
    static Look s_looks[30][5] = {};
    Look& l = s_looks[type][variant - 1];
    if (l.arc == nullptr) l = Look{vs->arcs[variant - 1], base->bmd, base->btp, base->pack};
    return &l;
}

// The camp cook (camp_cook.cpp): #9's own model and clothes, straight from the game.
const Look kCookLook = {"MAD_a2", "mad_a2.bmd", "mad_a2.btp", nullptr};

// Beedle (tools/beedle): his own model on the men's skeleton, his Wind Waker talking (psr_bs_*_up.bck)
// in the same archive. No pack (his bag is part of him); his lantern hangs from the bag's right side.
const Look kBeedleLook = {"P_Beedl", "beedle.bmd", nullptr, nullptr};
const Vec kBeedleHook = {68.6f, 54.0f, 51.9f};   // the lantern's handle on his bag, backbone joint space

// A traveller in the light world wearing one of our looks (or the cook), or nullptr.
const Look* travellerLook(daNpcCd2_c* npc) {
    if (npc != nullptr && !npc->mIsDarkWorld && cook::isBody(npc)) return &kCookLook;
    if (npc == nullptr || npc->mIsDarkWorld || !travellers::isTraveller(npc)) return nullptr;
    if (travellers::isBeedle(npc)) return &kBeedleLook;
    const people::Person* p = people::get(travellers::slotIndex(npc));
    return lookFor(npc->m_type, p != nullptr && p->type == npc->m_type ? p->look : 0);
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
    // a guard's arms: the game's own spear, the castle soldiers' shields, sword and scabbard
    J3DModel* gSpear = nullptr;
    J3DModel* gShield = nullptr;
    J3DModel* gSword = nullptr;
    J3DModel* gScab = nullptr;
    // Food and drink (Castle Town's own, cut from its rooms): a ham or a loaf (Telma's bar, the bakery
    // One of each kind is picked per meal (see kFoodGrip for how each kind sits in the hand).
    J3DModel* eats[6] = {};       // held by one end: salmon kebab, veggie kebab, carrot, bread, meat, corn
    J3DModel* nibbles[4] = {};    // in the palm: apple, baked potato, pastry, cheese wedge
    J3DModel* drinks[2] = {};     // Telma's brown and green bottles
    J3DModel* goblet = nullptr;   // Telma's
    J3DModel* tankard = nullptr;  // ours, from the Castle Town stein's wood and iron
    J3DModel* mealItem = nullptr;   // in hand for this meal
    const Mtx* mealGrip = nullptr;  // how it sits in this body's hand (meal_grips.inc)
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
    // the bowman (passer 8): the Bulblin's bow and quiver, reskinned; Link's arrows (in his hand, and
    // the ones he has shot while they fly or stick)
    J3DModel* bow = nullptr;
    J3DModel* quiver = nullptr;
    J3DModel* nockArrow = nullptr;
    J3DModel* flyArrows[6] = {};
    struct BowPose {
        Mtx center;               // the grip joint, in the bow's base frame
        f32 a = 160.0f, b = 20.0f;   // the limbs' bend (degrees)
        Vec line = {0.0f, 30.0f, 0.0f};   // the string's middle, in the grip frame
    } bowPose;
    cXyz gripAt;                  // where the bow's grip was last drawn (the arrows leave from there)
    bool gripSeen = false;        // ... in his hand
};
std::array<Gear, travellers::kSlots> g_gear{};

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
constexpr f32 kLanternFlame = 13.0f;             // the flame on the wick (the glass runs 9.5..27 up the lantern)
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
#include "meal_grips.inc"
constexpr int kBardType = 4;   // MAN_b, the tall hat: the bard
const Vec kStaffHook = {2.0f, 148.5f, 25.6f};   // where the lantern's handle hangs on the crook

// The staff stands on the ground where the hand holds it; while she casts it is lifted with her hand
// and tips forward toward the monster. Out: the staff's matrix, and the lantern's (hanging upright).
void staffMtx(daNpcPasser_c* passer, const Mtx hand, f32 grip, Mtx staffOut, Mtx lanternOut) {
    const f32 reach = fighter_anim::castReach(passer);
    // held in the palm, not at the wrist (the hand joint sits at the wrist; the palm is along it)
    const Vec palmLocal = {7.0f, 0.0f, 2.5f};
    Vec palm;
    MTXMultVec(hand, &palmLocal, &palm);
    if (reach > 0.0f) {
        mDoMtx_stack_c::transS(palm.x, palm.y, palm.z);
        mDoMtx_stack_c::YrotM(passer->shape_angle.y);
        mDoMtx_stack_c::XrotM(static_cast<s16>(reach * 0x13E8));   // up to 28 degrees
        mDoMtx_stack_c::transM(0.0f, -grip, 0.0f);
    } else {
        mDoMtx_stack_c::transS(palm.x, passer->current.pos.y, palm.z);
        mDoMtx_stack_c::YrotM(passer->shape_angle.y);
    }
    mDoMtx_copy(mDoMtx_stack_c::now, staffOut);
    cXyz hook;
    mDoMtx_stack_c::multVec(&kStaffHook, &hook);
    mDoMtx_stack_c::transS(hook.x, hook.y - 44.2f, hook.z);
    mDoMtx_stack_c::YrotM(passer->shape_angle.y);
    mDoMtx_copy(mDoMtx_stack_c::now, lanternOut);
}

// --- the bowman's bow -------------------------------------------------------------------------
#include "bow_fit.inc"
constexpr int kBowmanType = 8;
constexpr int kHandL = 7;
constexpr int kArrowRes = 0x1c;          // Link's own arrow model in his archive (as daArrow_c loads it)
constexpr f32 kArrowScale = 0.72f;       // his arrows sized to the bowman
constexpr f32 kArrowLength = 114.0f;     // Link's arrow, nock (its origin) to tip, along +z
constexpr f32 kStringRest = 30.0f;       // the string's middle off the grip, at rest and fully drawn
constexpr f32 kStringFull = 80.0f;
constexpr f32 kFingerReach = 7.0f;       // the string sits this far along the drawing hand
// The quiver and the bow on his back sit this far in against him (backbone space), under his pack.
const Mtx kUnderPack = {{1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f, -8.0f}, {0.0f, 0.0f, 1.0f, 0.0f}};

// Poses the bow's joints (bow_loc, center, bow_1, bow_2, line) from the pose its model carries.
int bowJointCB(J3DJoint* joint, int param) {
    if (param != 0) return 1;
    J3DModel* m = j3dSys.getModel();
    const auto* p = m != nullptr ? reinterpret_cast<const Gear::BowPose*>(m->getUserArea()) : nullptr;
    if (p == nullptr) return 1;
    const int j = joint->getJntNo();
    if (j > 4) return 1;
    mDoMtx_copy(m->getBaseTRMtx(), mDoMtx_stack_c::now);
    if (j >= 1) mDoMtx_stack_c::concat(p->center);
    if (j == 2) {
        mDoMtx_stack_c::transM(-5.0f, 0.0f, 0.0f);
        mDoMtx_stack_c::ZrotM(cM_deg2s(p->a));
        mDoMtx_stack_c::XrotM(-0x8000);
    } else if (j == 3) {
        mDoMtx_stack_c::transM(5.0f, 0.0f, 0.0f);
        mDoMtx_stack_c::ZrotM(cM_deg2s(p->b));
    } else if (j == 4) {
        mDoMtx_stack_c::transM(p->line.x, p->line.y, p->line.z);
    }
    m->setAnmMtx(j, mDoMtx_stack_c::now);
    cMtx_copy(mDoMtx_stack_c::now, J3DSys::mCurrentMtx);
    return 1;
}

// A model along +z with its origin at `from`, pointing at `to`, scaled.
void pointMtx(const cXyz& from, const cXyz& to, f32 scale, Mtx out) {
    cXyz z = to - from;
    const f32 l = z.abs();
    if (l < 0.001f) z.set(0.0f, 0.0f, 1.0f); else z /= l;
    cXyz up(0.0f, 1.0f, 0.0f);
    if (std::fabs(z.y) > 0.98f) up.set(1.0f, 0.0f, 0.0f);
    cXyz x = up.outprod(z);
    x.normalize();
    const cXyz y = z.outprod(x);
    const f32 k = scale;
    out[0][0] = x.x * k; out[0][1] = y.x * k; out[0][2] = z.x * k; out[0][3] = from.x;
    out[1][0] = x.y * k; out[1][1] = y.y * k; out[1][2] = z.y * k; out[1][3] = from.y;
    out[2][0] = x.z * k; out[2][1] = y.z * k; out[2][2] = z.z * k; out[2][3] = from.z;
}

// The arrows he has shot (e_arrow actors: the Bulblins' own flight, sticking and pick-up), each with
// one of his arrow models to be drawn with instead of the Bulblin's.
struct Flying {
    fpc_ProcID id = fpcM_ERROR_PROCESS_ID_e;
    std::chrono::steady_clock::time_point born{};
    Gear* gear = nullptr;
    J3DModel* model = nullptr;    // nullptr: his models are gone (he left): drawn as nothing
    bool armed = false;           // its attack set up as ours
};
std::array<Flying, 16> g_flying{};

Flying* flyingFor(fpc_ProcID id) {
    if (id == fpcM_ERROR_PROCESS_ID_e) return nullptr;
    for (Flying& f : g_flying) if (f.id == id) return &f;
    return nullptr;
}
// His models are about to go: arrows still about drop theirs.
void forgetArrows(Gear* g) {
    for (Flying& f : g_flying) {
        if (f.gear == g) {
            f.gear = nullptr;
            f.model = nullptr;
        }
    }
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
    {
        // the first time each body archive is asked for, say so before loading it: a crash while
        // loading (as on 2026-11-18) then names the archive in the log
        static const char* s_asked[48] = {};
        static int s_nAsked = 0;
        bool seen = false;
        for (int i = 0; i < s_nAsked; ++i) seen = seen || s_asked[i] == look->arc;
        if (!seen && s_nAsked < 48) {
            s_asked[s_nAsked++] = look->arc;
            travellers::diagLine("look    loading %s (%s) for type %d", look->arc, look->bmd, type);
        }
    }
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
        forgetArrows(g);
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
    const u8 style = travellers::guardStyle(passer);
    g->gSpear = g->gShield = g->gSword = g->gScab = nullptr;
    // fighters carry a torch, everyone else a lantern; the old women hang theirs on a hooked staff
    g->torch = travellers::isFighter(passer);
    const bool guard = style != 0;   // a guard's hands are full: no torch, no lantern
    // The game sizes a townsfolk's heap for its own body; the torch, lantern and staff don't fit in
    // it (they silently failed to be made). They get a heap of their own, freed with the traveller.
    if (g->extras != nullptr) {   // (made twice: the game retried)
        forgetArrows(g);
        mDoExt_destroySolidHeap(g->extras);
    }
    g->bow = g->quiver = g->nockArrow = nullptr;
    for (J3DModel*& a : g->flyArrows) a = nullptr;
    g->extras = mDoExt_createSolidHeapFromGame(guard ? 0x60000 : 0x40000, 0x20);   // (adjusted to fit after)
    if (g->extras != nullptr) {
        JKRHeap* before = mDoExt_setCurrentHeap(g->extras);
        g->light = guard ? nullptr : make(g->torch ? "psr_torch.bmd" : "psr_lantern.bmd");
        if (guard) {   // a guard's arms: in this heap too (the body's own has no room for them)
            if (style == travellers::kStyleSpear) {
                if (auto* data = static_cast<J3DModelData*>(dComIfG_getObjectRes("object", "b_spear.bmd"))) {
                    g->gSpear = mDoExt_J3DModel__create(data, 0x80000, 0x11000084);
                }
                g->gShield = make("psr_g_shield_t.bmd");
            } else {
                g->gShield = make("psr_g_shield_s.bmd");
                g->gSword = make("psr_g_sword.bmd");
            }
            g->gScab = make("psr_g_scabbard.bmd");
        }
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
        const bool royal = style == travellers::kStyleArcher;   // the royal convoy's archer: the royal bow
        if (passer->m_type == kBowmanType || royal) {
            g->bow = make(royal ? "psr_bow_royal.bmd" : "psr_bow.bmd");
            g->quiver = make(royal ? "psr_quiver_royal.bmd" : "psr_quiver.bmd");
            if (g->bow != nullptr) {
                g->bow->setUserArea(reinterpret_cast<uintptr_t>(&g->bowPose));
                J3DModelData* bd = g->bow->getModelData();
                for (u16 i = 0; i < bd->getJointNum() && i < 5; ++i) bd->getJointNodePointer(i)->setCallBack(bowJointCB);
            }
            if (auto* data = static_cast<J3DModelData*>(dComIfG_getObjectRes(daAlink_c::getAlinkArcName(), kArrowRes))) {
                g->nockArrow = mDoExt_J3DModel__create(data, 0x80000, 0x11000084);
                for (J3DModel*& a : g->flyArrows) a = mDoExt_J3DModel__create(data, 0x80000, 0x11000084);
            }
        }
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
    if (const int kind = fighter_anim::meal(passer); kind > 0 && g->mealItem != nullptr && g->mealGrip != nullptr &&
                                                      fighter_anim::leftHandMtx(passer, hand)) {
        mDoMtx_copy(hand, mDoMtx_stack_c::now);
        mDoMtx_stack_c::concat(*g->mealGrip);
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
    } else if (g->light != nullptr && g->carrying && travellers::isBeedle(passer)) {
        cXyz hook;
        mDoMtx_copy(body->getAnmMtx(kBackbone), mDoMtx_stack_c::now);
        mDoMtx_stack_c::multVec(&kBeedleHook, &hook);
        mDoMtx_stack_c::transS(hook.x, hook.y - kLanternDrop, hook.z);   // hanging upright from its hook
        mDoMtx_stack_c::YrotM(passer->shape_angle.y);
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
    // a guard's shield goes on his back when he sits (by the fire, on the wagon's bench), and the royal
    // archer's while his bow is in his hand
    const u8 gStyle = travellers::guardStyle(passer);
    const int gBody = passer->m_type == 1 ? 1 : 0;
    const bool gOnBench = passer->mpMorf != nullptr && passer->mpMorf->getAnm() == passer->getAnmP(14, 0);
    const bool bowInHand = g->bow != nullptr && fighter_anim::bowView(passer).inHand;
    // ...and whenever he isn't fighting: shield and spear slung on his back, hands free
    const bool gFighting = gStyle != 0 && fighter_anim::weaponOut(passer) && !bowInHand;
    const bool shieldOnBack =
        gStyle != 0 && (!gFighting || g->sitting || gOnBench || (gStyle == travellers::kStyleArcher && bowInHand));
    const bool royalArcher = gStyle == travellers::kStyleArcher;
    // fitted in the backbone's own space: the bowman's pushed under his pack; the royal archer's (no pack)
    // in layers on his back: quiver against it (Triforce out), the bow over it, the shield over the bow
    drawOn(g->quiver, kBackbone, royalArcher ? &kGuardQuiver[gBody] : &kUnderPack);
    if (g->bow != nullptr) {
        // in his left hand while he shoots (the string on his fingers once drawn), else across his back
        const fighter_anim::BowView v = fighter_anim::bowView(passer);
        Gear::BowPose& p = g->bowPose;
        mDoMtx_copy(body->getAnmMtx(v.inHand ? kHandL : kBackbone), mDoMtx_stack_c::now);
        if (v.inHand) mDoMtx_stack_c::concat(kBowGripL);
        else if (royalArcher) mDoMtx_stack_c::concat(kGuardBowOnBack[gBody]);
        else {
            mDoMtx_stack_c::concat(kUnderPack);
            mDoMtx_stack_c::concat(kBowOnBack);
        }
        Mtx base;
        mDoMtx_copy(mDoMtx_stack_c::now, base);
        mDoMtx_copy(v.inHand ? kBowCenterHand : kBowCenterBack, p.center);
        // where the drawing fingers are (the right hand, a little along it)
        MtxP hr = body->getAnmMtx(kHandR);
        cXyz along(hr[0][0], hr[1][0], hr[2][0]);
        along.normalize();
        const cXyz fingers(hr[0][3] + along.x * kFingerReach, hr[1][3] + along.y * kFingerReach,
                           hr[2][3] + along.z * kFingerReach);
        p.a = 160.0f;
        p.b = 20.0f;
        p.line = {0.0f, kStringRest, 0.0f};
        Mtx grip, gripInv;
        mDoMtx_concat(base, p.center, grip);
        if (v.nocked && MTXInverse(grip, gripInv) != 0) {
            cXyz local;
            mDoMtx_multVec(gripInv, &fingers, &local);
            p.line = local;
            const f32 pull = std::clamp((local.abs() - kStringRest) / (kStringFull - kStringRest), 0.0f, 1.0f);
            p.a = 160.0f - 25.0f * pull;
            p.b = 20.0f + 25.0f * pull;
        }
        g->gripAt.set(grip[0][3], grip[1][3], grip[2][3]);
        g->gripSeen = v.inHand;
        g->bow->setBaseTRMtx(base);
        g_env_light.setLightTevColorType_MAJI(g->bow, &passer->tevStr);
        mDoExt_modelUpdateDL(g->bow);
        if (v.arrow && g->nockArrow != nullptr) {
            // its nock on the string (or in his fingers), pointing out past the grip
            const cXyz gripAt(grip[0][3], grip[1][3], grip[2][3]);
            cXyz nock = fingers;
            if (v.nocked) {
                Vec w;
                mDoMtx_multVec(grip, &p.line, &w);
                nock.set(w.x, w.y, w.z);
            }
            Mtx at;
            pointMtx(nock, gripAt, kArrowScale, at);
            g->nockArrow->setBaseTRMtx(at);
            g_env_light.setLightTevColorType_MAJI(g->nockArrow, &passer->tevStr);
            mDoExt_modelUpdateDL(g->nockArrow);
        }
    }
    if (const u8 style = travellers::guardStyle(passer); style != 0) {
        // the guards: spear (walking: as on the spear walk; fighting: as the castle soldiers hold it),
        // shield on the left arm, scabbard at the side, the sword in hand while fighting
        const bool big = passer->m_type == 1;
        const bool fighting = fighter_anim::weaponOut(passer) && !fighter_anim::bowView(passer).inHand;
        if (fighting && !shieldOnBack) drawOn(g->gSpear, kHandR, big ? &kHold_fspear_mad_a : &kHold_fspear_man_a);
        else drawOn(g->gSpear, kBackbone, big ? &kHold_back_spear_mad_a : &kHold_back_spear_man_a);
        const bool spear = style == travellers::kStyleSpear;
        if (shieldOnBack) {   // sitting down (by the fire, on the wagon's bench), or shooting: the shield on his back
            drawOn(g->gShield, kBackbone,
                   spear ? (big ? &kHold_back_tall_mad_a : &kHold_back_tall_man_a)
                         : royalArcher ? &kGuardShieldBack[gBody]   // over his quiver and bow
                                       : (big ? &kHold_back_small_mad_a : &kHold_back_small_man_a));
        } else drawOn(g->gShield, kHandL, fighting ? (spear ? (big ? &kHold_ftall_mad_a : &kHold_ftall_man_a)
                                                     : (big ? &kHold_fsmall_mad_a : &kHold_fsmall_man_a))
                                            : (spear ? &kHold_tall : &kHold_small));
        drawOn(g->gScab, kWaist, &kHold_scab);
        if (fighting) drawOn(g->gSword, kHandR, big ? &kHold_fsword_mad_a : &kHold_fsword_man_a);
    }
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

// His arrows are the Bulblins' own arrow actor (e_arrow: its flight, drop, sticking in the ground and
// Link picking them up), with two changes: they hurt monsters only (an arrow's attack, Link's arrow
// power, aimed at the enemies' group alone: never Link, travellers or the wagon), and they look like
// Link's arrows (his model, sized to the bowman), drawn here instead of the Bulblin's.
DEFINE_HOOK_SYMBOL("daE_ARROW_Execute", int(void*), ArrowExecute);
DEFINE_HOOK_SYMBOL("daE_ARROW_Draw", int(void*), ArrowDraw);
constexpr f32 kArrowTipAhead = 35.0f;   // its tip is this far past the actor (it sticks 35 short of the wall)

HookAction onArrowExecutePre(ModContext*, void* args, void*, void*) {
    auto* a = mods::arg<e_arrow_class*>(args, 0);
    Flying* f = a != nullptr ? flyingFor(fopAcM_GetID(a)) : nullptr;
    if (f == nullptr || f->armed) return HOOK_CONTINUE;
    f->armed = true;
    a->mCcAtSph.SetAtType(AT_TYPE_ARROW);
    a->mCcAtSph.SetAtAtp(2);
    a->mCcAtSph.SetAtSPrm(0x3);   // set, against the enemies' group only
    return HOOK_CONTINUE;
}

HookAction onArrowDrawPre(ModContext*, void* args, void* retval, void*) {
    auto* a = mods::arg<e_arrow_class*>(args, 0);
    Flying* f = a != nullptr ? flyingFor(fopAcM_GetID(a)) : nullptr;
    if (f == nullptr) return HOOK_CONTINUE;
    if (retval != nullptr) *static_cast<int*>(retval) = 1;
    if (a->field_0xa14 != 0 || f->model == nullptr) return HOOK_SKIP_ORIGINAL;
    g_env_light.settingTevStruct(0, &a->current.pos, &a->tevStr);
    mDoMtx_stack_c::transS(a->current.pos.x, a->current.pos.y, a->current.pos.z);
    mDoMtx_stack_c::YrotM(a->shape_angle.y);
    mDoMtx_stack_c::XrotM(a->shape_angle.x);
    mDoMtx_stack_c::ZrotM(a->shape_angle.z);
    mDoMtx_stack_c::transM(0.0f, 0.0f, kArrowTipAhead - kArrowLength * kArrowScale);
    mDoMtx_stack_c::scaleM(kArrowScale, kArrowScale, kArrowScale);
    f->model->setBaseTRMtx(mDoMtx_stack_c::now);
    g_env_light.setLightTevColorType_MAJI(f->model, &a->tevStr);
    mDoExt_modelUpdateDL(f->model);
    return HOOK_SKIP_ORIGINAL;
}

}  // namespace

namespace traveller_gear {
int lookVariants(int type) { return variantCount(variantsOf(type)); }

bool shootArrow(daNpcPasser_c* passer, fopAc_ac_c* target) {
    Gear* g = travellerLook(passer) != nullptr ? gearFor(passer) : nullptr;
    if (g == nullptr || g->owner != fopAcM_GetID(passer) || g->bow == nullptr || target == nullptr) return false;
    // a place to keep track of it (arrows long gone, or never made, free theirs)
    const auto now = std::chrono::steady_clock::now();
    Flying* slot = nullptr;
    for (Flying& f : g_flying) {
        if (f.id != fpcM_ERROR_PROCESS_ID_e && fopAcM_SearchByID(f.id) == nullptr &&
            (f.armed || now - f.born > std::chrono::seconds(2))) {
            f = Flying{};
        }
        if (slot == nullptr && f.id == fpcM_ERROR_PROCESS_ID_e) slot = &f;
    }
    if (slot == nullptr) return false;
    J3DModel* model = nullptr;
    for (J3DModel* m : g->flyArrows) {
        bool used = false;
        for (const Flying& f : g_flying) used = used || (f.id != fpcM_ERROR_PROCESS_ID_e && f.model == m);
        if (m != nullptr && !used) { model = m; break; }
    }
    if (model == nullptr) return false;
    // from the bow, at the middle of it, leading it as the Bulblins do; aimed a little high for the
    // drop (the arrow tips down 65 a frame: aim up by half of what it will have dropped by then)
    const cXyz from = g->gripSeen ? g->gripAt : passer->eyePos;
    cXyz aim(target->current.pos.x, std::max(target->eyePos.y - 30.0f, target->current.pos.y + 30.0f),
             target->current.pos.z);
    const f32 frames = std::clamp(from.abs(aim) / 100.0f, 1.0f, 60.0f);
    aim += (target->current.pos - target->old.pos) * frames;
    const cXyz d = aim - from;
    csXyz ang;
    ang.y = static_cast<s16>(cM_atan2s(d.x, d.z) + cM_rndFX(150.0f));
    ang.x = static_cast<s16>(-cM_atan2s(d.y, std::sqrt(d.x * d.x + d.z * d.z)) - 65.0f * frames * 0.5f +
                             cM_rndFX(150.0f));
    ang.z = 0;
    const fpc_ProcID id = fopAcM_createChild(fpcNm_E_ARROW_e, fopAcM_GetID(passer), 0, &from,
                                             fopAcM_GetRoomNo(passer), &ang, nullptr, -1, nullptr);
    if (id == fpcM_ERROR_PROCESS_ID_e) return false;
    slot->id = id;
    slot->born = now;
    slot->gear = g;
    slot->model = model;
    slot->armed = false;
    return true;
}

bool isOurArrow(const fopAc_ac_c* actor) {
    return actor != nullptr && flyingFor(fopAcM_GetID(actor)) != nullptr;
}

void clearMeal(daNpcPasser_c* passer) {
    Gear* g = travellerLook(passer) != nullptr ? gearFor(passer) : nullptr;
    if (g == nullptr || g->owner != fopAcM_GetID(passer)) return;
    g->mealItem = nullptr;
    g->mealGrip = nullptr;
}

int pickMeal(daNpcPasser_c* passer, int kind) {
    Gear* g = travellerLook(passer) != nullptr ? gearFor(passer) : nullptr;
    if (g == nullptr || g->owner != fopAcM_GetID(passer)) return kind;
    const int type = passer->m_type;
    const FoodGrips* fg = nullptr;
    for (const FoodGrips& f : kFoodGrips) if (f.type == type) fg = &f;
    if (isOldWoman(type) && kind == 5) kind = 2;   // no tankards for the old women
    const int n = kind == 1 ? 6 : kind == 4 ? 4 : kind == 2 ? 2 : 1;
    const int i = std::min(n - 1, static_cast<int>(cM_rndF(static_cast<f32>(n))));
    g->mealItem = nullptr; g->mealGrip = nullptr;
    if (kind == 1) {
        g->mealItem = g->eats[i];
        if (fg != nullptr) g->mealGrip = &fg->food[i];
        if (i == 0 || i == 1 || i == 5) kind = 6;   // kebabs and corn: eaten off the side
    } else if (kind == 4) {
        g->mealItem = g->nibbles[i];
        if (fg != nullptr) g->mealGrip = &fg->food[6 + i];
    } else if (kind == 2) {
        g->mealItem = g->drinks[i];
        g->mealGrip = type == 20 ? &kOldWomanBottle[0] : type == 27 ? &kOldWomanBottle[1] : &kDrinkGrip;
    } else if (kind == 3) {
        g->mealItem = g->goblet; g->mealGrip = &kSipGrip;
    } else if (kind == 5) {
        g->mealItem = g->tankard; g->mealGrip = &kQuaffGrip;
    }
    return g->mealItem != nullptr && g->mealGrip != nullptr ? kind : 0;
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
    if (travellers::isBeedle(passer)) {
        // Beedle: the lantern on his bag, lit all night wherever he is; his hands stay free
        g->onGround = false;
        g->carrying = night && g->light != nullptr && passer->mpMorf != nullptr;
        if (!g->carrying) {
            cutLight(*g);
            return false;
        }
        cXyz hook;
        mDoMtx_copy(passer->mpMorf->getModel()->getAnmMtx(kBackbone), mDoMtx_stack_c::now);
        mDoMtx_stack_c::multVec(&kBeedleHook, &hook);
        const cXyz flame(hook.x, hook.y - kLanternDrop + kLanternFlame, hook.z);
        const cXyz scale(1.0f, 1.0f, 1.0f);
        g->flame = dComIfGp_particle_set(g->flame, ID_ZI_J_KANTERA_FIRE, &flame, &passer->tevStr, nullptr, &scale,
                                         0xFF, nullptr, -1, nullptr, nullptr, nullptr);
        g->glow.mPosition = flame;
        g->glow.mColor.r = 188;
        g->glow.mColor.g = 110;
        g->glow.mColor.b = 60;
        g->glow.mPow = 220.0f;
        g->glow.mFluctuation = 0.5f;
        mod_lights::offer(flame, 70.0f);   // and on the ground and walls round him
        if (!g->lit) {
            dKy_plight_set(&g->glow);
            g->lit = true;
        }
        return false;
    }
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
        mod_lights::offer(flame, g->torch ? 90.0f : 70.0f);   // and on the ground and walls round it
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
           mods::hook::add_post<PasserDraw>(onDrawPost) == MOD_OK &&
           mods::hook::add_pre<ArrowExecute>(onArrowExecutePre) == MOD_OK &&
           mods::hook::add_pre<ArrowDraw>(onArrowDrawPre) == MOD_OK;
}

}  // namespace traveller_gear
