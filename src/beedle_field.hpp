#pragma once
// Beedle in the field: his one-ware-a-day shop through his talk, his "THANK YOU!" / "GOODBYE!", his
// Wind Waker talking while Link talks to him (beedle_field.cpp).
#include "dolphin/types.h"
#include "mods/svc/flow.hpp"
#include "mods/svc/message.h"
class daNpcPasser_c;
class fopAc_ac_c;
namespace beedle_field {
bool installSounds();   // his voice clips (res/voices/beedle_*.wav)
// his shop's talk (one offer per ware, the check, the purchase, his answers), into the talk graph
// Beedle's shop talk is off (its graph failed to register).
void markUnready();
ModResult registerFlows(mods::flow::GraphBuilder& graph, const MessageLanguage* langs, int nLangs, u16 group);
u16 talkNode();                     // where today's talk starts (0xFFFF: not ready)
void talkStarted(daNpcPasser_c* p); // Link has started talking to him
void talkTick(daNpcPasser_c* p);    // each frame of it (he holds still, faces Link)
void talkEnded(daNpcPasser_c* p);   // "GOODBYE!"
void praise();                      // paid: his flourish
// his tent's shop: today's wares on the cushions (up to maxOut), what the game's shop shows for each,
// the talk about one of them, his welcome, and how many purchases there have been (and the last)
int tentWares(int* out, int maxOut);
u8 wareDisplayItem(int ware);
u16 tentOfferNode(int ware);
u16 tentHelloNode();
// the tent as a real shop: his welcome flow (G; G+1 his cursor flow) and each ware's (M; M+1 its description)
u16 shopGreetNode();
void newVisit();   // Link has come into the tent: his next talk gets the OHHHHH welcome
u16 shopWareNode(int ware);
int boughtSerial();
int lastBought();
void sayGoodbye(const fopAc_ac_c* at);
}  // namespace beedle_field
