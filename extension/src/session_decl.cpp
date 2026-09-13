#include "netw/session_decl.hpp"

#include "godot/utility.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace session_decl {

namespace {

struct KindRow {
    const char *callable_meta;
    const char *payload_meta;
    const char *generation_meta;
    const char *verb;
    Form form;
};

const KindRow &row_of(Kind p_kind) {
    static const KindRow table[KIND_COUNT] = {
        {"_netw_d_join",
         "_netw_d_join_x",
         "_netw_d_join_g",
         "configure_join",
         FORM_CALLABLE},
        {"_netw_d_auth",
         "_netw_d_auth_x",
         "_netw_d_auth_g",
         "configure_auth",
         FORM_CALLABLE},
        {"_netw_d_info",
         "_netw_d_info_x",
         "_netw_d_info_g",
         "configure_server_info",
         FORM_CALLABLE},
        {"_netw_d_req",
         "_netw_d_req_x",
         "_netw_d_req_g",
         "configure_scene_requests",
         FORM_CALLABLE},
        {"_netw_d_sess",
         "_netw_d_sess_x",
         "_netw_d_sess_g",
         "configure_session",
         FORM_CONFIG},
        {"_netw_d_clock",
         "_netw_d_clock_x",
         "_netw_d_clock_g",
         "configure_clock",
         FORM_CONFIG},
        {"_netw_d_lag",
         "_netw_d_lag_x",
         "_netw_d_lag_g",
         "configure_lagcomp",
         FORM_CONFIG},
    };
    return table[int(p_kind)];
}

constexpr const char *META_ARMED = "_netw_d_armed";
constexpr const char *META_LAST_API = "_netw_d_api";
constexpr const char *SIG_TREE_ENTERED = "tree_entered";

LocalVector<ObjectID> &declaring_index() {
    static LocalVector<ObjectID> index;
    return index;
}

uint64_t &next_generation() {
    static uint64_t counter = 0;
    return counter;
}

Node *node_of(ObjectID p_id) {
    return Object::cast_to<Node>(gd::object_of(p_id));
}

bool index_holds(ObjectID p_id) {
    for (const ObjectID &held : declaring_index()) {
        if (held == p_id) {
            return true;
        }
    }
    return false;
}

Callable declared_callable(Node *p_scope, Kind p_kind) {
    const StringName key(row_of(p_kind).callable_meta);
    if (!p_scope->has_meta(key)) {
        return Callable();
    }
    return p_scope->get_meta(key);
}

Variant declared_payload(Node *p_scope, Kind p_kind) {
    const StringName key(row_of(p_kind).payload_meta);
    return p_scope->has_meta(key) ? p_scope->get_meta(key) : Variant();
}

uint64_t declared_generation(Node *p_scope, Kind p_kind) {
    const StringName key(row_of(p_kind).generation_meta);
    if (!p_scope->has_meta(key)) {
        return 0;
    }
    return uint64_t(int64_t(p_scope->get_meta(key)));
}

bool eligible(
    Kind p_kind,
    const Callable &p_callable,
    const Variant &p_payload
) {
    if (row_of(p_kind).form == FORM_CONFIG) {
        return p_payload.get_type() != Variant::NIL;
    }
    return !p_callable.is_null();
}

NetwMultiplayer *last_api_of(Node *p_scope) {
    const StringName key(META_LAST_API);
    if (!p_scope->has_meta(key)) {
        return nullptr;
    }
    const ObjectID held = ObjectID(uint64_t(int64_t(p_scope->get_meta(key))));
    return Object::cast_to<NetwMultiplayer>(gd::object_of(held));
}

void install_now(const Variant &p_held) {
    Node *scope = Object::cast_to<Node>(gd::live_object(p_held));
    if (scope == nullptr) {
        return;
    }
    NetwMultiplayer *api = NetwMultiplayer::of(scope);
    if (api == nullptr) {
        return;
    }
    api->declaration_book().install_from(api, scope);
    api->declarations_changed();
}

void arm(Node *p_scope) {
    const StringName armed(META_ARMED);
    if (!p_scope->has_meta(armed)) {
        p_scope->set_meta(armed, true);
        p_scope->connect(
            StringName(SIG_TREE_ENTERED),
            callable_mp_static(&install_now).bind(Variant(p_scope))
        );
    }
    const ObjectID id = gd::instance_id(p_scope);
    if (!index_holds(id)) {
        declaring_index().push_back(id);
    }
}

bool usable(
    NetwMultiplayer *p_api,
    Kind p_kind,
    ObjectID p_scope,
    const Callable &p_callable,
    const Variant &p_payload
) {
    Node *scope = node_of(p_scope);
    if (scope == nullptr || !scope->is_inside_tree()
        || scope->is_queued_for_deletion()) {
        return false;
    }
    if (NetwMultiplayer::of(scope) != p_api) {
        return false;
    }
    if (row_of(p_kind).form == FORM_CONFIG) {
        return p_payload.get_type() != Variant::NIL;
    }
    if (!p_callable.is_valid()) {
        return false;
    }
    Object *target = p_callable.get_object();
    if (target != nullptr) {
        Node *node = Object::cast_to<Node>(target);
        if (node != nullptr && node->is_queued_for_deletion()) {
            return false;
        }
    }
    return true;
}

String scope_label(ObjectID p_scope) {
    Node *scope = node_of(p_scope);
    if (scope == nullptr) {
        return String("a freed node");
    }
    return scope->is_inside_tree() ? String(scope->get_path())
                                   : String(scope->get_name());
}

void append_sorted(LocalVector<String> &p_labels, const String &p_label) {
    uint32_t at = 0;
    while (at < p_labels.size() && p_labels[at] < p_label) {
        ++at;
    }
    p_labels.insert(at, p_label);
}

String joined(const LocalVector<String> &p_labels) {
    String out;
    for (const String &label : p_labels) {
        if (!out.is_empty()) {
            out += ", ";
        }
        out += label;
    }
    return out;
}

} // namespace

Form form_of(Kind p_kind) {
    return row_of(p_kind).form;
}

const char *verb_of(Kind p_kind) {
    return row_of(p_kind).verb;
}

Error declare(
    Node *p_scope,
    Kind p_kind,
    const Callable &p_callable,
    const Variant &p_payload,
    const char *p_verb
) {
    NETW_ERR_COND_V(
        p_scope == nullptr,
        ERR_INVALID_PARAMETER,
        sys::SESSION,
        "Netw.%s: a scope node is required, because the node names the "
        "multiplayer branch the declaration governs",
        p_verb
    );
    NETW_ERR_COND_V(
        form_of(p_kind) == FORM_CALLABLE && !p_callable.is_null()
            && !p_callable.is_valid(),
        ERR_INVALID_PARAMETER,
        sys::SESSION,
        "Netw.%s: the handler is not callable, so '%s' keeps the declaration "
        "it already had. Pass an ordinary method, or Callable() to clear it.",
        p_verb,
        String(p_scope->get_name())
    );
    const KindRow &keys = row_of(p_kind);
    const uint64_t generation = ++next_generation();
    if (!eligible(p_kind, p_callable, p_payload)) {
        p_scope->remove_meta(StringName(keys.callable_meta));
        p_scope->remove_meta(StringName(keys.payload_meta));
        p_scope->set_meta(
            StringName(keys.generation_meta),
            int64_t(generation)
        );
        if (NetwMultiplayer *api = last_api_of(p_scope)) {
            api->declaration_book()
                .withdraw(gd::instance_id(p_scope), p_kind, generation);
        }
        return OK;
    }
    if (p_callable.is_null()) {
        p_scope->remove_meta(StringName(keys.callable_meta));
    } else {
        p_scope->set_meta(StringName(keys.callable_meta), p_callable);
    }
    if (p_payload.get_type() == Variant::NIL) {
        p_scope->remove_meta(StringName(keys.payload_meta));
    } else {
        p_scope->set_meta(StringName(keys.payload_meta), p_payload);
    }
    p_scope->set_meta(StringName(keys.generation_meta), int64_t(generation));
    arm(p_scope);
    if (p_scope->is_inside_tree()) {
        install_now(Variant(p_scope));
    }
    return OK;
}

NetwMultiplayer *installed_api(Node *p_scope) {
    return p_scope == nullptr ? nullptr : last_api_of(p_scope);
}

Variant payload_on(Node *p_scope, Kind p_kind) {
    return p_scope == nullptr ? Variant() : declared_payload(p_scope, p_kind);
}

void Book::install_from(NetwMultiplayer *p_api, Node *p_scope) {
    if (p_api == nullptr || p_scope == nullptr) {
        return;
    }
    p_scope->set_meta(
        StringName(META_LAST_API),
        int64_t(uint64_t(gd::instance_id(p_api)))
    );
    const ObjectID scope = gd::instance_id(p_scope);
    for (int kind = 0; kind < KIND_COUNT; ++kind) {
        const Callable declared = declared_callable(p_scope, Kind(kind));
        const Variant payload = declared_payload(p_scope, Kind(kind));
        const uint64_t generation = declared_generation(p_scope, Kind(kind));
        LocalVector<Slot> &held = slots[kind];
        int found = -1;
        for (uint32_t at = 0; at < held.size(); ++at) {
            if (held[at].scope == scope) {
                found = int(at);
                break;
            }
        }
        if (!eligible(Kind(kind), declared, payload)) {
            if (found >= 0 && held[uint32_t(found)].generation <= generation) {
                held.remove_at(uint32_t(found));
            }
            continue;
        }
        Slot fresh;
        fresh.scope = scope;
        fresh.callable = declared;
        fresh.payload = payload;
        fresh.generation = generation;
        if (found >= 0) {
            if (held[uint32_t(found)].generation > generation) {
                continue;
            }
            held[uint32_t(found)] = fresh;
        } else {
            held.push_back(fresh);
        }
    }
}

void Book::withdraw(ObjectID p_scope, Kind p_kind, uint64_t p_generation) {
    LocalVector<Slot> &held = slots[int(p_kind)];
    for (uint32_t at = 0; at < held.size(); ++at) {
        if (held[at].scope != p_scope) {
            continue;
        }
        if (held[at].generation > p_generation) {
            return;
        }
        held.remove_at(at);
        return;
    }
}

void Book::reconcile(NetwMultiplayer *p_api) {
    LocalVector<ObjectID> &index = declaring_index();
    LocalVector<ObjectID> kept;
    for (const ObjectID &id : index) {
        Node *scope = node_of(id);
        if (scope == nullptr) {
            continue;
        }
        kept.push_back(id);
        if (!scope->is_inside_tree()) {
            continue;
        }
        if (NetwMultiplayer::of(scope) == p_api) {
            install_from(p_api, scope);
        }
    }
    index = kept;
}

Resolved Book::resolve(NetwMultiplayer *p_api, Kind p_kind) {
    reconcile(p_api);
    Resolved out;
    const LocalVector<Slot> &held = slots[int(p_kind)];
    int live = 0;
    for (const Slot &slot : held) {
        if (!usable(p_api, p_kind, slot.scope, slot.callable, slot.payload)) {
            continue;
        }
        ++live;
        out.callable = slot.callable;
        out.payload = slot.payload;
        out.scope = scope_label(slot.scope);
        out.generation = slot.generation;
    }
    if (live == 1) {
        out.state = READY;
        fault_reported[int(p_kind)] = false;
        return out;
    }
    if (live > 1) {
        out.state = AMBIGUOUS;
        out.callable = Callable();
        out.payload = Variant();
        return out;
    }
    out.callable = Callable();
    out.payload = Variant();
    out.state = held.is_empty() ? ABSENT : UNAVAILABLE;
    return out;
}

bool Book::report_unresolved(
    NetwMultiplayer *p_api,
    Kind p_kind,
    Availability p_state,
    const char *p_action
) {
    if (p_state != UNAVAILABLE && p_state != AMBIGUOUS) {
        return false;
    }
    if (fault_reported[int(p_kind)]) {
        return false;
    }
    fault_reported[int(p_kind)] = true;
    const char *verb = row_of(p_kind).verb;
    const LocalVector<Slot> &held = slots[int(p_kind)];
    LocalVector<String> scopes;
    if (p_state == AMBIGUOUS) {
        for (const Slot &slot : held) {
            if (!usable(
                    p_api,
                    p_kind,
                    slot.scope,
                    slot.callable,
                    slot.payload
                )) {
                continue;
            }
            append_sorted(scopes, scope_label(slot.scope));
        }
        NETW_ERROR(
            sys::SESSION,
            "%s refused: %s is declared by more than one live node (%s), so "
            "this session has no single configuration. Keep one declaration "
            "on the session's branch.",
            p_action,
            verb,
            joined(scopes)
        );
        return true;
    }
    for (const Slot &slot : held) {
        append_sorted(scopes, scope_label(slot.scope));
    }
    NETW_WARN(
        sys::SESSION,
        "%s refused: the configuration declared through Netw.%s by %s is no "
        "longer in this session's branch. Keep its scope node alive until "
        "configuration settles, or declare a replacement on this branch.",
        p_action,
        verb,
        joined(scopes)
    );
    return true;
}

void Book::release() {
    for (int kind = 0; kind < KIND_COUNT; ++kind) {
        slots[kind].clear();
        fault_reported[kind] = false;
    }
}

} // namespace session_decl

} // namespace netw
