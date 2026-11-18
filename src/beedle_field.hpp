#pragma once
// Beedle in the field: his one-ware-a-day shop through his talk, his "THANK YOU!" / "GOODBYE!", his
// Wind Waker talking while Link talks to him (beedle_field.cpp).
#include "dolphin/types.h"
#include "mods/svc/flow.hpp"
#include "mods/svc/message.h"
class daNpcPasser_c;
namespace beedle_field {
bool installSounds();   // his two voice clips (res/voices/beedle_*.wav)
// his shop's talk (one offer per ware, the check, the purchase, his answers), into the talk graph
ModResult registerFlows(mods::flow::GraphBuilder& graph, const MessageLanguage* langs, int nLangs, u16 group);
u16 talkNode();                     // where today's talk starts (0xFFFF: not ready)
void talkStarted(daNpcPasser_c* p); // Link has started talking to him
void talkTick(daNpcPasser_c* p);    // each frame of it (he holds still, faces Link)
void talkEnded(daNpcPasser_c* p);   // "GOODBYE!"
void praise();                      // paid: his flourish
}  // namespace beedle_field
