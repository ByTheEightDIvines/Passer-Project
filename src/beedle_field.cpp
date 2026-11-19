// Beedle in the field: his shop (one ware a day, through his talk), his voice, his talking.
//
// The ware: one a day (the save's day count picks it), from the wares Link can use so far (a bow for
// arrows, a bomb bag for bombs, Lanayru cleared for water bombs, the Master Sword for bomblings, a
// bottle for potions, milk, oil and bee larva, the lantern for oil, the slingshot for seeds). Prices: the
// game's usual shop price, 5 Rupees cheaper (it's Beedle).
// The talk (a flow of our own per ware): the offer with a choice -> our check (enough Rupees? room?)
// -> our purchase (Rupees off, the ware given as the game gives it) -> "THANK YOU!" (his voice);
// "GOODBYE!" (his voice) as the talk ends. While he talks: his Wind Waker talking (upper body).
#include "global.h"
#include "beedle_field.hpp"

#include <deque>
#include <algorithm>
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
enum Kind : u8 { K_ARROWS, K_BOMBS, K_WATER, K_POKE, K_BOTTLE, K_SHIELD, K_LARVA, K_SEEDS };
struct Ware {
    const char* name;   // as he says it
    const char* title;  // as his shop shows it
    const char* desc;   // its description in his shop
    u16 price;          // the usual price - 5
    Kind kind;
    u8 item;            // the item given (bottles: the bottle's contents)
};
constexpr std::array<Ware, 11> kWares{{
    {"10 Arrows", "Arrows (10)", "Arrows for your bow. Never run dry\nin the middle of a fight!", 5, K_ARROWS, dItemNo_ARROW_10_e},              // 10 (Death Mountain's hot spring shop)
    {"10 Bombs", "Bombs (10)", "Boom! Blasts rocks, walls and\nmonsters alike. Handle with care!", 25, K_BOMBS, dItemNo_BOMB_10_e},                // 30 (Barnes)
    {"5 Water Bombs", "Water Bombs (5)", "Bombs that sink and blow up\nunderwater, Zora style!", 25, K_WATER, dItemNo_WATER_BOMB_5_e},      // 30 (Barnes)
    {"5 Bomblings", "Bomblings (5)", "Little bombs that scurry along\nthe ground on their own legs!", 25, K_POKE, dItemNo_BOMB_INSECT_5_e},        // 30 (Barnes)
    {"a Red Potion", "Red Potion", "Restores 8 hearts of life energy.\nNeeds an empty bottle!", 25, K_BOTTLE, dItemNo_RED_BOTTLE_e},        // 30
    {"a Blue Potion", "Blue Potion", "Restores all your hearts! The\nreal deal. Needs an empty bottle!", 95, K_BOTTLE, dItemNo_BLUE_BOTTLE_e},      // 100
    {"Lantern Oil", "Lantern Oil", "Keeps your lantern burning.\nNeeds an empty bottle!", 15, K_BOTTLE, dItemNo_OIL_BOTTLE_e},         // 20
    {"Milk", "Milk", "Fresh from Ordon! Two drinks of\nthree hearts each. Needs a bottle!", 15, K_BOTTLE, dItemNo_MILK_BOTTLE_e},               // 20
    {"a Wooden Shield", "Wooden Shield", "A sturdy wooden shield. It burns,\nso mind the fire!", 45, K_SHIELD, dItemNo_WOOD_SHIELD_e},    // 50
    {"Bee Larva", "Bee Larva", "Bait for fishing, or a snack in a\npinch. Needs an empty bottle!", 5, K_LARVA, dItemNo_BEE_CHILD_e},              // 10 (Sera's Sundries), a bottle of 10
    {"50 Seeds", "Slingshot Seeds", "Ammo for your slingshot!", 5, K_SEEDS, dItemNo_PACHINKO_SHOT_e},           // (no shop sells them: 10, as a pack of arrows)
}};
constexpr int kWareCount = static_cast<int>(kWares.size());

// his voice: programs 0xEC/0xED (the travellers' voices use 0x88-0xDF, the camp songs 0xE0-0xEB)
constexpr u8 kPrgGoodbye = 0xEC, kPrgThanks = 0xED;
u32 g_goodbye = 0, g_thanks = 0;

std::array<u16, kWareCount> g_offerNode{};
std::array<u16, kWareCount> g_tentOfferNode{};   // the same, for a ware on a cushion in his tent
u16 g_tentHelloNode = 0xFFFF;
std::array<u16, kWareCount> g_shopWare{};   // the tent shop: each ware's offer (M; its description is M+1)
u16 g_shopGreet = 0xFFFF;                   // ...and his welcome (G; G+1 is the cursor back on him)
FlowEventId g_eBye{};
int g_boughtSerial = 0;   // counts purchases (the tent sees one happen)
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
    case K_SHIELD: return true;
    case K_LARVA: return has(dItemNo_EMPTY_BOTTLE_e);
    case K_SEEDS: return has(dItemNo_PACHINKO_e);
    }
    return false;
}

// room for it (or he'd only be taking Link's Rupees)
bool room(const Ware& w) {
    switch (w.kind) {
    case K_ARROWS: return dComIfGs_getArrowNum() < dComIfGs_getArrowMax();
    case K_BOMBS: case K_WATER: case K_POKE: return bagRoom(bombTypeOf(w.kind), countOf(w));
    case K_BOTTLE: return dComIfGs_checkEmptyBottle() > 0;
    case K_SHIELD: return !dComIfGs_isCollectShield(COLLECT_WOODEN_SHIELD);
    case K_LARVA: return dComIfGs_checkEmptyBottle() > 0;
    case K_SEEDS: return dComIfGs_getPachinkoNum() < dComIfGs_getPachinkoMax();
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
    ++g_boughtSerial;
    travellers::diagLine("beedle  sold %s for %d", w.name, w.price);
    auto* at = static_cast<const fopAc_ac_c*>(e->speaker_actor);
    playVoice(g_thanks, at);
    beedle_field::praise();
}
void onBye(ModContext*, const FlowEventContext* e, void*) {
    playVoice(g_goodbye, static_cast<const fopAc_ac_c*>(e->speaker_actor));
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

ModResult g_lastErr = MOD_OK;
std::deque<mods::flow::RegisteredMessage> g_messages;   // his lines, alive as long as the mod
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
        // kept for good: the SDK's message object takes its message away again when it goes
        auto& msg = g_messages.emplace_back(mods::flow::register_message(group, v));
        if (!msg) { g_lastErr = msg.result(); return -1; }
        auto n = graph.add_message(msg.id());
        n.next(next);
        return n.id();
    };
    const int bye = node([](auto& b) { b.text("Come back any time!"); }, mods::flow::kEnd);
    const int poor = node([](auto& b) { b.text("Huh? You don't have enough Rupees!\nCome back when you do!"); }, mods::flow::kEnd);
    const int full = node([](auto& b) { b.text("Looks like you can't carry any more\nof that! Next time, then!"); }, mods::flow::kEnd);
    const int thanks = node([](auto& b) { b.text("THANK YOU!"); }, mods::flow::kEnd);
    const int nothing = node([](auto& b) { b.text("Ah, I've got nothing you can use today.\nCome back tomorrow!"); }, mods::flow::kEnd);
    if (bye < 0 || poor < 0 || full < 0 || thanks < 0 || nothing < 0) return g_lastErr != MOD_OK ? g_lastErr : MOD_ERROR;
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
        if (offer < 0) return g_lastErr != MOD_OK ? g_lastErr : MOD_ERROR;
        g_offerNode[i] = static_cast<u16>(offer);
        // in his tent, about the ware on the cushion Link is at (its own choice, the same purchase)
        auto choose2 = graph.add_branch(FLOW_QUERY_SELECT_2, 0);
        choose2.results({check.id(), static_cast<u16>(bye)});
        const int offer2 = node([&](auto& b) {
            b.text_color(MESSAGE_COLOR_RED).text(w.name).text_color(MESSAGE_COLOR_DEFAULT);
            b.text("? Good eye!\nJust ");
            b.text_color(MESSAGE_COLOR_RED).text(price).text_color(MESSAGE_COLOR_DEFAULT);
            b.text(". Want it?");
            b.options("I'll take it!", "No thanks");
        }, choose2.id());
        if (offer2 < 0) return g_lastErr != MOD_OK ? g_lastErr : MOD_ERROR;
        g_tentOfferNode[i] = static_cast<u16>(offer2);
    }
    // --- his tent as a real shop (dShopSystem_c, see tent_beedle.cpp) ---------------------------------
    // The game's shop finds its talk by flow number: the shopkeeper's own (G: his welcome; G+1: the
    // cursor back on him: "anything else?", or GOODBYE when Link backs out), and each ware's (M: the
    // offer, A pressed on it; M+1: its description while the cursor is on it). Custom flow nodes pass
    // straight through the game's flow lookup, so the pairs only need to be back to back.
    if (ModResult r = svc_flow->register_event(mod_ctx, "psr_beedle_bye", onBye, nullptr, &g_eBye); r != MOD_OK) return r;
    auto shopWait = [](auto& b) {   // as Malo's: the box stays up while Link browses (the shop's send control)
        const u8 none[1] = {0};
        b.raw_tag(0, 0x1b, std::span<const u8>(none, 0)).raw_tag(0, 0x15, std::span<const u8>(none, 0)).auto_advance(0);
    };
    {
        const int greet = node([&](auto& b) {
            b.talk_anim(1);
            b.text("Welcome to Beedle's tent shop!\nHave a look! Something here's bound\nto catch your eye! ");
            shopWait(b);
        }, mods::flow::kEnd);
        auto keeper = graph.add_branch(static_cast<FlowQueryId>(FLOW_QUERY_SHOP_CLERK), 0);
        if (greet < 0 || keeper.id() != greet + 1) {
            travellers::diagLine("beedle  shop flows not back to back (%d, %d)", greet, keeper.id());
        }
        const int more = node([&](auto& b) {
            b.talk_anim(2);
            b.text("Anything else catch your eye? ");
            shopWait(b);
        }, mods::flow::kEnd);
        const int bye = node([](auto& b) {
            b.talk_anim(3);
            b.text("GOODBYE!\nCome back soon, OK?");
        }, mods::flow::kEnd);
        auto byeEv = graph.add_event(g_eBye, {0, 0, 0, 0});
        byeEv.next(static_cast<u16>(bye));
        keeper.results({static_cast<u16>(more), byeEv.id()});
        if (more < 0 || bye < 0) return g_lastErr != MOD_OK ? g_lastErr : MOD_ERROR;
        g_shopGreet = static_cast<u16>(greet);
    }
    const int sThanks = node([](auto& b) { b.talk_anim(5); b.text("THANK YOU!"); }, mods::flow::kEnd);
    const int sPoor = node([](auto& b) { b.talk_anim(4); b.text("Huh? You don't have enough Rupees!\nCome back when you do!"); }, mods::flow::kEnd);
    const int sFull = node([](auto& b) { b.talk_anim(4); b.text("Looks like you can't carry any more\nof that! Next time, then!"); }, mods::flow::kEnd);
    if (sThanks < 0 || sPoor < 0 || sFull < 0) return g_lastErr != MOD_OK ? g_lastErr : MOD_ERROR;
    for (int i = 0; i < kWareCount; ++i) {
        const Ware& w = kWares[i];
        char price[24];
        std::snprintf(price, sizeof(price), "%u Rupee%s", static_cast<unsigned>(w.price), w.price == 1 ? "" : "s");
        // M: can he buy it? -> the choice (the game's vertical select, B = no) -> the offer -> yes/no
        auto entry = graph.add_branch(g_qCanBuy, static_cast<u16>(i));
        const int desc = node([&](auto& b) {   // M+1
            b.speaker(3).draw_type(MESSAGE_DRAW_INSTANT);
            b.text_color(MESSAGE_COLOR_ORANGE).text(w.title).text(": ");
            b.text_color(MESSAGE_COLOR_PURPLE).text(price).text("\n").text_color(MESSAGE_COLOR_DEFAULT);
            b.text(w.desc);
        }, mods::flow::kEnd);
        if (desc < 0) return g_lastErr != MOD_OK ? g_lastErr : MOD_ERROR;
        if (desc != entry.id() + 1) {
            travellers::diagLine("beedle  ware %d flows not back to back (%d, %d)", i, entry.id(), desc);
        }
        auto buyEv = graph.add_event(g_eBuy, {0, 0, static_cast<u8>(i >> 8), static_cast<u8>(i)});
        buyEv.next(static_cast<u16>(sThanks));
        auto yesNo = graph.add_branch(FLOW_QUERY_SELECT_2, 0);
        yesNo.results({buyEv.id(), mods::flow::kEnd});
        const int opts = node([](auto& b) { b.options("I'll take it!", "No thanks"); }, yesNo.id());
        const int offer = node([&](auto& b) {
            b.talk_anim(static_cast<u8>(1 + (i & 3)));
            b.text_color(MESSAGE_COLOR_ORANGE).text(w.name).text_color(MESSAGE_COLOR_DEFAULT).text("? Good eye!\nJust ");
            b.text_color(MESSAGE_COLOR_PURPLE).text(price).text_color(MESSAGE_COLOR_DEFAULT).text(". Want it?");
            b.await_choice();
        }, static_cast<u16>(opts));
        if (opts < 0 || offer < 0) return g_lastErr != MOD_OK ? g_lastErr : MOD_ERROR;
        auto select = graph.add_event(static_cast<FlowEventId>(FLOW_EVENT_SELECT_VERTICAL), {0, 0, 0, 2});
        select.next(static_cast<u16>(offer));
        entry.results({select.id(), static_cast<u16>(sPoor), static_cast<u16>(sFull)});
        g_shopWare[i] = entry.id();
    }

    const int hello = node([](auto& b) {
        b.text("Welcome to Beedle's tent shop!\nHave a look at the cushions, and if\nanything catches your eye, just ask!");
    }, mods::flow::kEnd);
    if (hello < 0) return g_lastErr != MOD_OK ? g_lastErr : MOD_ERROR;
    g_tentHelloNode = static_cast<u16>(hello);
    g_ready = true;
    return MOD_OK;
}

void markUnready() { g_ready = false; }

// --- his tent's shop ------------------------------------------------------------------------------
int tentWares(int* out, int maxOut) {
    // today's wares (as many as his tables hold): those Link can use, in an order the day shuffles (the seeds have no shop model)
    int can[kWareCount];
    int n = 0;
    for (int i = 0; i < kWareCount; ++i) {
        if (unlocked(kWares[i]) && kWares[i].kind != K_SEEDS) can[n++] = i;
    }
    u32 h = static_cast<u32>(dComIfGs_getDate()) * 2246822519u + 0x9E3779B9u;
    for (int i = n - 1; i > 0; --i) {
        h ^= h << 13; h ^= h >> 17; h ^= h << 5;
        const int j = static_cast<int>(h % static_cast<u32>(i + 1));
        std::swap(can[i], can[j]);
    }
    const int k = std::min(n, maxOut);
    for (int i = 0; i < k; ++i) out[i] = can[i];
    return k;
}

u8 wareDisplayItem(int ware) {
    // what the game's own shop shows for it (daShopItem_c: a bomb, a bottle, the shield...)
    switch (kWares[ware].item) {
    case dItemNo_WATER_BOMB_5_e: return dItemNo_WATER_BOMB_e;
    case dItemNo_BOMB_INSECT_5_e: return dItemNo_POKE_BOMB_e;
    case dItemNo_WOOD_SHIELD_e: return dItemNo_SHIELD_e;
    default: return kWares[ware].item;
    }
}

u16 tentOfferNode(int ware) { return g_ready && ware >= 0 && ware < kWareCount ? g_tentOfferNode[ware] : 0xFFFF; }
u16 shopGreetNode() { return g_ready ? g_shopGreet : 0xFFFF; }
u16 shopWareNode(int ware) { return g_ready && ware >= 0 && ware < kWareCount ? g_shopWare[ware] : 0xFFFF; }
u16 tentHelloNode() { return g_ready ? g_tentHelloNode : 0xFFFF; }
int boughtSerial() { return g_boughtSerial; }
int lastBought() { return g_lastBought; }
void sayGoodbye(const fopAc_ac_c* at) { playVoice(g_goodbye, at); }

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
