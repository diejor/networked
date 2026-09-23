#pragma once

#include <cstdint>

namespace netw::sim {

enum class Mode : uint8_t {
    NONE = 0,
    AUTHORITY = 1,
    PREDICT = 2,
    ACTIVE = 3,
    PROXY = 4,
};

enum class Replicas : uint8_t {
    PROXY = 0,
    ACTIVE = 1,
};

struct Facts {
    bool declared = false;
    bool predicted = false;
    bool state_rows = false;
    bool input_rows = false;
    bool session_authority_here = false;
    bool controller_here = false;
    bool controller_is_nobody = false;
    bool pending_claim_here = false;
    bool fallback_latched = false;
    bool delay_closed = false;
    Replicas replicas = Replicas::PROXY;
    uint32_t selection_count = 0;
};

bool controller_authored(const Facts &p_facts);

bool session_authors(const Facts &p_facts);

bool authors_here(const Facts &p_facts);

bool executes(Mode p_mode);

bool leads(Mode p_mode);

Mode resolve(const Facts &p_facts);

} // namespace netw::sim
