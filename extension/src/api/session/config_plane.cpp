#include "godot/callable.hpp"
#include "godot/utility.hpp"
#include "netw/api/clock_config.hpp"
#include "netw/api/lag_compensation_config.hpp"
#include "netw/api/netw_multiplayer.hpp"
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

} // namespace

bool NetwMultiplayer::config_is_consumed(session_decl::Kind p_kind) const {
    return config_consumption[slot_of(p_kind)].consumed;
}

void NetwMultiplayer::declarations_changed() {
    if (config_settle_queued) {
        return;
    }
    config_settle_queued = true;
    callable_mp(this, &NetwMultiplayer::config_settle).call_deferred();
}

Error NetwMultiplayer::config_readiness(String &r_reason) {
    for (const session_decl::Kind kind : CONFIG_KINDS) {
        if (config_is_consumed(kind)) {
            continue;
        }
        const session_decl::Resolved offered
            = declaration_book().resolve(this, kind);
        if (offered.state == session_decl::ABSENT) {
            if (kind == session_decl::KIND_SESSION_CONFIG
                && session_fallback.is_valid()) {
                session_initialize(session_fallback);
                session_apply_role_constraint();
            }
            continue;
        }
        if (offered.state == session_decl::READY) {
            r_reason = String(session_decl::verb_of(kind));
            return ERR_BUSY;
        }
        declaration_book()
            .report_unresolved(this, kind, offered.state, "this operation");
        r_reason = String(session_decl::verb_of(kind));
        return ERR_UNAVAILABLE;
    }
    return OK;
}

void NetwMultiplayer::config_settle() {
    config_settle_queued = false;

    session_decl::Resolved offered[3];
    for (const session_decl::Kind kind : CONFIG_KINDS) {
        const int slot = slot_of(kind);
        if (config_consumption[slot].consumed) {
            continue;
        }
        offered[slot] = declaration_book().resolve(this, kind);
        if (offered[slot].state == session_decl::UNAVAILABLE
            || offered[slot].state == session_decl::AMBIGUOUS) {
            declaration_book().report_unresolved(
                this,
                kind,
                offered[slot].state,
                "configuration"
            );
            return;
        }
    }

    const int session = slot_of(session_decl::KIND_SESSION_CONFIG);
    if (!config_consumption[session].consumed) {
        if (offered[session].state == session_decl::READY) {
            const Ref<NetwSessionConfig> draft = offered[session].payload;
            if (draft.is_valid()) {
                session_report_discarded_fallback(offered[session].scope);
                session_initialize(draft);
                config_consumption[session].generation
                    = offered[session].generation;
                draft->seal(offered[session].scope);
            }
        } else if (session_fallback.is_valid()) {
            session_initialize(session_fallback);
        }
        session_apply_role_constraint();
    }

    const int clock = slot_of(session_decl::KIND_CLOCK_CONFIG);
    if (!config_consumption[clock].consumed) {
        if (offered[clock].state == session_decl::READY) {
            const Ref<NetwClockConfig> draft = offered[clock].payload;
            if (draft.is_valid()) {
                clock_initialize(draft);
                config_consumption[clock].generation
                    = offered[clock].generation;
                draft->seal(offered[clock].scope);
            }
        } else if (clock_fallback.is_valid()) {
            clock_initialize(clock_fallback);
        }
    }

    const int lagcomp = slot_of(session_decl::KIND_LAGCOMP_CONFIG);
    if (offered[lagcomp].state == session_decl::READY) {
        const Ref<NetwLagCompensationConfig> draft = offered[lagcomp].payload;
        if (draft.is_valid()) {
            lagcomp_initialize(
                draft->get_max_future_action_ticks(),
                draft->get_input_gate_deadline_ticks()
            );
            config_consumption[lagcomp].generation
                = offered[lagcomp].generation;
            draft->seal(offered[lagcomp].scope);
        }
    }

    if (predict_awaiting_config.is_empty()) {
        return;
    }
    if (!config_is_consumed(session_decl::KIND_LAGCOMP_CONFIG)) {
        config_consume_lagcomp_defaults();
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
}

void NetwMultiplayer::session_report_discarded_fallback(const String &p_scope) {
    if (session_fallback.is_null()) {
        return;
    }
    const String lost = session_fallback->non_default_fields();
    if (lost.is_empty()) {
        return;
    }
    Node *source
        = Object::cast_to<Node>(gd::object_of(session_fallback_source));
    NETW_WARN(
        sys::SESSION,
        "Netw.configure_session: the declaration on '%s' replaces the whole "
        "session configuration exported by '%s'. These non-default tree "
        "fields are ignored: %s. Put all session settings in the declaration.",
        p_scope,
        source != nullptr ? String(source->get_name()) : String("a freed node"),
        lost
    );
}

void NetwMultiplayer::config_consume_lagcomp_defaults() {
    lagcomp_initialize(max_future_action_ticks, input_gate_deadline_ticks);
}

Error NetwMultiplayer::lagcomp_initialize(
    int64_t p_max_future_action_ticks,
    int64_t p_input_gate_deadline_ticks
) {
    NETW_ERR_COND_V(
        p_max_future_action_ticks < 0 || p_input_gate_deadline_ticks < 0,
        ERR_INVALID_PARAMETER,
        sys::PREDICTION,
        "lag compensation refused a negative tick count, so the session keeps "
        "the values it already had"
    );
    const int slot = slot_of(session_decl::KIND_LAGCOMP_CONFIG);
    if (config_consumption[slot].consumed) {
        NETW_WARN(
            sys::PREDICTION,
            "Netw.configure_lagcomp: 'lag compensation' on '%s' was "
            "configured after this session consumed its configuration. The "
            "running values are unchanged. Configure before startup and "
            "finish the fluent chain without awaiting.",
            String("this session")
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
