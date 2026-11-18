#include "dialogue_marks.hpp"

#include "convoys.hpp"
#include "travellers.hpp"

namespace dialogue {

Spot spotFor(const fopAc_ac_c* actor) {
    if (actor == nullptr) return SPOT_NONE;
    if (travellers::isBeedle(actor)) return SPOT_BEEDLE;
    if (travellers::isGuard(actor)) {
        return travellers::eldinBridgeWarpedBack() ? SPOT_GUARD_BRIDGE_BACK : SPOT_GUARD_BRIDGE_OUT;
    }
    switch (convoys::crewSpot(travellers::slotIndex(actor))) {
        case 0: return SPOT_CONVOY_GORGE;
        case 1: return SPOT_CONVOY_WEST;
        case 2: return SPOT_CONVOY_ROYAL;
        case 10: return SPOT_NOD_WALL1;
        case 11: return SPOT_NOD_WALL2;
        default: return SPOT_NONE;
    }
}

const char* placeholder(Spot spot) {
    static const char* const kText[SPOT_COUNT] = {
        "[LINE: nod wagon, wall 1]",
        "[LINE: nod wagon, wall 2 - they know Link]",
        "[LINE: guard camp, bridge out]",
        "[LINE: guard camp, bridge back - one more ale]",
        "[LINE: gorge convoy]",
        "[LINE: west-gate convoy]",
        "[LINE: royal convoy]",
        "[Beedle: his shop]",
    };
    return spot > SPOT_NONE && spot < SPOT_COUNT ? kText[spot] : nullptr;
}

// The lines (agreed with the user, 2026-11-18). Talking to someone at a spot says one of its lines
// at random. Message boxes wrap long lines themselves; '\n' starts a new line in the box.
namespace {
const char* const kLines[SPOT_COUNT][kMaxLines] = {
    // the first wall: plain worry
    {"You see that wall of black? We drove\nup to it and the horse wouldn't take\nanother step. We're waiting it out.",
     "Folk say anyone who walks into that\ndarkness doesn't come back. So... we\nwait."},
    // the second wall: the same three, and they know Link
    {"Hey! You're the guy from the last\nwall! ...Are you following us, or are\nwe following you?",
     "The last wall vanished right after you\nturned up. I'm not saying it was you.\nI'm just saying... could you do it\nagain?",
     "Same wagon, same horse, same wall.\nDifferent wall. You know what I mean."},
    // the guard camp, the bridge gone
    {"The Eldin bridge vanished overnight.\nOur captain wants a report on where it\nwent. \"Up\" is not an acceptable\nanswer, apparently.",
     "Nothing gets across this gorge now.\nNot wagons, not guards... not even the\nmail. Especially not the mail.",
     "We're holding this position! ...Mostly\nby sitting on it."},
    // the guard camp, the bridge back
    {"The bridge fell out of the sky and\nlanded right where it used to be!\nWe're getting back on the road. After\none more ale.",
     "Yes, yes, we're leaving. Right after\nthis ale. And possibly the next one.\nDuty calls, but it can wait.",
     "Repaired, they say! I've checked it\ntwice. Well, I've looked at it twice.\nFrom here."},
    // the gorge travellers
    {"We're headed for Castle Town. The road\nthrough the gorge is safe enough now\nthat the darkness has lifted.",
     "Stay close if you're going our way.\nThe Bulblins don't like a crowd."},
    // the west-gate travellers
    {"Down to Faron from the west gate. It's\na long road, but it beats sitting in\ntown.",
     "Faron Woods, here we come! I hear the\nmonkeys there steal your lunch. I've\npacked two lunches.",
     "Long road down to Faron. We'll get\nthere. The wheels might not, but we\nwill."},
    // the royal convoy
    {"Royal business! Please step aside. No,\nyou can't peek in the crates. Yes,\neveryone asks.",
     "This wagon belongs to the Crown. So\ndoes this horse. And this road,\ntechnically. Mind your step.",
     "Keep moving, citizen. Nothing to see\nhere except a very important wagon\nfull of very important... things."},
    // Beedle: his shop's own talk (beedle_field.cpp)
    {},
};
}  // namespace

int lineCount(Spot spot) {
    if (spot <= SPOT_NONE || spot >= SPOT_COUNT) return 0;
    int n = 0;
    while (n < kMaxLines && kLines[spot][n] != nullptr) ++n;
    return n;
}

const char* line(Spot spot, int i) { return i >= 0 && i < lineCount(spot) ? kLines[spot][i] : nullptr; }

}  // namespace dialogue
