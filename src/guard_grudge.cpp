#include "global.h"

#include "guard_grudge.hpp"
#include "travellers.hpp"

#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_npc_passer.h"
#include "d/d_com_inf_game.h"
#include "d/d_msg_flow.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_draw_priority.h"
#include "f_pc/f_pc_name.h"
#include "f_pc/f_pc_searcher.h"
#include "mods/service.hpp"
#include "mods/svc/actor.h"
#include "mods/svc/log.h"
#include "SSystem/SComponent/c_math.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#define PSR_YELL_NAME "PsrYell"

namespace {

constexpr int kCoolOff = 30 * 60;            // a minute without a bomb: the count starts again
constexpr int kSoreFor = 30 * 60 * 8;        // eight minutes of sore lines after they lost
constexpr int kLostSight = 30 * 8;           // Link this long out of every soldier's reach: let go
constexpr f32 kSquadReach = 3500.0f;         // a soldier this near Link is still in the fight
constexpr int kLinkLastHeart = 4;            // Link's life in quarter hearts: on his last heart, they stop
constexpr f32 kRunYell = 1800.0f;            // Link breaking away this far from the nearest: one yells after him

enum Line : int {
    L_WARN1, L_WARN2, L_ATTACK,
    L_YIELD_CAPTAIN, L_YIELD_BARRACKS, L_YIELD_OW, L_YIELD_EASY,
    L_LESSON,
    L_RUN_WHERE, L_RUN_BREATH,
    L_COUNT
};
// [big][rest]: the first part shouted (bigger), as the camp cook does
const char* const kLines[L_COUNT][2] = {
    {"Oi!", " Watch where you're throwing those!"},
    {"That's your last warning, lad.", " Put\nthem away."},
    {"Right!", " You asked for it!"},
    {"Mercy! I yield!", " ...Don't tell the\ncaptain."},
    {"Lost to a lad in a green tunic...", "\nthe lads at the barracks will never\nlet me hear the end of it."},
    {"Ow. Ow. Right. You win.", " Can we stop\nnow? Please?"},
    {"I was going easy on you!", " ...I was\ngoing easy on you."},
    {"...Let that be a lesson.", ""},
    {"Ay! Where are you going?!", " We weren't\ndone! ...Well, I was done, but still!"},
    {"Get back here!", " ...No, wait, don't. I'm\nout of breath."},
};
std::vector<mods::flow::RegisteredMessage> g_msgs;
u16 g_nodes[L_COUNT];
bool g_ready = false;

struct Squad {
    int warnings = 0;
    int lastBomb = -100000;
    std::array<fpc_ProcID, 6> bombs{};   // bombs already counted (one bomb, one strike, however many it scares)
    int nextBomb = 0;
    bool hostile = false;
    int lastNear = 0;
    int soreUntil = -1;
    int ranUntil = -1;      // Link ran off mid-fight: they say so for a while
};
std::array<Squad, 3> g_sq{};
int g_frame = 0;
int g_yieldTurn = 0;
int g_runTurn = 0;

// the yell waiting for its actor: who says what
fpc_ProcID g_speaker = fpcM_ERROR_PROCESS_ID_e;
int g_line = -1;
bool g_yelling = false;

f32 distXZ(const cXyz& a, const cXyz& b) { return std::hypot(a.x - b.x, a.z - b.z); }

// The nearest soldier of a squad to a spot (out in the world), and whether any is within r.
daNpcPasser_c* nearestOf(int squad, const cXyz& at, f32 r) {
    struct S { int squad; const cXyz* at; f32 best; daNpcPasser_c* res; } s{squad, &at, r * r, nullptr};
    fpcM_Search(
        [](void* p, void* d) -> void* {
            auto* s = static_cast<S*>(d);
            if (!fopAcM_IsActor(p)) return nullptr;
            auto* a = static_cast<fopAc_ac_c*>(p);
            if (fopAcM_GetName(a) != fpcNm_NPC_PASSER_e || grudge::squadOf(a) != s->squad) return nullptr;
            if (travellers::life(a) == 0) return nullptr;
            const f32 dx = a->current.pos.x - s->at->x, dz = a->current.pos.z - s->at->z;
            if (dx * dx + dz * dz < s->best) {
                s->best = dx * dx + dz * dz;
                s->res = static_cast<daNpcPasser_c*>(a);
            }
            return nullptr;
        },
        &s);
    return s.res;
}

void yell(daNpcPasser_c* who, int line);

void standDown(Squad& q) {
    q.hostile = false;
    q.warnings = 0;
}

}  // namespace

// The one who's shouting: an actor of its own that holds a conversation (so the game stops Link and
// turns him to face the soldier, as with the camp cook), then goes.
class psrYell_c : public fopAc_ac_c {
public:
    fpc_ProcID mWho = fpcM_ERROR_PROCESS_ID_e;
    int mLine = -1;
    int mTimer = 0;
    bool mStarted = false;
    dMsgFlow_c mFlow;

    cPhs_Step create() {
        fopAcM_ct(this, psrYell_c);
        mWho = g_speaker;
        mLine = g_line;
        attention_info.flags = 0;
        return cPhs_COMPLEATE_e;
    }
    virtual ~psrYell_c() { g_yelling = false; }

    int Execute() {
        auto* who = static_cast<daNpcPasser_c*>(fopAcM_SearchByID(mWho));
        if (who == nullptr || mLine < 0 || !g_ready) {
            fopAcM_delete(this);
            return 1;
        }
        current.pos = who->current.pos;
        eyePos = current.pos;
        eyePos.y += 170.0f;
        attention_info.position = eyePos;
        attention_info.flags = 0;
        if (!eventInfo.checkCommandTalk()) {
            if (++mTimer > 150) { fopAcM_delete(this); return 1; }   // the game never let him speak
            eventInfo.onCondition(dEvtCnd_CANTALK_e);
            fopAcM_orderSpeakEvent(this, 0, 0);
            return 1;
        }
        // facing Link while he says it
        if (daAlink_c* link = daAlink_getAlinkActorClass()) {
            const s16 yaw = cLib_targetAngleY(&who->current.pos, &link->current.pos);
            who->current.angle.y = who->shape_angle.y = yaw;
        }
        if (!mStarted) {
            mFlow.init(this, g_nodes[mLine], 0, NULL);
            mStarted = true;
        }
        if (mFlow.doFlow(this, NULL, 0) != 0) {
            dComIfGp_event_reset();
            fopAcM_delete(this);
        }
        return 1;
    }
};

namespace {
void yell(daNpcPasser_c* who, int line) {
    if (who == nullptr || !g_ready || g_yelling) return;
    g_speaker = fopAcM_GetID(who);
    g_line = line;
    ActorSpawnParams p{};
    p.parameters = 0;
    p.argument = -1;
    p.room_num = static_cast<s8>(fopAcM_GetRoomNo(who));
    p.position.x = who->current.pos.x;
    p.position.y = who->current.pos.y;
    p.position.z = who->current.pos.z;
    p.scale.x = p.scale.y = p.scale.z = 1.0f;
    ActorId id{};
    if (svc_actor->create_actor_from_name(mod_ctx, PSR_YELL_NAME, &p, &id) == MOD_OK) g_yelling = true;
    travellers::diagLine("grudge  slot %d says line %d", travellers::slotIndex(who), line);
}

cPhs_Step yell_Create(void* p) { return static_cast<psrYell_c*>(p)->create(); }
int yell_Delete(void* p) {
    static_cast<psrYell_c*>(p)->~psrYell_c();
    return 1;
}
int yell_Execute(void* p) { return static_cast<psrYell_c*>(p)->Execute(); }
int yell_Draw(void*) { return 1; }
int yell_IsDelete(void*) { return 1; }

s16 s_procName = -1;
ActorHandle s_handle = -1;
const ActorProfileDesc kProfile = {
    .name = PSR_YELL_NAME,
    .priority_group = 7,
    .process_size = sizeof(psrYell_c),
    .draw_priority = fpcDwPi_OBJ_LBOX_e,
    .status = fopAcStts_UNK_0x40000_e | fopAcStts_UNK_0x4000_e,
    .group = fopAc_ACTOR_e,
    .cull_type = fopAc_CULLBOX_CUSTOM_e,
    .create_function = yell_Create,
    .delete_function = yell_Delete,
    .execute_function = yell_Execute,
    .is_delete_function = yell_IsDelete,
    .draw_function = yell_Draw,
};
}  // namespace

namespace grudge {

int squadOf(const fopAc_ac_c* actor) {
    if (actor == nullptr) return 0;
    if (travellers::isGuard(actor)) return 1;
    return travellers::guardStyle(actor) != 0 ? 2 : 0;
}

bool squadHostile(int squad) { return squad > 0 && squad < 3 && g_sq[squad].hostile; }
bool hostile(const fopAc_ac_c* soldier) { return squadHostile(squadOf(soldier)); }

void onBomb(daNpcPasser_c* soldier, const fopAc_ac_c* bomb) {
    const int s = squadOf(soldier);
    if (s == 0 || bomb == nullptr) return;
    Squad& q = g_sq[s];
    if (q.hostile) return;
    const fpc_ProcID id = fopAcM_GetID(const_cast<fopAc_ac_c*>(bomb));
    for (fpc_ProcID b : q.bombs) {
        if (b == id) return;   // that bomb's already been counted
    }
    q.bombs[q.nextBomb++ % q.bombs.size()] = id;
    if (g_frame - q.lastBomb > kCoolOff) q.warnings = 0;
    q.lastBomb = g_frame;
    ++q.warnings;
    travellers::diagLine("grudge  squad %d: bomb %d", s, q.warnings);
    if (q.warnings == 1) {
        yell(soldier, L_WARN1);
    } else if (q.warnings == 2) {
        yell(soldier, L_WARN2);
    } else {
        yell(soldier, L_ATTACK);
        q.hostile = true;
        q.lastNear = g_frame;
        q.soreUntil = -1;
    }
}

void yield(daNpcPasser_c* soldier) {
    const int s = squadOf(soldier);
    if (s == 0 || !g_sq[s].hostile) return;
    Squad& q = g_sq[s];
    standDown(q);
    q.soreUntil = g_frame + kSoreFor;
    q.ranUntil = -1;
    static const int kYields[4] = {L_YIELD_CAPTAIN, L_YIELD_BARRACKS, L_YIELD_OW, L_YIELD_EASY};
    yell(soldier, kYields[g_yieldTurn++ % 4]);
    travellers::diagLine("grudge  squad %d yields to Link", s);
}

void tick() {
    ++g_frame;
    daAlink_c* link = daAlink_getAlinkActorClass();
    for (int s = 1; s < 3; ++s) {
        Squad& q = g_sq[s];
        if (!q.hostile || link == nullptr) continue;
        if (dComIfGp_event_runCheck()) { q.lastNear = g_frame; continue; }
        daNpcPasser_c* near = nearestOf(s, link->current.pos, kSquadReach);
        if (near != nullptr) q.lastNear = g_frame;
        if (dComIfGs_getLife() <= kLinkLastHeart) {
            // Link's the one down to his last heart: that's enough
            standDown(q);
            if (near != nullptr) yell(near, L_LESSON);
            travellers::diagLine("grudge  squad %d: Link's beaten", s);
        } else if (near != nullptr && distXZ(near->current.pos, link->current.pos) > kRunYell) {
            // he's breaking away: the nearest yells after him, and they give it up
            standDown(q);
            q.ranUntil = g_frame + kSoreFor;
            yell(near, (g_runTurn++ % 2) == 0 ? L_RUN_WHERE : L_RUN_BREATH);
            travellers::diagLine("grudge  squad %d: Link ran off", s);
        } else if (g_frame - q.lastNear > kLostSight) {
            standDown(q);   // he ran off: they let him go
            travellers::diagLine("grudge  squad %d: Link got away", s);
        }
    }
}

bool ranOff(const fopAc_ac_c* soldier) {
    const int s = squadOf(soldier);
    return s != 0 && g_sq[s].ranUntil >= 0 && g_frame < g_sq[s].ranUntil;
}

bool sore(const fopAc_ac_c* soldier) {
    const int s = squadOf(soldier);
    return s != 0 && g_sq[s].soreUntil >= 0 && g_frame < g_sq[s].soreUntil;
}

ModResult registerLines(mods::flow::GraphBuilder& graph, u16 group, const MessageLanguage* langs, int nLangs) {
    g_msgs.clear();
    g_msgs.reserve(L_COUNT);
    for (int i = 0; i < L_COUNT; ++i) {
        mods::flow::MessageBuilder b;
        b.box_kind(MESSAGE_BOX_TALK).text_scale(130).text(kLines[i][0]).text_scale(100).text(kLines[i][1]);
        std::vector<mods::flow::MessageVariant> variants;
        for (int l = 0; l < nLangs; ++l) variants.push_back(b.build(langs[l]));
        auto& msg = g_msgs.emplace_back(mods::flow::register_message(group, variants));
        if (!msg) return msg.result();
        auto node = graph.add_message(msg.id());
        node.next(mods::flow::kEnd);
        g_nodes[i] = node.id();
    }
    return MOD_OK;
}

void linesReady(bool ok) { g_ready = ok; }

ModResult registerActor() { return svc_actor->register_actor(mod_ctx, &kProfile, &s_procName, &s_handle); }

}  // namespace grudge
