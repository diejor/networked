#include "netw/sim/resolve.hpp"

namespace netw::sim {

bool session_authors(const Facts &p_facts) {
    return p_facts.predicted || p_facts.state_rows
        || p_facts.controller_is_nobody;
}

bool authors_here(const Facts &p_facts) {
    return session_authors(p_facts) ? p_facts.session_authority_here
                                    : p_facts.controller_here;
}

bool executes(Mode p_mode) {
    return p_mode == Mode::AUTHORITY || p_mode == Mode::PREDICT
        || p_mode == Mode::ACTIVE;
}

bool leads(Mode p_mode) {
    return p_mode == Mode::AUTHORITY || p_mode == Mode::PREDICT;
}

Mode resolve(const Facts &p_facts) {
    if (!p_facts.declared) {
        return Mode::NONE;
    }
    if (authors_here(p_facts)) {
        return Mode::AUTHORITY;
    }
    if (p_facts.pending_claim_here && !session_authors(p_facts)) {
        return Mode::AUTHORITY;
    }
    const bool closed = p_facts.fallback_latched || p_facts.delay_closed;
    if (p_facts.predicted && p_facts.input_rows && p_facts.controller_here) {
        return closed ? Mode::PROXY : Mode::PREDICT;
    }
    if (closed) {
        return Mode::PROXY;
    }
    if (p_facts.replicas == Replicas::ACTIVE || p_facts.selection_count > 0) {
        return Mode::ACTIVE;
    }
    return Mode::PROXY;
}

} // namespace netw::sim
