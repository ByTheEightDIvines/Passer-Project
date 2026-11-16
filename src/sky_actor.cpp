#include "global.h"

#include "sky_actor.hpp"

#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_draw_priority.h"
#include "m_Do/m_Do_ext.h"
#include "m_Do/m_Do_mtx.h"
#include "mods/service.hpp"


namespace {
const char* kArc = "PsrSky";
const char* kBox[4] = {"sky_m.bmd", "sky_d.bmd", "sky_e.bmd", "sky_n.bmd"};
constexpr u32 kHeapSize = 0x40000;
}  // namespace

namespace psr_sky {
int boxForHour(int h) {
    if (h >= 5 && h < 9) return 0;
    if (h >= 9 && h < 16) return 1;
    if (h >= 16 && h < 19) return 2;
    return 3;
}
}  // namespace psr_sky

psrSky_c::~psrSky_c() { dComIfG_resDelete(&mPhase, kArc); }

int psrSky_c::CreateHeap() {
    for (int i = 0; i < 4; i++) {
        auto* data = static_cast<J3DModelData*>(dComIfG_getObjectRes(kArc, kBox[i]));
        mpBox[i] = data != NULL ? mDoExt_J3DModel__create(data, 0x80000, 0x11000084) : NULL;
    }
    return 1;
}

static int skyCreateHeap(fopAc_ac_c* a) { return static_cast<psrSky_c*>(a)->CreateHeap(); }

cPhs_Step psrSky_c::create() {
    fopAcM_ct(this, psrSky_c);
    const cPhs_Step step = dComIfG_resLoad(&mPhase, kArc);
    if (step != cPhs_COMPLEATE_e) return step;
    for (J3DModel*& m : mpBox) m = NULL;
    if (!fopAcM_entrySolidHeap(this, skyCreateHeap, kHeapSize)) return cPhs_ERROR_e;
    fopAcM_setCullSizeBox(this, -3000.0f, -3000.0f, -3000.0f, 3000.0f, 3000.0f, 3000.0f);
    Execute();
    return step;
}

int psrSky_c::Execute() {
    mDoMtx_stack_c::transS(current.pos.x, current.pos.y, current.pos.z);
    for (J3DModel* m : mpBox) {
        if (m != NULL) m->setBaseTRMtx(mDoMtx_stack_c::get());
    }
    return 1;
}

int psrSky_c::Draw() {
    // pre-rendered with its own light: drawn as painted, without the shop's lighting
    if (J3DModel* m = mpBox[psr_sky::boxForHour(dKy_getdaytime_hour())]) {
        dComIfGd_setListBG();
        mDoExt_modelUpdateDL(m);
        dComIfGd_setList();
    }
    return 1;
}

namespace {
cPhs_Step psrSky_Create(void* p) { return static_cast<psrSky_c*>(p)->create(); }
int psrSky_Delete(void* p) { static_cast<psrSky_c*>(p)->~psrSky_c(); return 1; }
int psrSky_Execute(void* p) { return static_cast<psrSky_c*>(p)->Execute(); }
int psrSky_Draw(void* p) { return static_cast<psrSky_c*>(p)->Draw(); }
int psrSky_IsDelete(void*) { return 1; }
}  // namespace

s16 psrSky_c::sProcName = -1;
ActorHandle psrSky_c::sActorHandle = -1;
const ActorProfileDesc psrSky_c::sProfile = {
    .name = PSR_SKY_NAME,
    .priority_group = 7,
    .process_size = sizeof(psrSky_c),
    .draw_priority = fpcDwPi_OBJ_LBOX_e,
    .status = fopAcStts_UNK_0x40000_e | fopAcStts_UNK_0x4000_e | fopAcStts_CULL_e,
    .group = fopAc_ACTOR_e,
    .cull_type = fopAc_CULLBOX_CUSTOM_e,
    .create_function = psrSky_Create,
    .delete_function = psrSky_Delete,
    .execute_function = psrSky_Execute,
    .is_delete_function = psrSky_IsDelete,
    .draw_function = psrSky_Draw,
};

namespace psr_sky {
ModResult registerActor() {
    return svc_actor->register_actor(mod_ctx, &psrSky_c::sProfile, &psrSky_c::sProcName, &psrSky_c::sActorHandle);
}
}  // namespace psr_sky
