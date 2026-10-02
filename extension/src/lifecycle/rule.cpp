#include "netw/lifecycle/rule.hpp"

#include "godot/utility.hpp"

namespace netw::lifecycle {

namespace {

const char *does_to(Kind p_kind) {
    switch (p_kind) {
        case Kind::SPAWN:
            return "spawns";
        case Kind::DESPAWN:
            return "despawns";
        case Kind::REPARENT:
            break;
    }
    return "moves";
}

const char *act(Kind p_kind) {
    switch (p_kind) {
        case Kind::SPAWN:
            return "spawn";
        case Kind::DESPAWN:
            return "despawn";
        case Kind::REPARENT:
            break;
    }
    return "move";
}

const char *why_session_only(const Facts &p_facts) {
    if (p_facts.predicted) {
        return "predicted";
    }
    if (p_facts.native) {
        return "replicated through a MultiplayerSynchronizer or a "
               "MultiplayerSpawner";
    }
    if (p_facts.player_bound) {
        return "a player's body";
    }
    return "loaded from its database at spawn "
           "(NetwPersistenceConfig.load_on_spawn)";
}

const char *where_it_fails(Destination p_destination) {
    switch (p_destination) {
        case Destination::OUTSIDE:
            return "is outside the session root. Pick a parent under the "
                   "session root.";
        case Destination::INSIDE_MOVER:
            return "is inside the entity itself. Pick a parent outside it.";
        case Destination::DYING:
            return "is being despawned. Pick a parent that stays.";
        case Destination::UNRESOLVABLE:
        case Destination::OK:
            break;
    }
    return "is not known to the session authority. Pick a parent every peer "
           "has.";
}

godot::String unauthorized_text(
    const godot::String &p_verb,
    const godot::String &p_root,
    const Facts &p_facts,
    const Ruling &p_ruling
) {
    if (p_ruling.verdict == Verdict::DYNAMIC) {
        return godot::vformat(
            "%s on '%s' was refused, because the session authority changed "
            "it first. Call it again once that change arrives.",
            p_verb,
            p_root
        );
    }
    if (!p_facts.declared) {
        return godot::vformat(
            "%s on '%s' was refused, because only the session authority %s "
            "it. Set NetwEntity.lifecycle to LIFECYCLE_CONTROLLER in its "
            "root's _init to let %s %s it.",
            p_verb,
            p_root,
            does_to(p_facts.kind),
            p_facts.kind == Kind::SPAWN ? "any peer" : "its controller",
            act(p_facts.kind)
        );
    }
    if (p_facts.peer_claim == Claim::REQUESTABLE) {
        return godot::vformat(
            "%s on '%s' was refused, because this peer's claim on it is not "
            "granted yet. Call it once the claim's promise resolves.",
            p_verb,
            p_root
        );
    }
    return godot::vformat(
        "%s on '%s' was refused, because this peer is neither the session "
        "authority nor its controller. Call it from the peer that controls "
        "it.",
        p_verb,
        p_root
    );
}

Ruling refuse(Verdict p_verdict, godot::Error p_code) {
    Ruling ruling;
    ruling.verdict = p_verdict;
    ruling.code = p_code;
    return ruling;
}

bool unavailable_to_a_controller(const Facts &p_facts) {
    return p_facts.predicted || p_facts.native || p_facts.player_bound
        || p_facts.load_on_spawn;
}

bool names_a_place(const Facts &p_facts) {
    return p_facts.kind != Kind::DESPAWN;
}

bool misplaced_by_the_caller(const Facts &p_facts) {
    return names_a_place(p_facts)
        && (p_facts.destination == Destination::OUTSIDE
            || p_facts.destination == Destination::INSIDE_MOVER);
}

bool misplaced_in_flight(const Facts &p_facts) {
    return names_a_place(p_facts)
        && (p_facts.destination == Destination::DYING
            || p_facts.destination == Destination::UNRESOLVABLE);
}

bool builds_on_a_stale_base(const Facts &p_facts) {
    return p_facts.kind != Kind::SPAWN
        && (p_facts.base != p_facts.revision
            || p_facts.base_author != p_facts.revision_author);
}

} // namespace

bool may_author(const Facts &p_facts) {
    if (p_facts.peer_is_session) {
        return true;
    }
    if (!p_facts.declared) {
        return false;
    }
    if (p_facts.kind == Kind::SPAWN) {
        return true;
    }
    return p_facts.peer_is_controller || p_facts.peer_claim == Claim::IMMEDIATE;
}

Ruling rule(const Facts &p_facts) {
    if (p_facts.minting_halted) {
        return refuse(Verdict::STATIC, godot::ERR_UNAVAILABLE);
    }
    if (p_facts.replaying) {
        return refuse(Verdict::STATIC, godot::ERR_BUSY);
    }
    if (!may_author(p_facts)) {
        return refuse(Verdict::STATIC, godot::ERR_UNAUTHORIZED);
    }
    if (!p_facts.peer_is_session && unavailable_to_a_controller(p_facts)) {
        return refuse(Verdict::STATIC, godot::ERR_UNAVAILABLE);
    }
    if (misplaced_by_the_caller(p_facts)) {
        return refuse(Verdict::STATIC, godot::ERR_INVALID_PARAMETER);
    }
    if (p_facts.peer_is_session) {
        return Ruling();
    }
    if (misplaced_in_flight(p_facts)) {
        return refuse(Verdict::DYNAMIC, godot::ERR_INVALID_PARAMETER);
    }
    if (builds_on_a_stale_base(p_facts)) {
        return refuse(Verdict::DYNAMIC, godot::ERR_UNAUTHORIZED);
    }
    return Ruling();
}

Ruling rule(Kind p_kind, bool p_peer_is_session) {
    Facts facts;
    facts.kind = p_kind;
    facts.peer_is_session = p_peer_is_session;
    return rule(facts);
}

godot::String refusal_text(
    const godot::String &p_verb,
    const godot::String &p_root,
    const Facts &p_facts,
    const Ruling &p_ruling
) {
    switch (p_ruling.code) {
        case godot::OK:
            return godot::String();
        case godot::ERR_UNAUTHORIZED:
            return unauthorized_text(p_verb, p_root, p_facts, p_ruling);
        case godot::ERR_BUSY:
            return godot::vformat(
                "%s on '%s' was refused, because it ran during a prediction "
                "replay. Call it outside the replay.",
                p_verb,
                p_root
            );
        case godot::ERR_INVALID_PARAMETER:
            return godot::vformat(
                "%s on '%s' was refused, because the new parent %s",
                p_verb,
                p_root,
                where_it_fails(p_facts.destination)
            );
        default:
            break;
    }
    if (p_facts.minting_halted) {
        return godot::vformat(
            "%s on '%s' was refused, because session authority is moving to "
            "another peer. Call it again once the move completes.",
            p_verb,
            p_root
        );
    }
    return godot::vformat(
        "%s on '%s' was refused, because it is %s, so only the session "
        "authority %s it, whatever NetwEntity.lifecycle says. Call it on the "
        "server.",
        p_verb,
        p_root,
        why_session_only(p_facts),
        does_to(p_facts.kind)
    );
}

godot::String lease_empty_text(
    const godot::String &p_verb,
    const godot::String &p_root
) {
    return godot::vformat(
        "%s on '%s' was refused, because this peer used up the spawns the "
        "session authority granted it. More are granted as its spawns are "
        "admitted, so try again shortly.",
        p_verb,
        p_root
    );
}

} // namespace netw::lifecycle
