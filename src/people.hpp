#pragma once
// Who the camp travellers are: each roster traveller (not Beedle, the guards or convoy people) is a
// person with a name, two or three personality traits, a home camp (or none: a wanderer), the camp
// they last slept at, and whether Link has met them. Kept with the save file (a save blob), so the
// same people are out there every time that save is played.
//
// travellers.cpp decides who lives where (it knows the roads and camps); this file keeps the records,
// names them, gives them their traits, saves and loads them, and writes what they say to Link.

#include "global.h"
#include <dolphin/types.h>
#include "mods/svc/flow.hpp"

namespace people {

constexpr int kCount = 29;   // roster slots 0..28 (slot 29 is Beedle)

enum Trait : u16 {
    T_HOTHEAD = 1 << 0,
    T_PROUD = 1 << 1,
    T_GREEDY = 1 << 2,
    T_COWARD = 1 << 3,
    T_KIND = 1 << 4,
    T_GOSSIP = 1 << 5,
    T_GRUMPY = 1 << 6,
    T_JOKER = 1 << 7,
    T_GENEROUS = 1 << 8,
    T_BRAVE = 1 << 9,
    T_CHEERFUL = 1 << 10,
    T_SHY = 1 << 11,
};
constexpr int kTraitCount = 12;
const char* traitName(int bit);   // "hot-headed", ... (for the log)

struct Person {
    bool used = false;
    u8 type = 0;          // body (stock passer type)
    u8 name = 0xFF;       // index into the names list
    u16 traits = 0;
    s8 home = -1;         // home camp (index into the camp list), -1: a wanderer
    s8 slept = -1;        // camp it last slept at, -1: none yet
    bool metLink = false;
    u16 since = 0;        // game date it joined the roster (a newcomer replaces someone who fell)
};

Person* get(int slot);          // nullptr unless slot is a person
bool isPerson(int slot);
const char* name(int slot);     // "" if none

// A new person in this slot: a name nobody else on the roster has, and traits. home/slept as given.
void make(int slot, u8 type, s8 home, bool fighter);
bool ready();                   // the people are cast (or loaded) for the current save

// Saving: changes are kept with the save file at once (the game writes them out when it saves).
void markChanged();
void tick();                    // writes the blob when something changed (at most once a second)

// Save lifecycle (SaveService). travellers.cpp is told through these to cast or place everyone.
void install(void (*onFresh)(), void (*onLoaded)());

// Talking to Link: one message whose text is written when it's shown (who's talking, whether Link
// has met them). talkNode() is its flow node; talkStarted() says who's talking.
ModResult registerTalk(mods::flow::GraphBuilder& graph, u16 group, const MessageLanguage* langs, int nLangs);
u16 talkNode();
void talkStarted(int slot);

}  // namespace people
