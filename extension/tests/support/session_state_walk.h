#pragma once

#include "netw_cells.h"

#include <cstdint>
#include <vector>

#include "netw/api/enums.hpp"

namespace netw_test {

struct StateHeard {
    int64_t emitted = -1;
    int64_t read = -1;
};

inline LawVerdict law_state_walk(
    const std::vector<StateHeard> &p_walk,
    int64_t p_from = netw::enums::NetwSession::STATE_OFFLINE
) {
    const int64_t offline = netw::enums::NetwSession::STATE_OFFLINE;
    const int64_t online = netw::enums::NetwSession::STATE_ONLINE;
    int64_t was = p_from;
    for (size_t at = 0; at < p_walk.size(); at++) {
        const StateHeard &heard = p_walk[at];
        if (heard.emitted != heard.read) {
            return law_broken(
                "change %d emitted %d while state read %d",
                int(at),
                int(heard.emitted),
                int(heard.read)
            );
        }
        const bool climbs = heard.emitted > was;
        const bool lands = heard.emitted == offline && was != online;
        if (heard.emitted == was || (!climbs && !lands)) {
            return law_broken(
                "change %d moved from %d to %d",
                int(at),
                int(was),
                int(heard.emitted)
            );
        }
        was = heard.emitted;
    }
    return law_held();
}

} // namespace netw_test
