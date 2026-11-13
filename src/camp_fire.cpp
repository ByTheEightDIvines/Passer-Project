#include "global.h"

#include "camp_fire.hpp"
#include "travellers.hpp"
#include "camp_cook.hpp"

#include "d/actor/d_a_alink.h"
#include "d/d_bg_s_gnd_chk.h"
#include "d/d_com_inf_game.h"
#include "d/d_item_data.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_draw_priority.h"
#include "m_Do/m_Do_audio.h"
#include "m_Do/m_Do_mtx.h"
#include "mods/service.hpp"
#include "mods/svc/save.h"

#include <array>

IMPORT_SERVICE(SaveService, svc_save);

namespace {
const char* kCampArc = "PsrCamp";
const char* kPackArc = "PsrPack";
const char* kParts[psrFire_c::kPartNum] = {"coro_stones.bmd", "coro_wood.bmd", "coro_pot.bmd"};
const char* kBubbles[3] = {"coro_bubbles1.bmd", "coro_bubbles2.bmd", "coro_bubbles3.bmd"};
const char* kBubbling = "coro_bubbles.btk";
const char* kSoups[3] = {"y1stsoup.bmd", "y2ndsoup.bmd", "y3rdsoup.bmd"};
const u8 kSoupItem[3] = {dItemNo_LV1_SOUP_e, dItemNo_LV2_SOUP_e, dItemNo_LV3_SOUP_e};

// Coro's bubbling surface is still in Faron's coordinates; this is where his pot stands there.
constexpr f32 kCoroPot[3] = {-13880.0f, 0.0f, -14360.0f};
// Yeto's soups are modelled where his pot is in his kitchen: the middle of the broth, and how far
// to shrink it to sit inside Coro's (smaller, eight-sided) pot just under the rim.
constexpr f32 kYetoBroth[3] = {3317.8f, 214.6f, 323.2f};
constexpr f32 kSoupScale = 0.235f;
constexpr f32 kSoupLevel = 70.0f;
// Coro's flame comes up from under his firewood (his ktFire sits 30 below the ground there).
constexpr f32 kFlameY = -30.0f;

// Barrels and the Kakariko crate by the tents (camp space: +z is the way the camp faces; the big
// tent is behind the fire, the small one to its right).
struct Decor {
    int arc;   // 0 barrel, 1 crate
    f32 x, z, scale;
    s16 turn;
};
constexpr Decor kDecor[psrFire_c::kDecorNum] = {
    {0, 330.0f, -250.0f, 0.9f, 0x1200},
    {0, 420.0f, -170.0f, 0.9f, 0x5400},
    {1, 300.0f, -360.0f, 0.45f, 0x0800},
    {0, -300.0f, -260.0f, 0.9f, 0x3300},
};

constexpr f32 kStoolHeight = 40.0f;   // a man's stool (the vanilla one is 37.2)
constexpr f32 kStoolWidth = 1.1f;
constexpr f32 kScoopReach = 250.0f;

bool fireTime() {
    const int hour = dKy_getdaytime_hour();
    return hour >= 17 || hour < 7;
}

// --- one scoop per camp per day, kept with the save file ---------------------------------------
constexpr int kNodeMax = 128;
std::array<u16, kNodeMax> g_scoopDay{};   // game date + 1 of the last scoop; 0 never
const char* kScoopBlob = "camp_scoops";

void loadScoops(ModContext*, uint32_t, void*) {
    g_scoopDay.fill(0);
    size_t size = sizeof(g_scoopDay);
    svc_save->get_blob(mod_ctx, kScoopBlob, g_scoopDay.data(), &size);
}
void clearScoops(ModContext*, uint32_t, void*) { g_scoopDay.fill(0); }

u16 today() { return static_cast<u16>(dComIfGs_getDate() + 1); }
bool scoopedToday(int node) { return node >= 0 && node < kNodeMax && g_scoopDay[node] == today(); }
void markScooped(int node) {
    if (node < 0 || node >= kNodeMax) return;
    g_scoopDay[node] = today();
    svc_save->set_blob(mod_ctx, kScoopBlob, g_scoopDay.data(), sizeof(g_scoopDay));
}

J3DModel* makeModel(const char* arc, const char* file, u32 flags = 0x11000084) {
    auto* data = static_cast<J3DModelData*>(dComIfG_getObjectRes(arc, file));
    return data != NULL ? mDoExt_J3DModel__create(data, 0x80000, flags) : NULL;
}
}  // namespace

psrFire_c::~psrFire_c() {
    if (mLit) {
        dKy_plight_cut(&mLight);
    }
    if (mLoaded) {
        dComIfG_resDelete(&mPhCamp, kCampArc);
        dComIfG_resDelete(&mPhPack, kPackArc);
        dComIfG_resDelete(&mPhSoup, "SnowSoup");
        dComIfG_resDelete(&mPhBarrel, "J_taru00");
        dComIfG_resDelete(&mPhCrate, "Obj_kbox");
    }
}

cPhs_Step psrFire_c::create() {
    fopAcM_ct(this, psrFire_c);
    const cPhs_Step steps[] = {
        dComIfG_resLoad(&mPhCamp, kCampArc), dComIfG_resLoad(&mPhPack, kPackArc),
        dComIfG_resLoad(&mPhSoup, "SnowSoup"), dComIfG_resLoad(&mPhBarrel, "J_taru00"),
        dComIfG_resLoad(&mPhCrate, "Obj_kbox"),
    };
    for (cPhs_Step s : steps) {
        if (s == cPhs_ERROR_e) return cPhs_ERROR_e;
    }
    for (cPhs_Step s : steps) {
        if (s != cPhs_COMPLEATE_e) return s;
    }
    mLoaded = true;
    mCampNode = static_cast<int>(fopAcM_GetParam(this) & 0xFF);
    mSoupLevel = static_cast<int>((fopAcM_GetParam(this) >> 8) & 0x3);
    if (mSoupLevel < 1) mSoupLevel = 2;
    snapToGround();
    if (!fopAcM_entrySolidHeap(this, createHeapCallBack, 0x40000)) {
        return cPhs_ERROR_e;
    }
    // the cull box is measured from the fire itself (without this it sat at the world's origin and
    // the whole camp was always "off screen": only the flame, a particle, ever showed)
    placeModels();
    fopAcM_SetMtx(this, mpPart[0]->getBaseTRMtx());
    fopAcM_setCullSizeBox(this, -600.0f, -60.0f, -600.0f, 600.0f, 260.0f, 600.0f);
    Execute();
    return cPhs_COMPLEATE_e;
}

int psrFire_c::CreateHeap() {
    for (int i = 0; i < kPartNum; i++) {
        mpPart[i] = makeModel(kCampArc, kParts[i]);
        if (mpPart[i] == NULL) return 0;
    }
    mpSoup = makeModel("SnowSoup", kSoups[mSoupLevel - 1]);
    auto* bubbles = static_cast<J3DModelData*>(dComIfG_getObjectRes(kCampArc, kBubbles[mSoupLevel - 1]));
    auto* bubbling = static_cast<J3DAnmTextureSRTKey*>(dComIfG_getObjectRes(kCampArc, kBubbling));
    if (bubbles != NULL && bubbling != NULL) {
        mpBubbles = mDoExt_J3DModel__create(bubbles, 0x80000, 0x11000284);
        if (mpBubbles != NULL && !mBubbling.init(bubbles, bubbling, TRUE, J3DFrameCtrl::EMode_LOOP, 1.0f, 0, -1)) {
            return 0;
        }
    }
    const int seats = travellers::campSeatCount(mCampNode);
    for (int i = 0; i < seats && i < kStoolMax; i++) {
        mpStool[i] = makeModel(kPackArc, "psr_seat.bmd");
    }
    mpMat = makeModel(kCampArc, "psr_cookmat.bmd");
    for (int i = 0; i < kDecorNum; i++) {
        mpDecor[i] = kDecor[i].arc == 0 ? makeModel("J_taru00", "j_taru_00.bmd") : makeModel("Obj_kbox", "k_skiba_00.bmd");
    }
    return 1;
}

int psrFire_c::createHeapCallBack(fopAc_ac_c* actor) { return static_cast<psrFire_c*>(actor)->CreateHeap(); }

void psrFire_c::snapToGround() {
    dBgS_ObjGndChk gnd;
    cXyz probe = current.pos;
    probe.y += 400.0f;
    gnd.SetPos(&probe);
    const f32 ground = dComIfG_Bgsp().GroundCross(&gnd);
    if (ground != -G_CM3D_F_INF) {
        current.pos.y = home.pos.y = ground;
        mGrounded = true;
        tevStr.room_no = dComIfG_Bgsp().GetRoomId(gnd);
        tevStr.YukaCol = dComIfG_Bgsp().GetPolyColor(gnd);
    }
}

// Each barrel and the crate stands on the ground where it is (camps aren't perfectly flat).
void psrFire_c::groundDecor() {
    bool all = true;
    for (int i = 0; i < kDecorNum; i++) {
        const Decor& d = kDecor[i];
        const s16 yaw = shape_angle.y;
        cXyz probe(current.pos.x + d.x * cM_scos(yaw) + d.z * cM_ssin(yaw), current.pos.y + 400.0f,
                   current.pos.z - d.x * cM_ssin(yaw) + d.z * cM_scos(yaw));
        dBgS_ObjGndChk gnd;
        gnd.SetPos(&probe);
        const f32 ground = dComIfG_Bgsp().GroundCross(&gnd);
        if (ground == -G_CM3D_F_INF) {
            all = false;
            mDecorY[i] = current.pos.y;
        } else {
            mDecorY[i] = ground;
        }
    }
    mDecorGrounded = all;
}

void psrFire_c::placeModels() {
    mDoMtx_stack_c::transS(current.pos.x, current.pos.y, current.pos.z);
    mDoMtx_stack_c::YrotM(shape_angle.y);
    Mtx base;
    mDoMtx_copy(mDoMtx_stack_c::get(), base);
    for (J3DModel* m : mpPart) {
        m->setBaseTRMtx(base);
    }
    if (mpSoup != NULL) {
        mDoMtx_stack_c::copy(base);
        mDoMtx_stack_c::transM(0.0f, kSoupLevel, 0.0f);
        mDoMtx_stack_c::scaleM(kSoupScale, kSoupScale, kSoupScale);
        mDoMtx_stack_c::transM(-kYetoBroth[0], -kYetoBroth[1], -kYetoBroth[2]);
        mpSoup->setBaseTRMtx(mDoMtx_stack_c::get());
    }
    if (mpBubbles != NULL) {
        mDoMtx_stack_c::copy(base);
        mDoMtx_stack_c::transM(0.0f, 1.5f, 0.0f);   // just above the broth, under the chunks
        mDoMtx_stack_c::transM(-kCoroPot[0], -kCoroPot[1], -kCoroPot[2]);
        mpBubbles->setBaseTRMtx(mDoMtx_stack_c::get());
    }
    for (int i = 0; i < kStoolMax; i++) {
        cXyz at;
        s16 yaw;
        if (mpStool[i] == NULL || !travellers::campSeatSpot(mCampNode, i, &at, &yaw)) continue;
        mDoMtx_stack_c::transS(at.x, current.pos.y, at.z);
        mDoMtx_stack_c::YrotM(yaw);
        mDoMtx_stack_c::scaleM(kStoolWidth, kStoolHeight, kStoolWidth);
        mpStool[i]->setBaseTRMtx(mDoMtx_stack_c::get());
    }
    if (mpMat != NULL) {
        // the cook's mat: lying by the fire whether he's here or not
        cXyz at;
        s16 yaw;
        cook::matSpot(current.pos, shape_angle.y, &at, &yaw);
        dBgS_ObjGndChk gnd;
        cXyz probe(at.x, at.y + 300.0f, at.z);
        gnd.SetPos(&probe);
        const f32 ground = dComIfG_Bgsp().GroundCross(&gnd);
        mDoMtx_stack_c::transS(at.x, ground != -G_CM3D_F_INF ? ground : current.pos.y, at.z);
        mDoMtx_stack_c::YrotM(yaw);
        mpMat->setBaseTRMtx(mDoMtx_stack_c::get());
    }
    for (int i = 0; i < kDecorNum; i++) {
        if (mpDecor[i] == NULL) continue;
        const Decor& d = kDecor[i];
        const s16 yaw = shape_angle.y;
        mDoMtx_stack_c::transS(current.pos.x + d.x * cM_scos(yaw) + d.z * cM_ssin(yaw), mDecorY[i],
                               current.pos.z - d.x * cM_ssin(yaw) + d.z * cM_scos(yaw));
        mDoMtx_stack_c::YrotM(static_cast<s16>(yaw + d.turn));
        mDoMtx_stack_c::scaleM(d.scale, d.scale, d.scale);
        mpDecor[i]->setBaseTRMtx(mDoMtx_stack_c::get());
    }
}

// Coro's flame, glow and crackle (Obj_ktOnFire), lit by the clock instead of Link's lantern.
void psrFire_c::updateFire() {
    mFlamePos = current.pos;
    mFlamePos.y += kFlameY;
    const bool want = fireTime();
    if (want && !mLit) {
        mLight.mPosition = mFlamePos;
        mLight.mPosition.y += 10.0f;
        mLight.mColor.r = 188;
        mLight.mColor.g = 102;
        mLight.mColor.b = 66;
        mLight.mPow = 500.0f;
        mLight.mFluctuation = 1.0f;
        dKy_plight_set(&mLight);
        mLit = true;
    } else if (!want && mLit) {
        dKy_plight_cut(&mLight);
        mLit = false;
    }
    if (mLit) {
        mFlame[0] = dComIfGp_particle_set(mFlame[0], 0x3AD, &mFlamePos, NULL, NULL);
        mFlame[1] = dComIfGp_particle_set(mFlame[1], 0x3AF, &mFlamePos, NULL, NULL);
        mFlame[2] = dComIfGp_particle_set(mFlame[2], 0x3AE, &mFlamePos, NULL, NULL);
        mDoAud_seStartLevel(Z2SE_OBJ_FIRE_BURNING, &mFlamePos, 0, dComIfGp_getReverb(fopAcM_GetRoomNo(this)));
        if (mpBubbles != NULL) {
            mBubbling.play();   // the soup only bubbles over a lit fire
        }
    }
}

// Scooping, the way Coro's pot does it (Obj_KiPot): offer the catch to an empty bottle while no
// other event runs; once per camp per day.
void psrFire_c::updateSoup() {
    attention_info.position = current.pos;
    attention_info.position.y += 150.0f;
    eyePos = current.pos;
    eyePos.y += kSoupLevel;
    attention_info.flags = 0;

    const bool catching = eventInfo.checkCommandCatch() != 0;
    if (catching && !mCatching) {
        markScooped(mCampNode);   // Link's bottle went in: that's this camp's soup for today
    }
    mCatching = catching;
    if (!scoopedToday(mCampNode) && dComIfGp_getEvent()->isOrderOK() && !catching) {
        dComIfGp_att_CatchRequest(this, kSoupItem[mSoupLevel - 1], 100.0f, 50.0f, -50.0f, 0x2000, 1);
        eventInfo.onCondition(dEvtCnd_40_e);
    }
}

// A second try the same day: the camp cook comes (or, if he's here, gets up) to yell.
void psrFire_c::updateGreedy() {
    daAlink_c* link = daAlink_getAlinkActorClass();
    if (link == NULL) return;
    if (mGreedyCooldown > 0) --mGreedyCooldown;
    const bool swinging = link->mProcID == daAlink_c::PROC_BOTTLE_SWING;   // only ever an empty bottle
    const bool started = swinging && !mSwinging;
    mSwinging = swinging;
    if (!started || mGreedyCooldown > 0 || !scoopedToday(mCampNode)) return;
    const f32 dx = link->current.pos.x - current.pos.x, dz = link->current.pos.z - current.pos.z;
    if (dx * dx + dz * dz > kScoopReach * kScoopReach) return;
    cook::onGreedy(mCampNode, current.pos, shape_angle.y);
    mGreedyCooldown = 60;
}

int psrFire_c::Execute() {
    if (!mGrounded) snapToGround();
    if (!mDecorGrounded) groundDecor();
    placeModels();
    updateFire();
    updateSoup();
    updateGreedy();
    return 1;
}

int psrFire_c::Draw() {
    g_env_light.settingTevStruct(0x20, &current.pos, &tevStr);
    auto draw = [&](J3DModel* m) {
        if (m == NULL) return;
        g_env_light.setLightTevColorType_MAJI(m, &tevStr);
        mDoExt_modelUpdateDL(m);
    };
    for (J3DModel* m : mpPart) draw(m);
    draw(mpSoup);
    if (mpBubbles != NULL && mLit) {
        mBubbling.entry(mpBubbles->getModelData());
        draw(mpBubbles);
    }
    for (int i = 0; i < kStoolMax; i++) {
        if (!travellers::campSeatInUse(mCampNode, i)) draw(mpStool[i]);   // a sitter draws its own
    }
    draw(mpMat);
    for (J3DModel* m : mpDecor) draw(m);
    return 1;
}

namespace {
cPhs_Step psrFire_Create(void* p) { return static_cast<psrFire_c*>(p)->create(); }
int psrFire_Delete(void* p) {
    static_cast<psrFire_c*>(p)->~psrFire_c();
    return 1;
}
int psrFire_Execute(void* p) { return static_cast<psrFire_c*>(p)->Execute(); }
int psrFire_Draw(void* p) { return static_cast<psrFire_c*>(p)->Draw(); }
int psrFire_IsDelete(void*) { return 1; }
}  // namespace

s16 psrFire_c::sProcName = -1;
ActorHandle psrFire_c::sActorHandle = -1;
const ActorProfileDesc psrFire_c::sProfile = {
    .name = PSR_FIRE_NAME,
    .priority_group = 7,
    .process_size = sizeof(psrFire_c),
    .draw_priority = fpcDwPi_OBJ_LBOX_e,
    .status = fopAcStts_UNK_0x40000_e | fopAcStts_UNK_0x4000_e | fopAcStts_CULL_e,
    .group = fopAc_ACTOR_e,
    .cull_type = fopAc_CULLBOX_CUSTOM_e,
    .create_function = psrFire_Create,
    .delete_function = psrFire_Delete,
    .execute_function = psrFire_Execute,
    .is_delete_function = psrFire_IsDelete,
    .draw_function = psrFire_Draw,
};

namespace psr_fire {
ModResult registerActor() {
    svc_save->observe_saves(mod_ctx, clearScoops, loadScoops, nullptr, nullptr, nullptr);
    return svc_actor->register_actor(mod_ctx, &psrFire_c::sProfile, &psrFire_c::sProcName,
                                     &psrFire_c::sActorHandle);
}
}  // namespace psr_fire
