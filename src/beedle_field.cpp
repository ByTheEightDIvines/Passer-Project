// Beedle in the field: his shop (one ware a day, through his talk), his voice, his talking.
//
// The ware: one a day (the save's day count picks it), from the wares Link can use so far (a bow for
// arrows, a bomb bag for bombs, Lanayru cleared for water bombs, the Master Sword for bomblings, a
// bottle for potions, milk and oil, the lantern for oil, Kakariko's shop open for the Hylian Shield and
// the Hawkeye). Prices: the game's usual shop price, 5 Rupees cheaper (it's Beedle).
// The talk (a flow of our own per ware): the offer with a choice -> our check (enough Rupees? room?)
// -> our purchase (Rupees off, the ware given as the game gives it) -> "THANK YOU!" (his voice);
// "GOODBYE!" (his voice) as the talk ends. While he talks: his Wind Waker talking (upper body).
#include "global.h"
#include "beedle_field.hpp"
#include "d/actor/d_a_npc_passer.h"
#include "d/d_com_inf_game.h"
#include "d/d_item.h"
#include "d/d_item_data.h"
#include "d/d_meter2_info.h"
#include "f_op/f_op_actor_mng.h"
#include "SSystem/SComponent/c_math.h"
#include "SSystem/SComponent/c_lib.h"
#include "m_Do/m_Do_audio.h"
#include "mods/service.hpp"
#include "mods/svc/flow.hpp"
#include "mods/svc/audio_res.h"
#include "mod_sounds.hpp"
#include "travellers.hpp"
#include <array>
#include <cstdio>
#include <vector>

namespace {
enum Kind : u8 { K_ARROWS, K_BOMBS, K_WATER, K_POKE, K_BOTTLE, K_SHIELD, K_HAWK };
struct Ware {
    const char* name;   // as he says it
    u16 price;          // the usual price - 5
    Kind kind;
    u8 item;            // the item given (bottles: the bottle's contents)
};
constexpr std::array<Ware, 11> kWares{{
    {"10 Arrows", 5, K_ARROWS, dItemNo_ARROW_10_e},              // 10 (Death Mountain's hot spring shop)
    {"10 Bombs", 25, K_BOMBS, dItemNo_BOMB_10_e},                // 30 (Barnes)
    {"5 Water Bombs", 25, K_WATER, dItemNo_WATER_BOMB_5_e},      // 30 (Barnes)
    {"5 Bomblings", 25, K_POKE, dItemNo_BOMB_INSECT_5_e},        // 30 (Barnes)
    {"a Red Potion", 25, K_BOTTLE, dItemNo_RED_BOTTLE_e},        // 30
    {"a Blue Potion", 95, K_BOTTLE, dItemNo_BLUE_BOTTLE_e},      // 100
    {"Lantern Oil", 15, K_BOTTLE, dItemNo_OIL_BOTTLE_e},         // 20
    {"Milk", 15, K_BOTTLE, dItemNo_MILK_BOTTLE_e},               // 20
    {"a Wooden Shield", 45, K_SHIELD, dItemNo_WOOD_SHIELD_e},    // 50
    {"a Hylian Shield", 195, K_SHIELD, dItemNo_HYLIA_SHIELD_e},  // 200 (Malo Mart)
    {"the Hawkeye", 95, K_HAWK, dItemNo_HAWK_EYE_e},             // 100 (Malo Mart)
}};
constexpr int kWareCount = static_cast<int>(kWares.size());

// his voice: programs 0xEC/0xED (the travellers' voices use 0x88-0xDF, the camp songs 0xE0-0xEB)
constexpr u8 kPrgGoodbye = 0xEC, kPrgThanks = 0xED;
u32 g_goodbye = 0, g_thanks = 0;

std::array<u16, kWareCount> g_offerNode{};
u16 g_nothingNode = 0xFFFF;
bool g_ready = false;
FlowQueryId g_qCanBuy{};
FlowEventId g_eBuy{};
int g_talkWare = -1;    // the ware on offer in the talk now
int g_lastBought = -1;  // diary

bool has(u8 item) { return dComIfGs_isItemFirstBit(item) != 0; }
constexpr int kBagSlot0 = SLOT_15;   // SLOT_15..17: the three bomb bags

// a bag that holds this kind of bomb (or none yet) with room for `count` more
bool bagRoom(u8 bombType, int count) {
    for (int b = 0; b < 3; ++b) {
        const u8 in = dComIfGs_getItem(kBagSlot0 + b, false);
        if (in == dItemNo_BOMB_BAG_LV1_e) return true;   // an empty bag takes any kind
        if (in == bombType && dComIfGs_getBombNum(static_cast<u8>(b)) + count <= dComIfGs_getBombMax(bombType)) return true;
    }
    return false;
}
u8 bombTypeOf(Kind k) {
    return k == K_WATER ? dItemNo_WATER_BOMB_e : k == K_POKE ? dItemNo_POKE_BOMB_e : dItemNo_NORMAL_BOMB_e;
}
int countOf(const Ware& w) {
    switch (w.item) {
    case dItemNo_BOMB_10_e: case dItemNo_ARROW_10_e: return 10;
    default: return 5;
    }
}

// what Link has come far enough to want
bool unlocked(const Ware& w) {
    switch (w.kind) {
    case K_ARROWS: return has(dItemNo_BOW_e);
    case K_BOMBS: return has(dItemNo_BOMB_BAG_LV1_e);
    case K_WATER: return has(dItemNo_BOMB_BAG_LV1_e) && travellers::lanayruCleared();
    case K_POKE: return has(dItemNo_BOMB_BAG_LV1_e) && has(dItemNo_MASTER_SWORD_e);
    case K_BOTTLE: return has(dItemNo_EMPTY_BOTTLE_e) && (w.item != dItemNo_OIL_BOTTLE_e || has(dItemNo_KANTERA_e));
    case K_SHIELD: return w.item == dItemNo_WOOD_SHIELD_e || travellers::eldinCleared();
    case K_HAWK: return has(dItemNo_BOW_e) && travellers::eldinCleared();
    }
    return false;
}

// room for it (or he'd only be taking Link's Rupees)
bool room(const Ware& w) {
    switch (w.kind) {
    case K_ARROWS: return dComIfGs_getArrowNum() < dComIfGs_getArrowMax();
    case K_BOMBS: case K_WATER: case K_POKE: return bagRoom(bombTypeOf(w.kind), countOf(w));
    case K_BOTTLE: return dComIfGs_checkEmptyBottle() > 0;
    case K_SHIELD:
        return !dComIfGs_isCollectShield(w.item == dItemNo_HYLIA_SHIELD_e ? COLLECT_HYLIAN_SHIELD : COLLECT_WOODEN_SHIELD);
    case K_HAWK: return !has(dItemNo_HAWK_EYE_e);
    }
    return false;
}

void give(const Ware& w) {
    switch (w.kind) {
    case K_BOTTLE:
        dComIfGs_setEmptyBottleItemIn(w.item);
        break;
    case K_SHIELD:   // (the game's own item handlers for shields do nothing: shops equip them like this)
        dComIfGs_onItemFirstBit(w.item);
        dMeter2Info_setShield(w.item, false);
        break;
    default:
        execItemGet(w.item);
        break;
    }
}

// today's ware: the day picks one of those he can sell Link (-1: nothing yet)
int todaysWare() {
    std::array<int, kWareCount> can{};
    int n = 0;
    for (int i = 0; i < kWareCount; ++i) {
        if (unlocked(kWares[i])) can[n++] = i;
    }
    if (n == 0) return -1;
    u32 h = static_cast<u32>(dComIfGs_getDate()) * 2654435761u;
    h ^= h >> 15;
    return can[h % static_cast<u32>(n)];
}

u16 onCanBuy(ModContext*, const FlowQueryContext* q, void*) {
    const int i = q->parameter;
    if (i < 0 || i >= kWareCount) return 2;
    if (q->phase == FLOW_QUERY_PHASE_PROBE) return 0;
    const Ware& w = kWares[i];
    if (dComIfGs_getRupee() < w.price) return 1;
    if (!room(w)) return 2;
    return 0;
}

void playVoice(u32 sound, const fopAc_ac_c* at) {
    if (sound == 0 || at == nullptr) return;
    mDoAud_seStart(sound, &at->eyePos, 0, dComIfGp_getReverb(fopAcM_GetRoomNo(at)));
}

void onBuy(ModContext*, const FlowEventContext* e, void*) {
    const int i = (e->parameters[2] << 8) | e->parameters[3];
    if (i < 0 || i >= kWareCount) return;
    const Ware& w = kWares[i];
    if (dComIfGs_getRupee() < w.price || !room(w)) return;   // (checked just before; never charge for nothing)
    dComIfGp_setItemRupeeCount(-static_cast<int>(w.price));
    give(w);
    g_lastBought = i;
    travellers::diagLine("beedle  sold %s for %d", w.name, w.price);
    auto* at = static_cast<const fopAc_ac_c*>(e->speaker_actor);
    playVoice(g_thanks, at);
    beedle_field::praise();
}
}  // namespace

namespace beedle_field {

bool installSounds() {
    if (svc_audio_res == nullptr) return false;
    AudioSoundTableEffectInfo info = *svc_audio_res->default_effect_info;
    info.volume = 1.0f;
    info.always_max_priority = true;
    if (mod_sounds::loadWave(kPrgGoodbye, "res/voices/beedle_goodbye.wav", false, 0)) {
        g_goodbye = mod_sounds::addSound(SE_CATEGORY_CHARA_VOICE, kPrgGoodbye, &info);
    }
    if (mod_sounds::loadWave(kPrgThanks, "res/voices/beedle_thanks.wav", false, 0)) {
        g_thanks = mod_sounds::addSound(SE_CATEGORY_CHARA_VOICE, kPrgThanks, &info);
    }
    return g_goodbye != 0 && g_thanks != 0;
}

ModResult registerFlows(mods::flow::GraphBuilder& graph, const MessageLanguage* langs, int nLangs, u16 group) {
    g_ready = false;
    if (svc_flow == nullptr) return MOD_ERROR;
    if (ModResult r = svc_flow->register_query(mod_ctx, "psr_beedle_can_buy", onCanBuy, nullptr, &g_qCanBuy); r != MOD_OK) return r;
    if (ModResult r = svc_flow->register_event(mod_ctx, "psr_beedle_buy", onBuy, nullptr, &g_eBuy); r != MOD_OK) return r;
    auto node = [&](auto&& fill, u16 next) -> int {   // a message node going on to `next`
        mods::flow::MessageBuilder b;
        b.box_kind(MESSAGE_BOX_TALK);
        fill(b);
        std::vector<mods::flow::MessageVariant> v;
        for (int k = 0; k < nLangs; ++k) v.push_back(b.build(langs[k]));
        auto msg = mods::flow::register_message(group, v);
        if (!msg) return -1;
        auto n = graph.add_message(msg.id());
        n.next(next);
        return n.id();
    };
    const int bye = node([](auto& b) { b.text("Come back any time!"); }, mods::flow::kEnd);
    const int poor = node([](auto& b) { b.text("Huh? You don't have enough Rupees!\nCome back when you do!"); }, mods::flow::kEnd);
    const int full = node([](auto& b) { b.text("Looks like you can't carry any more\nof that! Next time, then!"); }, mods::flow::kEnd);
    const int thanks = node([](auto& b) { b.text("THANK YOU!"); }, mods::flow::kEnd);
    const int nothing = node([](auto& b) { b.text("Ah, I've got nothing you can use today.\nCome back tomorrow!"); }, mods::flow::kEnd);
    if (bye < 0 || poor < 0 || full < 0 || thanks < 0 || nothing < 0) return MOD_ERROR;
    g_nothingNode = static_cast<u16>(nothing);
    for (int i = 0; i < kWareCount; ++i) {
        const Ware& w = kWares[i];
        char price[24];
        std::snprintf(price, sizeof(price), "%u Rupee%s", static_cast<unsigned>(w.price), w.price == 1 ? "" : "s");
        // offer -> (buy | no) ; buy -> check -> (ok -> purchase -> thanks | poor | full)
        auto buyEv = graph.add_event(g_eBuy, {0, 0, static_cast<u8>(i >> 8), static_cast<u8>(i)});
        buyEv.next(static_cast<u16>(thanks));
        auto check = graph.add_branch(g_qCanBuy, static_cast<u16>(i));
        check.results({buyEv.id(), static_cast<u16>(poor), static_cast<u16>(full)});
        auto choose = graph.add_branch(FLOW_QUERY_SELECT_2, 0);
        choose.results({check.id(), static_cast<u16>(bye)});
        const int offer = node([&](auto& b) {
            b.text("Today's special: ");
            b.text_color(MESSAGE_COLOR_RED).text(w.name).text_color(MESSAGE_COLOR_DEFAULT);
            b.text("!\nJust ");
            b.text_color(MESSAGE_COLOR_RED).text(price).text_color(MESSAGE_COLOR_DEFAULT);
            b.text(". Want it?");
            b.options("I'll take it!", "No thanks");
        }, choose.id());
        if (offer < 0) return MOD_ERROR;
        g_offerNode[i] = static_cast<u16>(offer);
    }
    g_ready = true;
    return MOD_OK;
}

u16 talkNode() {
    if (!g_ready) return 0xFFFF;
    g_talkWare = todaysWare();
    if (g_talkWare >= 0) {
        travellers::diagLine("beedle  today (day %d): %s", dComIfGs_getDate(), kWares[g_talkWare].name);
    }
    return g_talkWare >= 0 ? g_offerNode[g_talkWare] : g_nothingNode;
}

// --- his talking ---------------------------------------------------------------------------------
namespace {
fpc_ProcID g_talker = fpcM_ERROR_PROCESS_ID_e;
int g_praise = 0;   // frames left of his "thank you" flourish
const char* kTalks[] = {"psr_bs_talk01_up.bck", "psr_bs_talk02_up.bck", "psr_bs_talk03_up.bck", "psr_bs_talk04_up.bck"};
void play(daNpcPasser_c* p, const char* file, bool loop) {
    auto* anm = static_cast<J3DAnmTransformKey*>(dComIfG_getObjectRes("P_Beedl", file));
    if (anm != nullptr && p->mpMorf != nullptr) {
        p->setAnm(anm, 1.0f, 6.0f, loop ? J3DFrameCtrl::EMode_LOOP : J3DFrameCtrl::EMode_NONE, 0, -1);
    }
}
}  // namespace

void talkStarted(daNpcPasser_c* p) {
    g_talker = fopAcM_GetID(p);
    g_praise = 0;
    p->speedF = 0.0f;
    play(p, kTalks[static_cast<int>(cM_rndF(4.0f)) & 3], true);
}

void praise() { g_praise = 90; }

// while Link talks to him: standing, turned to Link, talking (the praise when he's paid)
void talkTick(daNpcPasser_c* p) {
    p->speedF = 0.0f;
    if (auto* link = dComIfGp_getPlayer(0)) {
        cLib_addCalcAngleS2(&p->shape_angle.y, cLib_targetAngleY(&p->current.pos, &link->current.pos), 4, 0x800);
        p->current.angle.y = p->shape_angle.y;
    }
    if (g_praise > 0) {
        if (g_praise == 90) play(p, "psr_bs_praise_up.bck", false);
        if (--g_praise == 0) play(p, kTalks[0], true);
    }
}

void talkEnded(daNpcPasser_c* p) {
    if (p == nullptr || fopAcM_GetID(p) != g_talker) return;
    g_talker = fpcM_ERROR_PROCESS_ID_e;
    playVoice(g_goodbye, p);
}

}  // namespace beedle_field
