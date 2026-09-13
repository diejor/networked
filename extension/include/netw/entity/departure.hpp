#pragma once

namespace netw::entity {

enum class Outcome {
    MOVE,
    DEATH,
    HIDE,
};

struct Settlement {
    bool terminal = false;
    bool hidden = false;
    bool owner_live = false;
    bool owner_in_tree = false;
    bool session_current = false;
    bool received = false;
};

Outcome classify(const Settlement &p_settlement);

} // namespace netw::entity
