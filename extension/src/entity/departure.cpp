#include "netw/entity/departure.hpp"

namespace netw::entity {

Outcome classify(const Settlement &p_settlement) {
    if (p_settlement.terminal) {
        return Outcome::DEATH;
    }
    if (p_settlement.hidden) {
        return Outcome::HIDE;
    }
    if (p_settlement.owner_live && p_settlement.owner_in_tree
        && p_settlement.session_current) {
        return Outcome::MOVE;
    }
    if (p_settlement.received) {
        return Outcome::HIDE;
    }
    return Outcome::DEATH;
}

} // namespace netw::entity
