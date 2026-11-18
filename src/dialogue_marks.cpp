#include "dialogue_marks.hpp"

#include "convoys.hpp"
#include "travellers.hpp"

namespace dialogue {

Spot spotFor(const fopAc_ac_c* actor) {
    if (actor == nullptr) return SPOT_NONE;
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
    };
    return spot > SPOT_NONE && spot < SPOT_COUNT ? kText[spot] : nullptr;
}

}  // namespace dialogue
