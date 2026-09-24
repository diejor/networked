#pragma once

#include <cstdint>

#include "godot/variant.hpp"

namespace netw::lifecycle {

enum class Kind : uint8_t {
    SPAWN = 0,
    DESPAWN = 1,
    REPARENT = 2,
};

enum class Claim : uint8_t {
    NONE = 0,
    IMMEDIATE = 1,
    REQUESTABLE = 2,
};

enum class Destination : uint8_t {
    OK = 0,
    OUTSIDE = 1,
    INSIDE_MOVER = 2,
    DYING = 3,
    UNRESOLVABLE = 4,
};

enum class Verdict : uint8_t {
    ADMIT = 0,
    STATIC = 1,
    DYNAMIC = 2,
};

struct Facts {
    Kind kind = Kind::SPAWN;
    bool peer_is_session = false;
    bool peer_is_controller = false;
    Claim peer_claim = Claim::NONE;
    bool declared = false;
    bool predicted = false;
    bool native = false;
    bool player_bound = false;
    bool load_on_spawn = false;
    bool replaying = false;
    bool minting_halted = false;
    Destination destination = Destination::OK;
    uint64_t base = 0;
    uint64_t base_author = 0;
    uint64_t revision = 0;
    uint64_t revision_author = 0;
};

struct Ruling {
    Verdict verdict = Verdict::ADMIT;
    godot::Error code = godot::OK;

    bool admitted() const {
        return verdict == Verdict::ADMIT;
    }
};

bool may_author(const Facts &p_facts);

Ruling rule(const Facts &p_facts);

Ruling rule(Kind p_kind, bool p_peer_is_session);

godot::String refusal_text(
    const godot::String &p_verb,
    const godot::String &p_root,
    const Facts &p_facts,
    const Ruling &p_ruling
);

godot::String lease_empty_text(
    const godot::String &p_verb,
    const godot::String &p_root
);

} // namespace netw::lifecycle
