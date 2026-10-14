#include "godot/callable.hpp"
#include "godot/utility.hpp"
#include "netw/api/clock_config.hpp"
#include "netw/api/lag_compensation_config.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/session_config.hpp"
#include "netw/log.hpp"
#include "netw/session_decl.hpp"

using namespace godot;

namespace netw {

namespace {

constexpr session_decl::Kind CONFIG_KINDS[3] = {
    session_decl::KIND_SESSION_CONFIG,
    session_decl::KIND_CLOCK_CONFIG,
    session_decl::KIND_LAGCOMP_CONFIG,
};

int slot_of(session_decl::Kind p_kind) {
    return int(p_kind) - int(session_decl::KIND_SESSION_CONFIG);
}

Error failure_of(session_decl::Availability p_state) {
    return p_state == session_decl::AMBIGUOUS ? ERR_ALREADY_EXISTS
                                              : ERR_UNAVAILABLE;
}

struct Selection {
    session_decl::Resolved offered[3];
    Ref<NetwSessionConfig> session;
    Ref<NetwClockConfig> clock;
    bool clock_declared = false;
    Ref<NetwLagCompensationConfig> lagcomp;
    bool lagcomp_defaults = false;
};

template <typename T>
Error typed_draft(
    const session_decl::Resolved &p_offered,
    session_decl::Kind p_kind,
    const char *p_by,
    Ref<T> &r_draft
) {
    r_draft = p_offered.payload;
    NETW_ERR_COND_V(
        r_draft.is_null(),
        ERR_INVALID_DATA,
        sys::SESSION,
        "The Netw.%s settings on '%s' cannot be read, so %s was refused. "
        "Declare them only through Netw.%s.",
        session_decl::verb_of(p_kind),
        p_offered.scope,
        p_by,
        session_decl::verb_of(p_kind)
    );
    return OK;
}

} // namespace

bool NetwMultiplayer::config_is_consumed(session_decl::Kind p_kind) const {
    return config_consumption[slot_of(p_kind)].consumed;
}

bool NetwMultiplayer::config_is_applying() const {
    return config_applying;
}

void NetwMultiplayer::declarations_changed() {
    if (config_settle_queued) {
        return;
    }
    config_settle_queued = true;
    callable_mp(this, &NetwMultiplayer::config_settle).call_deferred();
}

void NetwMultiplayer::config_settle() {
    config_settle_queued = false;
    if (apply_pending_config(false, "configuration") == ERR_BUSY) {
        declarations_changed();
    }
}

Error NetwMultiplayer::apply_pending_config(
    bool p_lagcomp_needed,
    const char *p_by
) {
    if (config_applying) {
        return ERR_BUSY;
    }
    Selection selected;
    for (const session_decl::Kind kind : CONFIG_KINDS) {
        const int slot = slot_of(kind);
        if (config_consumption[slot].consumed) {
            continue;
        }
        selected.offered[slot] = declaration_book().resolve(this, kind);
        const session_decl::Availability state = selected.offered[slot].state;
        if (state == session_decl::UNAVAILABLE
            || state == session_decl::AMBIGUOUS) {
            declaration_book().report_unresolved(this, kind, state, p_by);
            return failure_of(state);
        }
    }

    const int session = slot_of(session_decl::KIND_SESSION_CONFIG);
    if (!config_consumption[session].consumed
        && selected.offered[session].state == session_decl::READY) {
        const Error typed = typed_draft(
            selected.offered[session],
            session_decl::KIND_SESSION_CONFIG,
            p_by,
            selected.session
        );
        if (typed != OK) {
            return typed;
        }
    }
    const int clock = slot_of(session_decl::KIND_CLOCK_CONFIG);
    if (!config_consumption[clock].consumed) {
        if (selected.offered[clock].state == session_decl::READY) {
            const Error typed = typed_draft(
                selected.offered[clock],
                session_decl::KIND_CLOCK_CONFIG,
                p_by,
                selected.clock
            );
            if (typed != OK) {
                return typed;
            }
            selected.clock_declared = true;
        } else {
            selected.clock = clock_fallback;
        }
    }
    const int lagcomp = slot_of(session_decl::KIND_LAGCOMP_CONFIG);
    if (!config_consumption[lagcomp].consumed) {
        if (selected.offered[lagcomp].state == session_decl::READY) {
            const Error typed = typed_draft(
                selected.offered[lagcomp],
                session_decl::KIND_LAGCOMP_CONFIG,
                p_by,
                selected.lagcomp
            );
            if (typed != OK) {
                return typed;
            }
        } else {
            selected.lagcomp_defaults = p_lagcomp_needed || lagcomp_wanted
                || !predict_awaiting_config.is_empty();
        }
    }

    config_applying = true;
    Error committed = OK;
    if (selected.session.is_valid()) {
        committed = session_initialize(selected.session);
        if (committed == OK) {
            config_consumption[session].generation
                = selected.offered[session].generation;
            selected.session->seal(selected.offered[session].scope);
        }
    }
    bool clock_committed = false;
    if (committed == OK && selected.clock.is_valid()) {
        committed = clock_commit(selected.clock);
        clock_committed = committed == OK;
        if (clock_committed && selected.clock_declared) {
            config_consumption[clock].generation
                = selected.offered[clock].generation;
            selected.clock->seal(selected.offered[clock].scope);
        }
    }
    if (committed == OK && selected.lagcomp.is_valid()) {
        committed = lagcomp_initialize(
            selected.lagcomp->get_max_future_action_ticks(),
            selected.lagcomp->get_input_gate_deadline_ticks()
        );
        if (committed == OK) {
            config_consumption[lagcomp].generation
                = selected.offered[lagcomp].generation;
            selected.lagcomp->seal(selected.offered[lagcomp].scope);
        }
    } else if (committed == OK && selected.lagcomp_defaults) {
        committed = lagcomp_initialize(
            max_future_action_ticks,
            input_gate_deadline_ticks
        );
    }
    if (clock_committed) {
        notify(CLOCK_CONFIGURED);
    }
    config_applying = false;

    if (committed != OK || predict_awaiting_config.is_empty()
        || !lagcomp_configured) {
        return committed;
    }
    LocalVector<RID> waiting;
    waiting.reserve(predict_awaiting_config.size());
    for (const RID &held : predict_awaiting_config) {
        waiting.push_back(held);
    }
    predict_awaiting_config.clear();
    for (const RID &entity : waiting) {
        const Ref<NetwEntity> wrapper = entity_get_view(entity);
        if (wrapper.is_valid()) {
            register_prediction(wrapper);
        }
    }
    return OK;
}

void NetwMultiplayer::lagcomp_want() {
    if (lagcomp_wanted
        || config_is_consumed(session_decl::KIND_LAGCOMP_CONFIG)) {
        return;
    }
    lagcomp_wanted = true;
    declarations_changed();
}

Error NetwMultiplayer::lagcomp_require(const char *p_by) {
    if (config_applying) {
        return ERR_BUSY;
    }
    if (!config_is_consumed(session_decl::KIND_LAGCOMP_CONFIG)) {
        const Error applied = apply_pending_config(true, p_by);
        if (applied != OK) {
            return applied;
        }
    }
    lagcomp_arm();
    return OK;
}

Error NetwMultiplayer::lagcomp_initialize(
    int64_t p_max_future_action_ticks,
    int64_t p_input_gate_deadline_ticks
) {
    NETW_ERR_COND_V(
        p_max_future_action_ticks < 0 || p_input_gate_deadline_ticks < 0,
        ERR_INVALID_PARAMETER,
        sys::PREDICTION,
        "Lag compensation tick counts cannot be negative, so the settings did "
        "not change."
    );
    const int slot = slot_of(session_decl::KIND_LAGCOMP_CONFIG);
    if (config_consumption[slot].consumed) {
        NETW_WARN(
            sys::PREDICTION,
            "Netw.configure_lagcomp came after the session started, so lag "
            "compensation did not change. Configure it before the session "
            "starts, and finish the chain without await."
        );
        return ERR_ALREADY_IN_USE;
    }
    config_consumption[slot].consumed = true;
    max_future_action_ticks = int(p_max_future_action_ticks);
    input_gate_deadline_ticks = int(p_input_gate_deadline_ticks);
    lagcomp_arm();
    return OK;
}

} // namespace netw
