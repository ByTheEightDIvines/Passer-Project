#include "global.h"
#include "player_camp.hpp"

#include "mod_lights.hpp"
#include "tent_actor.hpp"
#include "travellers.hpp"

#include "SSystem/SComponent/c_lib.h"
#include "SSystem/SComponent/c_math.h"
#include "SSystem/SComponent/c_phase.h"
#include "d/actor/d_a_alink.h"
#include "d/actor/d_a_npc.h"
#include "d/actor/d_a_player.h"
#include "d/d_bg_s_gnd_chk.h"
#include "d/d_cc_d.h"
#include "d/d_com_inf_game.h"
#include "d/d_kankyo.h"
#include "d/d_msg_flow.h"
#include "d/d_particle_name.h"
#include "f_op/f_op_actor.h"
#include "f_op/f_op_actor_mng.h"
#include "f_pc/f_pc_draw_priority.h"
#include "m_Do/m_Do_audio.h"
#include "m_Do/m_Do_ext.h"
#include "m_Do/m_Do_mtx.h"
#include "mods/service.hpp"
#include "mods/svc/actor.h"
#include "mods/svc/camera.h"
#include "mods/svc/flow.hpp"
#include "mods/svc/save.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

IMPORT_OPTIONAL_SERVICE(CameraService, svc_camera);

#define PSR_PCAMP_NAME "PsrPCmp"   // (actor names are at most 7 letters)

class psrPCamp_c;

namespace {
// --- the camp's layout (camp space: +z is the way it faces, toward where Link stood) ------------
const char* kCampArc = "PsrCamp";   // Coro's fire (ours)
const char* kPackArc = "PsrPack";   // the log bench (ours)
const char* kFireParts[2] = {"coro_stones.bmd", "coro_wood.bmd"};   // no pot: no soup at Link's camp
constexpr f32 kFlameY = -30.0f;            // Coro's flame comes up from under his firewood
constexpr f32 kTentBack = 650.0f;          // the tent behind the fire, its door to it
constexpr s16 kBenchAngle = 0x1C00;        // the bench in front of the fire, a little to the side
constexpr f32 kBenchRing = 230.0f;
constexpr f32 kDoorOut = 70.0f;            // where Link stands to check the tent: just out from its door
constexpr f32 kCheckReach = 230.0f;        // ...within this of that spot

// --- Midna's menu (the game's Midna flow in message group 0, as read from zel_00.bmg) --------------
constexpr u16 kMidnaGroup = 0;
constexpr u16 kTalkGroup = 8;              // the field's message group: where our actors' talk lives
constexpr u8 kMidnaSpeaker = 21;
constexpr u16 kMidnaPromptEntry = 3003;    // "What is it, Link?" (waits for the choice below it)
// The game's menu variants all end in "Talk to Midna": the edge from that choice to her talk node,
// and the node it went to (all four are "jump to the Midna flow").
struct MenuTalk { u16 edge, talkNode; };
constexpr MenuTalk kMenuTalk[] = {
    {0x0113, 0x018f},   // Transform into human / Warp / Talk to Midna
    {0x0119, 0x0192},   // Transform into wolf / Warp / Talk to Midna
    {0x0128, 0x0197},   // Transform into human / Talk to Midna
    {0x012f, 0x01a4},   // Transform into wolf / Talk to Midna
};
// The choice lists themselves (message IDs), relabelled while the camp can be set up.
struct MenuLabels { u16 messageId; const char* first; const char* second; };   // second null: two choices
constexpr MenuLabels kMenuLabels[] = {
    {2041, "Transform into human", "Warp"},
    {2040, "Transform into wolf", "Warp"},
    {2005, "Transform into human", nullptr},
    {2004, "Transform into wolf", nullptr},
};

constexpr mods::flow::MessageStyle kMidnaStyle =
    mods::flow::MessageStyle{}.speaker(kMidnaSpeaker).box_kind(MESSAGE_BOX_MIDNA);

// --- kept with the save file ---------------------------------------------------------------------
struct SaveRec {
    u8 version = 1;
    u8 hasGear = 0;      // Midna took the gear in Faron
    u8 told = 0;         // ...and told Link about it in the field
    u8 up = 0;           // a camp stands somewhere
    s8 stage = -1;       // travellers::networkStage() of it
    s8 room = -1;
    u8 lit = 0;          // its fire is burning
    u8 pad = 0;
    f32 x = 0, y = 0, z = 0;
    s16 yaw = 0;
    s16 pad2 = 0;
};
SaveRec g_save{};
const char* kBlob = "player_camp";

void store() {
    if (svc_save != nullptr) svc_save->set_blob(mod_ctx, kBlob, &g_save, sizeof(g_save));
}
void onNewSave(ModContext*, uint32_t, void*) { g_save = SaveRec{}; }
void onSaveLoaded(ModContext*, uint32_t, void*) {
    g_save = SaveRec{};
    SaveRec r{};
    size_t size = sizeof(r);
    if (svc_save->get_blob(mod_ctx, kBlob, &r, &size) == MOD_OK && size == sizeof(r) && r.version == 1) g_save = r;
}

// --- what Midna's menu and the camp have to go on ------------------------------------------------
bool g_pendingSummon = false;
cXyz g_spotPos{};
s16 g_spotYaw = 0;
s8 g_spotRoom = -1;
bool g_spotOk = false;
bool g_sleepYes = false;
bool g_sleeping = false;

ActorId g_campId{};
bool g_campMade = false;
int g_spawnCooldown = 0;   // after a failed bring-out, a pause before the next try
psrPCamp_c* g_camp = nullptr;

bool campAllowed() {
    if (!g_save.hasGear || !g_save.told) return false;
    if (!travellers::inNetworkStage() || travellers::escortRunning()) return false;
    if (dKy_darkworld_check()) return false;   // never in twilight
    return true;
}

daPy_py_c* player() { return daPy_getPlayerActorClass(); }

f32 probeGround(f32 x, f32 y, f32 z, s8* room = nullptr) {
    dBgS_ObjGndChk gnd;
    cXyz probe(x, y + 600.0f, z);
    gnd.SetPos(&probe);
    const f32 g = dComIfG_Bgsp().GroundCross(&gnd);
    if (room != nullptr && g != -G_CM3D_F_INF) *room = static_cast<s8>(dComIfG_Bgsp().GetRoomId(gnd));
    return g;
}

cXyz campPoint(const cXyz& center, s16 yaw, f32 x, f32 z) {
    return cXyz(center.x + x * cM_scos(yaw) + z * cM_ssin(yaw), center.y, center.z - x * cM_ssin(yaw) + z * cM_scos(yaw));
}
cXyz tentPoint(const cXyz& center, s16 yaw) { return campPoint(center, yaw, 0.0f, -kTentBack); }
cXyz benchPoint(const cXyz& center, s16 yaw) {
    const s16 a = static_cast<s16>(yaw + kBenchAngle);
    return cXyz(center.x + cM_ssin(a) * kBenchRing, center.y, center.z + cM_scos(a) * kBenchRing);
}
cXyz doorPoint(const cXyz& center, s16 yaw) {
    return campPoint(center, yaw, 0.0f, -kTentBack + psr_tent::kFootZ[PSR_TENT_A] + kDoorOut);
}

// The flattest open ground near Link for the whole camp: the fire facing him, the tent behind it.
// Clear of the travellers' camps, Beedle's tent and the roads. Close in is better, but flat beats close.
bool findSpot(const cXyz& link, cXyz* outPos, s16* outYaw, s8* outRoom) {
    const f32 hx = psr_tent::kFootX[PSR_TENT_A], hz = psr_tent::kFootZ[PSR_TENT_A];
    struct Cand { f32 score; cXyz at; s16 yaw; };
    std::vector<Cand> cands;
    for (f32 r = 400.0f; r <= 1600.0f; r += 200.0f) {
        for (int a = 0; a < 16; a++) {
            const s16 dir = static_cast<s16>(a * 0x1000);
            cXyz at(link.x + cM_ssin(dir) * r, link.y, link.z + cM_scos(dir) * r);
            const f32 g = probeGround(at.x, link.y, at.z);
            if (g == -G_CM3D_F_INF || std::abs(g - link.y) > 250.0f) continue;
            at.y = g;
            const s16 yaw = cLib_targetAngleY(&at, &link);   // the camp faces Link
            const cXyz tent = tentPoint(at, yaw);
            if (!travellers::clearOfCampsAndRoads(at, 2500.0f, 450.0f)) continue;
            if (!travellers::clearOfCampsAndRoads(tent, 2500.0f, 450.0f)) continue;
            const f32 bumpTent = travellers::groundBump(tent, yaw, hx, hz, g, false);
            const f32 bumpFire = travellers::groundBump(at, yaw, 260.0f, 260.0f, g, false);
            if (bumpTent < 0.0f || bumpFire < 0.0f) continue;
            cands.push_back({bumpTent + bumpFire + r * 0.02f, at, yaw});
        }
    }
    std::sort(cands.begin(), cands.end(), [](const Cand& x, const Cand& y) { return x.score < y.score; });
    f32 best = 1e9f;
    bool found = false;
    for (size_t i = 0; i < cands.size() && i < 8; i++) {
        const Cand& c = cands[i];
        const cXyz tent = tentPoint(c.at, c.yaw);
        const f32 bumpTent = travellers::groundBump(tent, c.yaw, hx, hz, c.at.y, true);
        const f32 bumpFire = travellers::groundBump(c.at, c.yaw, 260.0f, 260.0f, c.at.y, true);
        if (bumpTent < 0.0f || bumpFire < 0.0f || bumpTent > 70.0f || bumpFire > 45.0f) continue;
        const f32 score = bumpTent + bumpFire + c.at.abs(link) * 0.02f;
        if (score < best) {
            s8 room = -1;
            if (probeGround(c.at.x, c.at.y, c.at.z, &room) == -G_CM3D_F_INF || room < 0) continue;
            best = score;
            found = true;
            *outPos = c.at;
            *outYaw = c.yaw;
            *outRoom = room;
        }
    }
    travellers::diagLine("pcamp   spot search: %d candidates, %s", static_cast<int>(cands.size()), found ? "found" : "none flat enough");
    return found;
}

// --- the camera for the camp's scenes (Dusklight's camera operator) ------------------------------
struct CamShot {
    bool active = false;
    bool capture = false;   // take the game camera's view as the starting point on the next frame
    cXyz fromEye{}, fromCenter{};
    f32 fromFovy = 60.0f;
    cXyz eye{}, center{};
    f32 fovy = 60.0f;
};
CamShot g_cam;
CameraOperatorHandle g_camHandle = 0;

bool operateCamera(ModContext*, CameraOperatorState* state, void*) {
    if (g_cam.capture) {
        g_cam.fromEye.set(state->eye[0], state->eye[1], state->eye[2]);
        g_cam.fromCenter.set(state->center[0], state->center[1], state->center[2]);
        g_cam.fromFovy = state->fovy;
        g_cam.capture = false;
    }
    if (!g_cam.active) return false;
    state->eye[0] = g_cam.eye.x; state->eye[1] = g_cam.eye.y; state->eye[2] = g_cam.eye.z;
    state->center[0] = g_cam.center.x; state->center[1] = g_cam.center.y; state->center[2] = g_cam.center.z;
    state->fovy = g_cam.fovy;
    state->bank_degrees = 0.0f;
    return true;
}

f32 ease(f32 t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return 0.5f - 0.5f * std::cos(t * 3.14159265f);
}
cXyz lerp(const cXyz& a, const cXyz& b, f32 t) { return cXyz(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t); }

// --- the lines and the flows ---------------------------------------------------------------------
std::vector<mods::flow::RegisteredMessage> g_messages;
std::vector<mods::flow::MessageOverride> g_overrides;
std::vector<std::vector<uint8_t>> g_labelText;   // the relabelled choice lists (English), kept alive
mods::flow::Graph g_menuGraph, g_talkGraph;
mods::flow::Query g_qAllowed, g_qExists, g_qSpot;
mods::flow::Event g_eSummon, g_eSleep;
u16 g_tentNode = 0xFFFF, g_wakeMorningNode = 0xFFFF, g_wakeEveningNode = 0xFFFF;
bool g_flowsReady = false;

constexpr std::array kLanguages{
    MESSAGE_LANGUAGE_ENGLISH, MESSAGE_LANGUAGE_GERMAN, MESSAGE_LANGUAGE_FRENCH,
    MESSAGE_LANGUAGE_SPANISH, MESSAGE_LANGUAGE_ITALIAN, MESSAGE_LANGUAGE_JAPANESE,
};

int registerMessage(u16 group, const mods::flow::MessageBuilder& b) {
    std::vector<mods::flow::MessageVariant> v;
    for (const auto language : kLanguages) v.push_back(b.build(language));
    auto& msg = g_messages.emplace_back(mods::flow::register_message(group, v));
    return msg ? static_cast<int>(msg.id()) : -1;
}

u16 qAllowed(ModContext*, const FlowQueryContext*, void*) { return campAllowed() ? 0 : 1; }
u16 qExists(ModContext*, const FlowQueryContext*, void*) { return g_save.up ? 1 : 0; }
u16 qSpot(ModContext*, const FlowQueryContext* q, void*) {
    if (q->phase == FLOW_QUERY_PHASE_PROBE) return g_spotOk ? 0 : 1;
    daPy_py_c* link = player();
    g_spotOk = link != nullptr && findSpot(link->current.pos, &g_spotPos, &g_spotYaw, &g_spotRoom);
    return g_spotOk ? 0 : 1;
}
void eSummon(ModContext*, const FlowEventContext*, void*) {
    if (g_spotOk) g_pendingSummon = true;
}
void eSleep(ModContext*, const FlowEventContext*, void*) { g_sleepYes = true; }

bool labelOverride(ModContext*, const MessageOverrideContext* ctx, MessageTextData* out, void* user) {
    if (!campAllowed() || out == nullptr) return false;   // the game's own menu everywhere else
    const auto i = reinterpret_cast<uintptr_t>(user);
    if (i >= g_labelText.size()) return false;
    out->text = g_labelText[i].data();
    out->text_size = g_labelText[i].size();
    (void)ctx;
    return true;
}

ModResult registerFlows() {
    auto report = [](const char* what, ModResult r) {
        travellers::diagLine("pcamp   %s could not be registered (result %d)", what, static_cast<int>(r));
        return r;
    };
    g_qAllowed = mods::flow::register_query("psr_camp_allowed", qAllowed);
    g_qExists = mods::flow::register_query("psr_camp_exists", qExists);
    g_qSpot = mods::flow::register_query("psr_camp_spot", qSpot);
    g_eSummon = mods::flow::register_event("psr_camp_summon", eSummon);
    g_eSleep = mods::flow::register_event("psr_camp_sleep", eSleep);
    if (!g_qAllowed || !g_qExists || !g_qSpot || !g_eSummon || !g_eSleep) return report("a query or event", MOD_ERROR);

    // ---- Midna's menu (group 0) ----
    const int sayFine = registerMessage(kMidnaGroup, mods::flow::MessageBuilder{kMidnaStyle}
        .text("Fine, fine. One camp,\ncoming right up!"));
    const int sayLumpy = registerMessage(kMidnaGroup, mods::flow::MessageBuilder{kMidnaStyle}
        .text("Ugh, the ground's way too lumpy\naround here. Find somewhere\nflatter!"));
    const int askMove = registerMessage(kMidnaGroup, mods::flow::MessageBuilder{kMidnaStyle}
        .text("We already have a camp, remember?\nWant me to drag it over here?")
        .options("Yes", "No"));
    const int moreList = registerMessage(kMidnaGroup, mods::flow::MessageBuilder{}
        .speaker(kMidnaSpeaker).options("Set up camp", "Talk to Midna"));
    if (sayFine < 0 || sayLumpy < 0 || askMove < 0 || moreList < 0) return report("Midna's camp lines", MOD_ERROR);
    {
        mods::flow::GraphBuilder graph{kMidnaGroup};
        auto summon = graph.add_event(g_eSummon.id(), {0, 0, 0, 0});
        summon.next(mods::flow::kEnd);
        auto fine = graph.add_message(static_cast<u16>(sayFine));
        fine.next(summon.id());
        auto lumpy = graph.add_message(static_cast<u16>(sayLumpy));
        lumpy.next(mods::flow::kEnd);
        auto spot = graph.add_branch(g_qSpot.id(), 0);   // 0 found, 1 nowhere flat enough
        spot.results({fine.id(), lumpy.id()});
        auto moveYes = graph.add_branch(FLOW_QUERY_SELECT_2_CANCEL, 0);
        moveYes.results({spot.id(), mods::flow::kEnd, mods::flow::kEnd});
        auto move = graph.add_message(static_cast<u16>(askMove));
        move.next(moveYes.id());
        auto exists = graph.add_branch(g_qExists.id(), 0);   // 0 none yet, 1 one stands somewhere
        exists.results({spot.id(), move.id()});
        // "More...": the camp, or on to her talk as before (one copy per menu, for its own talk node)
        for (const MenuTalk& m : kMenuTalk) {
            auto pick = graph.add_branch(FLOW_QUERY_SELECT_2_CANCEL, 0);
            pick.results({exists.id(), m.talkNode, mods::flow::kEnd});
            auto list = graph.add_message(static_cast<u16>(moreList));
            list.next(pick.id());
            auto prompt = graph.add_message(kMidnaPromptEntry);   // "What is it, Link?" again, over the list
            prompt.next(list.id());
            auto allowed = graph.add_branch(g_qAllowed.id(), 0);   // 0: in the field with the gear
            allowed.results({prompt.id(), m.talkNode});
            graph.patch_edge(m.edge, allowed.id());
        }
        g_menuGraph = graph.commit();
        if (!g_menuGraph) return report("Midna's menu", g_menuGraph.result());
    }
    // the menu's last choice reads "More..." while the camp can be set up (English; the other
    // languages keep the game's label, which leads to the same place)
    for (size_t i = 0; i < std::size(kMenuLabels); ++i) {
        const MenuLabels& l = kMenuLabels[i];
        mods::flow::MessageBuilder b{};
        b.speaker(kMidnaSpeaker);
        if (l.second != nullptr) b.options(l.first, l.second, "More...");
        else b.options(l.first, "More...");
        g_labelText.push_back(b.build(MESSAGE_LANGUAGE_ENGLISH).text());
    }
    for (size_t i = 0; i < std::size(kMenuLabels); ++i) {
        auto& o = g_overrides.emplace_back(mods::flow::override_message_fn(kMidnaGroup, kMenuLabels[i].messageId,
                                           MESSAGE_LANGUAGE_ENGLISH, labelOverride, reinterpret_cast<void*>(i)));
        if (!o) return report("the menu's More... label", o.result());
    }

    // ---- the camp's own talk (group 8) ----
    const int askSleep = registerMessage(kTalkGroup, mods::flow::MessageBuilder{}
        .box_kind(MESSAGE_BOX_TALK).text("Go to sleep?").options("Yes", "No"));
    const int wakeMorning = registerMessage(kTalkGroup, mods::flow::MessageBuilder{kMidnaStyle}
        .text("Rise and shine!\nYou know you snore, right?"));
    const int wakeEvening = registerMessage(kTalkGroup, mods::flow::MessageBuilder{kMidnaStyle}
        .text("Finally! You slept the whole\nday away, sleepyhead."));
    if (askSleep < 0 || wakeMorning < 0 || wakeEvening < 0) return report("the camp's lines", MOD_ERROR);
    {
        mods::flow::GraphBuilder graph{kTalkGroup};
        auto sleep = graph.add_event(g_eSleep.id(), {0, 0, 0, 0});
        sleep.next(mods::flow::kEnd);
        auto yes = graph.add_branch(FLOW_QUERY_SELECT_2_CANCEL, 0);
        yes.results({sleep.id(), mods::flow::kEnd, mods::flow::kEnd});
        auto ask = graph.add_message(static_cast<u16>(askSleep));
        ask.next(yes.id());
        auto morning = graph.add_message(static_cast<u16>(wakeMorning));
        morning.next(mods::flow::kEnd);
        auto evening = graph.add_message(static_cast<u16>(wakeEvening));
        evening.next(mods::flow::kEnd);
        g_talkGraph = graph.commit();
        if (!g_talkGraph) return report("the camp's talk", g_talkGraph.result());
        g_tentNode = ask.id();
        g_wakeMorningNode = morning.id();
        g_wakeEveningNode = evening.id();
    }
    g_flowsReady = true;
    return MOD_OK;
}

J3DModel* makeModel(const char* arc, const char* file) {
    auto* data = static_cast<J3DModelData*>(dComIfG_getObjectRes(arc, file));
    return data != NULL ? mDoExt_J3DModel__create(data, 0x80000, 0x11000084) : NULL;
}

// Coro's fire's collision (Obj_ktOnFire): a lantern swing lights it.
const dCcD_SrcCyl kFireCyl = {
    {
        {0x0, {{0x200, 0x0, 0x13}, {0xD8FBFFFF, 0x11}, 0x0}},
        {dCcD_SE_SWORD, 0x0, 0x0, 0x0, 0x0},
        {dCcD_SE_STONE, 0x0, 0x0, 0x0, 0x0},
        {0x0},
    },
    {{{0.0f, 0.0f, 0.0f}, 0.0f, 0.0f}},
};
}  // namespace

// --- the camp actor ------------------------------------------------------------------------------
// Parameters: bit 0 = summoned just now (play Midna's scene; the pieces appear in it).
class psrPCamp_c : public fopAc_ac_c {
public:
    enum State : u8 { ST_IDLE, ST_SUMMON, ST_TALK, ST_SLEEP, ST_WAKE };
    enum Talk : u8 { TALK_NONE, TALK_TENT };

    request_of_phase_process_class mPhCamp;
    request_of_phase_process_class mPhPack;
    bool mLoaded = false;
    J3DModel* mpFire[2] = {};
    J3DModel* mpBench = nullptr;
    f32 mBenchY = 0.0f;
    bool mGrounded = false;

    cXyz mCenter{};
    s16 mYaw = 0;
    bool mShown = true;          // pieces drawn (hidden until Midna's summon brings them)
    ActorId mTentId{};
    bool mTentMade = false;

    dCcD_Stts mStts;
    dCcD_Cyl mCyl;
    bool mLit = false;
    cXyz mFlamePos{};
    u32 mFlame[3] = {};
    LIGHT_INFLUENCE mLight{};
    bool mLightOn = false;

    State mState = ST_IDLE;
    Talk mTalk = TALK_NONE;
    int mTimer = 0;
    int mWait = 0;
    bool mFlowStarted = false;
    dMsgFlow_c mFlow;
    f32 mSleepFrom = 0.0f;
    cXyz mShotEye{}, mShotCenter{};

    virtual ~psrPCamp_c();
    cPhs_Step create();
    int CreateHeap();
    int Execute();
    int Draw();
    void placeModels();
    void makeTent();
    void light();
    void updateFire();
    void updateAttention();
    bool waitForEvent();
    void endEvent();
    void runSummon();
    void runTalk();
    void runSleep();
    void runWake();
    void shotWide(f32 back, f32 side, f32 up, f32 lookUp, cXyz* eye, cXyz* center) const;
    static int createHeapCallBack(fopAc_ac_c* a) { return static_cast<psrPCamp_c*>(a)->CreateHeap(); }

    static s16 sProcName;
    static ActorHandle sActorHandle;
    static const ActorProfileDesc sProfile;
};

psrPCamp_c::~psrPCamp_c() {
    if (g_camp == this) {   // gone (taken away, or its room unloaded): out again when Link comes back
        g_camp = nullptr;
        g_campMade = false;
    }
    if (mLightOn) dKy_plight_cut(&mLight);
    if (mTentMade) svc_actor->delete_actor(mod_ctx, mTentId);
    if (mState != ST_IDLE) {
        // taken away mid-scene (the room went): leave nothing hanging
        g_cam.active = false;
        if (g_sleeping) {
            g_sleeping = false;
            if (daPy_py_c* link = player()) {
                link->offPlayerNoDraw();
                link->offPlayerShadowNoDraw();
            }
        }
    }
    if (mLoaded) {
        dComIfG_resDelete(&mPhCamp, kCampArc);
        dComIfG_resDelete(&mPhPack, kPackArc);
    }
}

cPhs_Step psrPCamp_c::create() {
    fopAcM_ct(this, psrPCamp_c);
    g_camp = this;
    const cPhs_Step a = dComIfG_resLoad(&mPhCamp, kCampArc);
    const cPhs_Step b = dComIfG_resLoad(&mPhPack, kPackArc);
    if (a == cPhs_ERROR_e || b == cPhs_ERROR_e) return cPhs_ERROR_e;
    if (a != cPhs_COMPLEATE_e) return a;
    if (b != cPhs_COMPLEATE_e) return b;
    mLoaded = true;
    if (!fopAcM_entrySolidHeap(this, createHeapCallBack, 0x20000)) return cPhs_ERROR_e;
    mCenter = current.pos;
    mYaw = shape_angle.y;
    mLit = g_save.lit != 0;
    mStts.Init(255, 255, this);
    mCyl.Set(kFireCyl);
    mCyl.SetStts(&mStts);
    mFlamePos = mCenter;
    mFlamePos.y += kFlameY;
    mLight.mPosition = mFlamePos;
    mLight.mPosition.y += 10.0f;
    mLight.mColor.r = 188;
    mLight.mColor.g = 102;
    mLight.mColor.b = 66;
    mLight.mPow = 500.0f;
    mLight.mFluctuation = 1.0f;
    attention_info.flags = 0;
    if (fopAcM_GetParam(this) & 1) {
        mState = ST_SUMMON;
        mShown = false;
    } else {
        makeTent();
    }
    placeModels();
    fopAcM_SetMtx(this, mpFire[0]->getBaseTRMtx());
    fopAcM_setCullSizeBox(this, -1400.0f, -100.0f, -1400.0f, 1400.0f, 600.0f, 1400.0f);
    travellers::diagLine("pcamp   camp out at %.0f %.0f %.0f facing %d%s", mCenter.x, mCenter.y, mCenter.z,
                         static_cast<int>(mYaw), mState == ST_SUMMON ? " (summoned)" : "");
    return cPhs_COMPLEATE_e;
}

int psrPCamp_c::CreateHeap() {
    for (int i = 0; i < 2; i++) {
        mpFire[i] = makeModel(kCampArc, kFireParts[i]);
        if (mpFire[i] == NULL) return 0;
    }
    mpBench = makeModel(kPackArc, "psr_bench2.bmd");
    return 1;
}

void psrPCamp_c::makeTent() {
    if (mTentMade) return;
    const cXyz at = tentPoint(mCenter, mYaw);
    ActorSpawnParams p{};
    p.parameters = PSR_TENT_A | PSR_TENT_PLAYER;
    p.argument = -1;
    p.room_num = static_cast<s8>(fopAcM_GetRoomNo(this));
    p.position.x = at.x; p.position.y = mCenter.y; p.position.z = at.z;
    p.angle.y = mYaw;   // its door (+z) toward the fire
    p.scale.x = p.scale.y = p.scale.z = 1.0f;
    mTentMade = svc_actor->create_actor_from_name(mod_ctx, PSR_TENT_NAME, &p, &mTentId) == MOD_OK;
}

void psrPCamp_c::placeModels() {
    if (!mGrounded) {
        const f32 g = probeGround(mCenter.x, mCenter.y, mCenter.z);
        if (g != -G_CM3D_F_INF) {
            mCenter.y = g;
            const cXyz bench = benchPoint(mCenter, mYaw);
            const f32 gb = probeGround(bench.x, mCenter.y, bench.z);
            mBenchY = gb != -G_CM3D_F_INF ? gb : mCenter.y;
            mGrounded = true;
            mFlamePos = mCenter;
            mFlamePos.y += kFlameY;
            mLight.mPosition = mFlamePos;
            mLight.mPosition.y += 10.0f;
        }
    }
    mDoMtx_stack_c::transS(mCenter.x, mCenter.y, mCenter.z);
    mDoMtx_stack_c::YrotM(mYaw);
    for (J3DModel* m : mpFire) m->setBaseTRMtx(mDoMtx_stack_c::get());
    if (mpBench != NULL) {
        const cXyz at = benchPoint(mCenter, mYaw);
        mDoMtx_stack_c::transS(at.x, mGrounded ? mBenchY : mCenter.y, at.z);
        mDoMtx_stack_c::YrotM(static_cast<s16>(mYaw + kBenchAngle + 0x8000));   // its sitter faces the fire
        mpBench->setBaseTRMtx(mDoMtx_stack_c::get());
    }
}

void psrPCamp_c::light() {
    if (mLit) return;
    mLit = true;
    g_save.lit = 1;
    store();
    mDoAud_seStart(Z2SE_OBJ_FIRE_IGNITION, &mFlamePos, 0, dComIfGp_getReverb(fopAcM_GetRoomNo(this)));
    travellers::diagLine("pcamp   fire lit");
}

// Coro's flame, glow and crackle, and his fire's collision: a lantern swing lights it.
void psrPCamp_c::updateFire() {
    if (mShown && mCyl.ChkTgHit()) {
        dCcD_GObjInf* hit = mCyl.GetTgHitGObj();
        if (hit != NULL && hit->GetAtType() == AT_TYPE_LANTERN_SWING && hit->GetAtMtrl() == dCcD_MTRL_FIRE) light();
    }
    mCyl.SetAtMtrl(mLit ? dCcD_MTRL_FIRE : dCcD_MTRL_NONE);
    mCyl.SetR(50.0f);
    mCyl.SetH(100.0f);
    mCyl.SetC(mCenter);
    dComIfG_Ccsp()->Set(&mCyl);
    if (mLit && !mLightOn) {
        dKy_plight_set(&mLight);
        mLightOn = true;
    }
    if (mLit) {
        mod_lights::offer(mLight.mPosition, 160.0f);
        mFlame[0] = dComIfGp_particle_set(mFlame[0], 0x3AD, &mFlamePos, NULL, NULL);
        mFlame[1] = dComIfGp_particle_set(mFlame[1], 0x3AF, &mFlamePos, NULL, NULL);
        mFlame[2] = dComIfGp_particle_set(mFlame[2], 0x3AE, &mFlamePos, NULL, NULL);
        mDoAud_seStartLevel(Z2SE_OBJ_FIRE_BURNING, &mFlamePos, 0, dComIfGp_getReverb(fopAcM_GetRoomNo(this)));
    }
}

// What Link can check here: the tent (from just out of its door). (Only human Link lights the fire: his lantern.)
void psrPCamp_c::updateAttention() {
    attention_info.flags = 0;
    mTalk = TALK_NONE;
    daPy_py_c* link = player();
    if (link == nullptr || !g_flowsReady || !mShown || dKy_darkworld_check()) return;
    const cXyz door = doorPoint(mCenter, mYaw);
    const cXyz& at = link->current.pos;
    const f32 dDoor = std::hypot(at.x - door.x, at.z - door.z);
    if (dDoor < kCheckReach) {
        mTalk = TALK_TENT;
        current.pos = door;
    } else {
        current.pos = mCenter;
        return;
    }
    current.pos.y = mCenter.y;
    attention_info.position = current.pos;
    attention_info.position.y += 120.0f;
    eyePos = attention_info.position;
    attention_info.distances[fopAc_attn_SPEAK_e] = daNpcT_getDistTableIdx(3, 6);
    attention_info.flags = fopAc_AttnFlag_SPEAK_e;
    eventInfo.onCondition(dEvtCnd_CANTALK_e);
    if (daNpcT_chkDoBtnIsSpeak(this)) dComIfGp_setDoStatus(8, 0);   // "Check"
}

// A scene of ours: a talk event with no talk in it, so the game holds Link still (as the cook's).
bool psrPCamp_c::waitForEvent() {
    if (eventInfo.checkCommandTalk()) return true;
    eventInfo.onCondition(dEvtCnd_CANTALK_e);
    fopAcM_orderSpeakEvent(this, 0, 0);
    return false;
}

void psrPCamp_c::endEvent() {
    dComIfGp_event_reset();
    g_cam.active = false;
    mState = ST_IDLE;
    mTimer = 0;
    mFlowStarted = false;
}

// eye and center of a wide shot of the camp: from in front (where Link stood), out to one side
void psrPCamp_c::shotWide(f32 back, f32 side, f32 up, f32 lookUp, cXyz* eye, cXyz* center) const {
    *eye = campPoint(mCenter, mYaw, side, back);
    eye->y = mCenter.y + up;
    *center = campPoint(mCenter, mYaw, 0.0f, -kTentBack * 0.5f);
    center->y = mCenter.y + lookUp;
}

// Midna's summon: the camera pulls back, her twilight gathers at the spot, and the camp is there.
void psrPCamp_c::runSummon() {
    if (mTimer == 0 && !waitForEvent()) {
        if (++mWait > 300) {   // the game never gave us the scene: the camp is simply there
            mShown = true;
            makeTent();
            mState = ST_IDLE;
            travellers::diagLine("pcamp   summon scene never started; camp shown without it");
        }
        return;
    }
    if (mTimer == 0) {
        g_cam.capture = true;
        shotWide(1500.0f, 650.0f, 520.0f, 160.0f, &mShotEye, &mShotCenter);
    }
    ++mTimer;
    if (mTimer == 2) g_cam.active = true;
    const f32 in = ease((mTimer - 2) / 30.0f);
    const f32 out = ease((mTimer - 100) / 24.0f);
    const f32 t = in * (1.0f - out);
    g_cam.eye = lerp(g_cam.fromEye, mShotEye, t);
    g_cam.center = lerp(g_cam.fromCenter, mShotCenter, t);
    g_cam.fovy = g_cam.fromFovy + (60.0f - g_cam.fromFovy) * t;
    if (mTimer == 22) {
        // her twilight, where each piece will stand
        const cXyz spots[3] = {mCenter, tentPoint(mCenter, mYaw), benchPoint(mCenter, mYaw)};
        const cXyz big(1.0f, 1.0f, 1.0f);
        for (const cXyz& s : spots) dComIfGp_particle_set(ID_ZI_J_LK_WARP_APP_A, &s, &tevStr, &shape_angle, &big);
        mDoAud_seStart(Z2SE_AL_WARP_OUT, &mCenter, 0, dComIfGp_getReverb(fopAcM_GetRoomNo(this)));
    }
    if (mTimer == 50) {
        mShown = true;
        makeTent();
    }
    if (mTimer >= 126) endEvent();
}

// Link checked the tent: "Go to sleep?", and on into the night if he says yes.
void psrPCamp_c::runTalk() {
    if (!mFlowStarted) {
        g_sleepYes = false;
        mFlow.init(this, g_tentNode, 0, NULL);
        mFlowStarted = true;
    }
    if (mFlow.doFlow(this, NULL, 0) == 0) return;
    if (mTalk == TALK_TENT && g_sleepYes) {
        // into the tent: the event stays on through the night
        g_sleepYes = false;
        mState = ST_SLEEP;
        mTimer = 0;
        mFlowStarted = false;
        return;
    }
    endEvent();
}

// Asleep: Link is in the tent (it shuts and glows), the camera pulls back to take in the camp and the
// sky, and the clock runs on twelve hours, the sky rolling over with it. Then back to Link.
void psrPCamp_c::runSleep() {
    constexpr int kIn = 60, kRoll = 330, kHold = 30, kOut = 30;
    daPy_py_c* link = player();
    if (mTimer == 0) {
        g_cam.capture = true;
        shotWide(1900.0f, 750.0f, 260.0f, 520.0f, &mShotEye, &mShotCenter);
        mSleepFrom = dComIfGs_getTime();
        g_sleeping = true;
        if (link != nullptr) {
            link->onPlayerNoDraw();
            link->onPlayerShadowNoDraw();
        }
        travellers::diagLine("pcamp   Link goes to sleep at %.1f (hour %d)", mSleepFrom, dKy_getdaytime_hour());
    }
    ++mTimer;
    if (mTimer == 2) g_cam.active = true;
    const f32 in = ease((mTimer - 2) / static_cast<f32>(kIn));
    const f32 out = ease((mTimer - (kIn + kRoll + kHold)) / static_cast<f32>(kOut));
    const f32 t = in * (1.0f - out);
    // a slow drift sideways while the sky turns, so the shot breathes
    const f32 roll = std::clamp((mTimer - kIn) / static_cast<f32>(kRoll), 0.0f, 1.0f);
    cXyz eye = mShotEye;
    const cXyz drift = campPoint(cXyz(0.0f, 0.0f, 0.0f), mYaw, -400.0f * roll, 0.0f);
    eye.x += drift.x;
    eye.z += drift.z;
    g_cam.eye = lerp(g_cam.fromEye, eye, t);
    g_cam.center = lerp(g_cam.fromCenter, mShotCenter, t);
    g_cam.fovy = g_cam.fromFovy + (62.0f - g_cam.fromFovy) * t;
    if (mTimer > kIn && mTimer <= kIn + kRoll) {
        // the game's own "instant time change": it turns the calendar over when it passes midnight
        f32 now = mSleepFrom + 180.0f * ease(roll);
        while (now >= 360.0f) now -= 360.0f;
        dKy_instant_timechg(now);
    }
    if (mTimer == kIn + kRoll + kHold) {
        g_sleeping = false;   // the tent opens, Link comes out
        if (link != nullptr) {
            link->offPlayerNoDraw();
            link->offPlayerShadowNoDraw();
        }
    }
    if (mTimer >= kIn + kRoll + kHold + kOut) {
        g_cam.active = false;
        mState = ST_WAKE;
        mTimer = 0;
        mFlowStarted = false;
        travellers::diagLine("pcamp   Link wakes at %.1f (hour %d, day %d)", dComIfGs_getTime(), dKy_getdaytime_hour(),
                             static_cast<int>(dComIfGs_getDate()));
    }
}

void psrPCamp_c::runWake() {
    if (!mFlowStarted) {
        const int h = dKy_getdaytime_hour();
        mFlow.init(this, (h >= 5 && h < 17) ? g_wakeMorningNode : g_wakeEveningNode, 0, NULL);
        mFlowStarted = true;
    }
    if (mFlow.doFlow(this, NULL, 0) != 0) endEvent();
}

int psrPCamp_c::Execute() {
    placeModels();
    updateFire();
    switch (mState) {
    case ST_IDLE:
        updateAttention();
        if (mTalk != TALK_NONE && eventInfo.checkCommandTalk()) {
            attention_info.flags = 0;
            mState = ST_TALK;
            mFlowStarted = false;
        }
        break;
    case ST_SUMMON: runSummon(); break;
    case ST_TALK: runTalk(); break;
    case ST_SLEEP: runSleep(); break;
    case ST_WAKE: runWake(); break;
    }
    return 1;
}

int psrPCamp_c::Draw() {
    if (!mShown) return 1;
    g_env_light.settingTevStruct(0x20, &mCenter, &tevStr);
    auto draw = [&](J3DModel* m) {
        if (m == NULL) return;
        g_env_light.setLightTevColorType_MAJI(m, &tevStr);
        mDoExt_modelUpdateDL(m);
    };
    for (J3DModel* m : mpFire) draw(m);
    draw(mpBench);
    return 1;
}

namespace {
cPhs_Step pcamp_Create(void* p) { return static_cast<psrPCamp_c*>(p)->create(); }
int pcamp_Delete(void* p) {
    static_cast<psrPCamp_c*>(p)->~psrPCamp_c();
    return 1;
}
int pcamp_Execute(void* p) { return static_cast<psrPCamp_c*>(p)->Execute(); }
int pcamp_Draw(void* p) { return static_cast<psrPCamp_c*>(p)->Draw(); }
int pcamp_IsDelete(void*) { return 1; }
}  // namespace

s16 psrPCamp_c::sProcName = -1;
ActorHandle psrPCamp_c::sActorHandle = -1;
const ActorProfileDesc psrPCamp_c::sProfile = {
    .name = PSR_PCAMP_NAME,
    .priority_group = 7,
    .process_size = sizeof(psrPCamp_c),
    .draw_priority = fpcDwPi_OBJ_LBOX_e,
    .status = fopAcStts_UNK_0x40000_e | fopAcStts_UNK_0x4000_e | fopAcStts_CULL_e,
    .group = fopAc_ACTOR_e,
    .cull_type = fopAc_CULLBOX_CUSTOM_e,
    .create_function = pcamp_Create,
    .delete_function = pcamp_Delete,
    .execute_function = pcamp_Execute,
    .is_delete_function = pcamp_IsDelete,
    .draw_function = pcamp_Draw,
};

// --- bringing it out and taking it away ------------------------------------------------------------
namespace {
void removeCamp() {
    if (g_campMade) svc_actor->delete_actor(mod_ctx, g_campId);
    g_campMade = false;
    g_camp = nullptr;   // (its destructor, when the game gets to it, finds it isn't the camp any more)
}

void spawnCamp(bool summoned) {
    ActorSpawnParams p{};
    p.parameters = summoned ? 1u : 0u;
    p.argument = -1;
    p.room_num = g_save.room;
    p.position.x = g_save.x; p.position.y = g_save.y; p.position.z = g_save.z;
    p.angle.y = g_save.yaw;
    p.scale.x = p.scale.y = p.scale.z = 1.0f;
    g_campMade = svc_actor->create_actor_from_name(mod_ctx, PSR_PCAMP_NAME, &p, &g_campId) == MOD_OK;
}
}  // namespace

namespace player_camp {
ModResult install() {
    if (svc_save != nullptr) svc_save->observe_saves(mod_ctx, onNewSave, onSaveLoaded, nullptr, nullptr, nullptr);
    if (ModResult r = svc_actor->register_actor(mod_ctx, &psrPCamp_c::sProfile, &psrPCamp_c::sProcName,
                                               &psrPCamp_c::sActorHandle);
        r != MOD_OK) {
        return r;
    }
    if (svc_camera != nullptr) {
        CameraOperatorDesc desc = CAMERA_OPERATOR_DESC_INIT;
        desc.debug_name = "player camp scenes";
        desc.priority = 10;
        desc.operate = operateCamera;
        svc_camera->register_camera_operator(mod_ctx, &desc, &g_camHandle);
    }
    return registerFlows();
}

bool sleeping() { return g_sleeping; }

void update() {
    daPy_py_c* link = player();
    const int stage = travellers::networkStage();
    // Midna was asked: the camp goes where she found ground (moving the old one if there was one)
    if (g_pendingSummon && link != nullptr && dComIfGp_getEvent()->isOrderOK()) {
        g_pendingSummon = false;
        removeCamp();
        g_save.up = 1;
        g_save.stage = static_cast<s8>(stage);
        g_save.room = g_spotRoom;
        g_save.x = g_spotPos.x; g_save.y = g_spotPos.y; g_save.z = g_spotPos.z;
        g_save.yaw = g_spotYaw;
        g_save.lit = 0;   // a fresh fire, waiting to be lit
        store();
        spawnCamp(true);
        return;
    }
    if (g_camp != nullptr && g_camp->mState != psrPCamp_c::ST_IDLE) return;   // never mid-scene
    if (g_spawnCooldown > 0) --g_spawnCooldown;
    const bool want = g_save.up && link != nullptr && stage >= 0 && stage == g_save.stage &&
                      !travellers::escortRunning() && g_save.room >= 0 &&
                      dComIfGp_roomControl_checkRoomDisp(g_save.room) &&
                      std::hypot(link->current.pos.x - g_save.x, link->current.pos.z - g_save.z) < 11000.0f;
    if (want && !g_campMade && g_spawnCooldown <= 0) {
        spawnCamp(false);
        g_spawnCooldown = 90;
    } else if (!want && g_campMade) {
        removeCamp();
    }
}

void testGiveGear() {
    g_save.hasGear = 1;
    g_save.told = 1;
    store();
}

void testReset() {
    removeCamp();
    g_save = SaveRec{};
    store();
}
}  // namespace player_camp
