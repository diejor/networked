#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwMultiplayer;

namespace session_decl {

enum Kind {
    KIND_JOIN = 0,
    KIND_ADMISSION = 1,
    KIND_SERVER_INFO = 2,
    KIND_SESSION_CONFIG = 3,
    KIND_CLOCK_CONFIG = 4,
    KIND_LAGCOMP_CONFIG = 5,
    KIND_TRANSFER = 6,
    KIND_COUNT = 7,
};

enum Form {
    FORM_CALLABLE,
    FORM_CONFIG,
};

Form form_of(Kind p_kind);
const char *verb_of(Kind p_kind);

enum Availability {
    ABSENT,
    READY,
    UNAVAILABLE,
    AMBIGUOUS,
};

struct Resolved {
    Availability state = ABSENT;
    godot::Callable callable;
    godot::Variant payload;
    godot::String scope;
    uint64_t generation = 0;
};

godot::Error declare(
    godot::Node *p_scope,
    Kind p_kind,
    const godot::Callable &p_callable,
    const godot::Variant &p_payload,
    const char *p_verb
);

godot::Variant payload_on(godot::Node *p_scope, Kind p_kind);

godot::Error declare_database(
    godot::Node *p_scope,
    const godot::StringName &p_name,
    const godot::Variant &p_config
);

godot::Dictionary databases_on(godot::Node *p_scope);

class Book {
public:
    Resolved resolve(NetwMultiplayer *p_api, Kind p_kind);
    void refresh(NetwMultiplayer *p_api);
    bool report_unresolved(
        NetwMultiplayer *p_api,
        Kind p_kind,
        Availability p_state,
        const char *p_action
    );
    void install_from(NetwMultiplayer *p_api, godot::Node *p_scope);
    void withdraw(godot::ObjectID p_scope, Kind p_kind, uint64_t p_generation);
    void release();

private:
    struct Slot {
        godot::ObjectID scope;
        godot::Callable callable;
        godot::Variant payload;
        uint64_t generation = 0;
    };

    void reconcile(NetwMultiplayer *p_api);

    godot::LocalVector<Slot> slots[KIND_COUNT];
    bool fault_reported[KIND_COUNT] = {};
};

} // namespace session_decl

} // namespace netw
