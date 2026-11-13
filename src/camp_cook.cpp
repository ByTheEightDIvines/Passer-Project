#include "global.h"

#include "camp_cook.hpp"
#include "travellers.hpp"

#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_npc_cd2.h"
#include "d/actor/d_a_npc_passer.h"
#include "d/d_com_inf_game.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_draw_priority.h"
#include "f_pc/f_pc_name.h"
#include "m_Do/m_Do_mtx.h"
#include "mods/service.hpp"
#include "mods/svc/log.h"
#include "SSystem/SComponent/c_math.h"

#include <cmath>

namespace {
constexpr u8 kCookType = 9;               // the big guy with the moustache (MAD_a2), in his own clothes
constexpr f32 kBehindLink = 130.0f;       // he turns up this far behind Link
constexpr f32 kWalkUp = 160.0f;           // he walks up to this distance before yelling again
constexpr f32 kLeaveDistance = 4000.0f;   // Link this far away: he's gone (and his temper resets)
constexpr f32 kScaredOf = 900.0f;         // a monster this close sends him running
constexpr f32 kSafeAgain = 1600.0f;
constexpr int kSafeFrames = 180;
constexpr f32 kSpoonReach = 115.0f;
constexpr int kSwingFrames = 42;          // one whack, wind-up to recovery
constexpr int kSwingHit = 9;              // the frame the spoon lands
constexpr u8 kSpoonPower = 4;             // a Bokoblin's club is 1: he hits HARD (two hearts)
constexpr int kLastLine = 3;              // "THAT'S IT!" - and then the spoon
constexpr u8 kAnmIdle = 0, kAnmWalk = 2, kAnmRun = 4, kAnmSit = 17;   // 17: the heavyset sit
constexpr f32 kMatAngle = 40.0f, kMatOut = 340.0f;   // his mat: front-right of the fire, past the stools

enum State : u8 { S_ARRIVE, S_TALK, S_TO_MAT, S_SIT, S_GETUP, S_ATTACK, S_FLEE };

fpc_ProcID g_brain = fpcM_ERROR_PROCESS_ID_e;
fpc_ProcID g_body = fpcM_ERROR_PROCESS_ID_e;
int g_lines = 0;                // lines he has yelled since he turned up
bool g_spoonOut = false;
f32 g_swing = 0.0f;
bool g_greedyAgain = false;     // Link tried again while he was on his way to (or on) his mat
// where the brain should go: set by onGreedy just before it is created
int g_camp = -1;
cXyz g_fire;
s16 g_campYaw = 0;

const dCcD_SrcSph kSpoonSrc = {
    {
        {0x0, {{AT_TYPE_CSTATUE_SWING, kSpoonPower, 0x1d}, {0x0, 0x0}, 0x0}},   // hits Link
        {dCcD_SE_WOOD, 0x0, 0x0, 0x0, 0x0},
        {dCcD_SE_NONE, 0x0, 0x0, 0x0, 0x0},
        {0x0},
    },
    {{{0.0f, 0.0f, 0.0f}, 45.0f}},
};

daNpcPasser_c* body() { return static_cast<daNpcPasser_c*>(fopAcM_SearchByID(g_body)); }

f32 distXZ(const cXyz& a, const cXyz& b) { return std::hypot(a.x - b.x, a.z - b.z); }
}  // namespace

class psrCook_c : public fopAc_ac_c {
public:
    u8 mState = S_ARRIVE;
    u8 mBefore = S_TO_MAT;   // what he goes back to after running from a monster
    u8 mAnim = 0xFF;
    int mTimer = 0;
    int mCalm = 0;
    int mSwing = -1;
    bool mHit = false;
    bool mFlowStarted = false;
    bool mSitting = false;
    cXyz mMat;
    s16 mMatYaw = 0;
    int mCamp = -1;
    dMsgFlow_c mFlow;
    dCcD_Sph mSpoon;

    virtual ~psrCook_c() {
        if (g_brain == fopAcM_GetID(this)) {
            g_brain = fpcM_ERROR_PROCESS_ID_e;
            g_lines = 0;
            g_spoonOut = false;
            g_swing = 0.0f;
            g_greedyAgain = false;
        }
    }

    cPhs_Step create() {
        fopAcM_ct(this, psrCook_c);
        mCamp = g_camp;
        cook::matSpot(g_fire, g_campYaw, &mMat, &mMatYaw);
        // his body: a townsfolk actor walking a short path of its own (from behind Link to his mat)
        travellers::setCookPath(current.pos, mMat);
        const u32 params = kCookType | (static_cast<u32>(cook::kPathId) << 16) | (1u << 24);
        const csXyz angle(0xFF, shape_angle.y, 0);
        cXyz at = current.pos;
        g_body = fopAcM_create(fpcNm_NPC_PASSER_e, params, &at, fopAcM_GetRoomNo(this), &angle, nullptr, -1);
        g_brain = fopAcM_GetID(this);
        mSpoon.Set(kSpoonSrc);
        attention_info.flags = 0;
        return cPhs_COMPLEATE_e;
    }

    void anim(daNpcPasser_c* b, u8 anm) {
        if (mAnim == anm) return;
        mAnim = anm;
        auto* a = static_cast<J3DAnmTransformKey*>(b->getAnmP(anm, anm == kAnmSit ? 0 : b->getObjNum()));
        if (a != nullptr) b->setAnm(a, 1.0f, 8.0f, J3DFrameCtrl::EMode_LOOP, 0, -1);
    }

    void face(daNpcPasser_c* b, const cXyz& to) {
        cLib_addCalcAngleS2(&b->current.angle.y, cLib_targetAngleY(&b->current.pos, &to), 4, 0x1000);
        b->shape_angle.y = b->current.angle.y;
    }

    void step(daNpcPasser_c* b, const cXyz& to, f32 speed) {
        face(b, to);
        b->current.pos.x += cM_ssin(b->current.angle.y) * speed;
        b->current.pos.z += cM_scos(b->current.angle.y) * speed;
        b->mAcch.CrrPos(dComIfG_Bgsp());
    }

    void standUp(daNpcPasser_c* b) {
        if (!mSitting) return;
        mSitting = false;
        b->current.pos.y += 37.2f * Cd2_HIO_objScale(kCookType);   // back up off the mat
        b->mAcch.CrrPos(dComIfG_Bgsp());
    }

    // His spoon lands: a hit on whatever is in front of him at spoon height (only Link can take it).
    void whack(daNpcPasser_c* b) {
        cXyz at = b->current.pos;
        at.x += cM_ssin(b->shape_angle.y) * 75.0f;
        at.y += 90.0f;
        at.z += cM_scos(b->shape_angle.y) * 75.0f;
        mSpoon.SetStts(&b->mStts);
        mSpoon.SetC(at);
        dComIfG_Ccsp()->Set(&mSpoon);
    }

    int Execute() {
        daNpcPasser_c* b = body();
        if (b == nullptr) {
            if (++mTimer > 150) fopAcM_delete(this);   // his body never appeared, or was taken away
            return 1;
        }
        daAlink_c* link = daAlink_getAlinkActorClass();
        if (link == nullptr) return 1;
        b->speedF = 0.0f;
        current.pos = b->current.pos;
        eyePos = current.pos;
        eyePos.y += 200.0f;   // his head: what the talk camera looks at
        attention_info.position = eyePos;
        attention_info.flags = 0;
        const bool event = dComIfGp_event_runCheck();

        // Link left, or Link died: he goes, and his temper with him
        if (!event && (dComIfGs_getLife() == 0 || distXZ(link->current.pos, b->current.pos) > kLeaveDistance)) {
            fopAcM_delete(b);
            fopAcM_delete(this);
            return 1;
        }
        // monsters: he's a cook, not a fighter
        if (mState != S_TALK && mState != S_FLEE) {
            if (fopAc_ac_c* m = nearestMonster(b->current.pos, kScaredOf)) {
                (void)m;
                standUp(b);
                mBefore = mState == S_ATTACK ? S_ATTACK : S_TO_MAT;
                mState = S_FLEE;
                mCalm = 0;
                g_swing = 0.0f;
                mSwing = -1;
            }
        }

        switch (mState) {
        case S_ARRIVE:
            anim(b, kAnmIdle);
            face(b, link->current.pos);
            if (++mTimer >= 12) { mState = S_TALK; mTimer = 0; mFlowStarted = false; }
            break;

        case S_TALK: {
            // a conversation with him: the game stops Link and turns him to face the cook
            anim(b, kAnmIdle);
            face(b, link->current.pos);
            fopAcM_OnStatus(b, fopAcStts_NOPAUSE_e);
            if (!eventInfo.checkCommandTalk()) {
                if (++mTimer > 300) { mState = S_TO_MAT; mTimer = 0; }   // the game never let him talk
                eventInfo.onCondition(dEvtCnd_CANTALK_e);
                fopAcM_orderSpeakEvent(this, 0, 0);
                break;
            }
            if (!mFlowStarted) {
                mFlow.init(this, cookFlowNode(g_lines < kLastLine ? g_lines : kLastLine), 0, NULL);
                mFlowStarted = true;
            }
            if (mFlow.doFlow(this, NULL, 0) != 0) {
                dComIfGp_event_reset();
                fopAcM_OffStatus(b, fopAcStts_NOPAUSE_e);
                const int said = g_lines++;
                mTimer = 0;
                if (said >= kLastLine) {
                    mState = S_ATTACK;
                    g_spoonOut = true;
                } else {
                    mState = S_TO_MAT;
                }
                g_greedyAgain = false;
            }
            break;
        }

        case S_TO_MAT:
            if (g_greedyAgain) { mState = S_GETUP; break; }
            if (distXZ(b->current.pos, mMat) > 25.0f) {
                anim(b, kAnmWalk);
                step(b, mMat, Cd2_HIO_walkMaxSpeed(kCookType));
                break;
            }
            b->current.pos.x = mMat.x;
            b->current.pos.z = mMat.z;
            b->current.angle.y = b->shape_angle.y = mMatYaw;
            mState = S_SIT;
            break;

        case S_SIT:
            if (g_greedyAgain) { standUp(b); mState = S_GETUP; break; }
            if (!mSitting) {
                // down on his mat: the sitting pose is made for a stool, so he sits that much lower
                mSitting = true;
                b->current.pos.y -= 37.2f * Cd2_HIO_objScale(kCookType);
            }
            anim(b, kAnmSit);
            break;

        case S_GETUP:
            // up off the mat and over to Link
            if (distXZ(b->current.pos, link->current.pos) > kWalkUp) {
                anim(b, kAnmWalk);
                step(b, link->current.pos, Cd2_HIO_walkMaxSpeed(kCookType) * 1.3f);
                break;
            }
            mState = S_TALK;
            mTimer = 0;
            mFlowStarted = false;
            g_greedyAgain = false;
            break;

        case S_ATTACK: {
            g_spoonOut = true;
            const f32 d = distXZ(b->current.pos, link->current.pos);
            if (mSwing < 0 && d > kSpoonReach) {
                anim(b, kAnmRun);
                step(b, link->current.pos, Cd2_HIO_maxSpeed(kCookType) * 1.25f);
                g_swing = 0.0f;
                break;
            }
            anim(b, kAnmIdle);
            face(b, link->current.pos);
            if (mSwing < 0) { mSwing = 0; mHit = false; }
            // wind up, smack, recover
            const int f = mSwing++;
            g_swing = f < kSwingHit ? f / static_cast<f32>(kSwingHit)
                                    : std::max(0.0f, 1.0f - (f - kSwingHit) / 18.0f);
            if (f >= kSwingHit && f <= kSwingHit + 2) whack(b);
            if (mSwing >= kSwingFrames) mSwing = -1;
            break;
        }

        case S_FLEE: {
            fopAc_ac_c* m = nearestMonster(b->current.pos, kSafeAgain);
            if (m != nullptr) {
                mCalm = 0;
                cXyz away = b->current.pos;
                const s16 a = cLib_targetAngleY(&m->current.pos, &b->current.pos);
                away.x += cM_ssin(a) * 300.0f;
                away.z += cM_scos(a) * 300.0f;
                anim(b, kAnmRun);
                step(b, away, Cd2_HIO_maxSpeed(kCookType) * 1.3f);
            } else if (++mCalm >= kSafeFrames) {
                mState = mBefore;
            } else {
                anim(b, kAnmIdle);
            }
            break;
        }
        }
        return 1;
    }
};

namespace {
cPhs_Step cook_Create(void* p) { return static_cast<psrCook_c*>(p)->create(); }
int cook_Delete(void* p) {
    static_cast<psrCook_c*>(p)->~psrCook_c();
    return 1;
}
int cook_Execute(void* p) { return static_cast<psrCook_c*>(p)->Execute(); }
int cook_Draw(void*) { return 1; }
int cook_IsDelete(void*) { return 1; }

s16 s_procName = -1;
ActorHandle s_handle = -1;
const ActorProfileDesc kProfile = {
    .name = PSR_COOK_NAME,
    .priority_group = 7,
    .process_size = sizeof(psrCook_c),
    .draw_priority = fpcDwPi_OBJ_LBOX_e,
    .status = fopAcStts_UNK_0x40000_e | fopAcStts_UNK_0x4000_e,
    .group = fopAc_ACTOR_e,
    .cull_type = fopAc_CULLBOX_CUSTOM_e,
    .create_function = cook_Create,
    .delete_function = cook_Delete,
    .execute_function = cook_Execute,
    .is_delete_function = cook_IsDelete,
    .draw_function = cook_Draw,
};
}  // namespace

namespace cook {

void matSpot(const cXyz& fire, s16 campYaw, cXyz* pos, s16* yaw) {
    const s16 a = static_cast<s16>(campYaw + static_cast<s16>(kMatAngle * 0x8000 / 180.0f));
    if (pos) pos->set(fire.x + cM_ssin(a) * kMatOut, fire.y, fire.z + cM_scos(a) * kMatOut);
    if (yaw) *yaw = static_cast<s16>(a + 0x8000);   // facing the fire
}

void onGreedy(int campNode, const cXyz& fire, s16 campYaw) {
    if (g_brain != fpcM_ERROR_PROCESS_ID_e && fopAcM_SearchByID(g_brain) != nullptr) {
        g_greedyAgain = true;   // he's here already: up he gets
        return;
    }
    daAlink_c* link = daAlink_getAlinkActorClass();
    if (link == nullptr) return;
    g_camp = campNode;
    g_fire = fire;
    g_campYaw = campYaw;
    g_lines = 0;
    // right behind Link, facing his back
    const s16 yaw = link->shape_angle.y;
    ActorSpawnParams p{};
    p.parameters = static_cast<u32>(campNode);
    p.argument = -1;
    p.room_num = static_cast<s8>(dComIfGp_roomControl_getStayNo());
    p.position.x = link->current.pos.x - cM_ssin(yaw) * kBehindLink;
    p.position.y = link->current.pos.y;
    p.position.z = link->current.pos.z - cM_scos(yaw) * kBehindLink;
    p.angle.y = yaw;
    p.scale.x = p.scale.y = p.scale.z = 1.0f;
    ActorId id{};
    svc_actor->create_actor_from_name(mod_ctx, PSR_COOK_NAME, &p, &id);
}

bool isBody(const fopAc_ac_c* actor) {
    if (actor == nullptr || g_body == fpcM_ERROR_PROCESS_ID_e) return false;
    if (fopAcM_GetName(const_cast<fopAc_ac_c*>(actor)) != fpcNm_NPC_PASSER_e) return false;
    if (((fopAcM_GetParam(actor) >> 16) & 0xFF) != static_cast<u32>(kPathId)) return false;
    return fopAcM_GetID(const_cast<fopAc_ac_c*>(actor)) == g_body;
}

bool spoonOut() { return g_spoonOut; }
f32 spoonSwing() { return g_swing; }

ModResult registerActor() { return svc_actor->register_actor(mod_ctx, &kProfile, &s_procName, &s_handle); }

}  // namespace cook
