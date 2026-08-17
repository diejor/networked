#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/entity_control.hpp"

namespace netw::repl {

enum SetKind { SET_CONSUMED = 0, SET_DERIVED = 1 };

enum SetRecord {
    SET_RECORD_NONE = 0,
    SET_RECORD_STATE = 1,
    SET_RECORD_INPUT = 2,
    SET_RECORD_BROADCAST = 3
};

enum SetAudience { SET_AUDIENCE_PUBLIC = 0, SET_AUDIENCE_SERVER_ONLY = 1 };

struct SetRow {
    int64_t route = 0;
    int64_t ordinal = 0;
    int64_t comp = 0;
    int64_t kind = SET_CONSUMED;
    godot::StringName key;
    godot::RID set;
    int64_t record = SET_RECORD_NONE;
    int64_t schema_hash = 0;
    int64_t policy = int64_t(WritePolicy::AUTHORITY);
    int64_t audience = SET_AUDIENCE_PUBLIC;
};

bool row_authors(
    const SetRow &p_row,
    int64_t p_local_id,
    bool p_node_authority,
    int64_t p_controller
);

godot::PackedInt32Array row_recipients(
    const SetRow &p_row,
    int64_t p_local_id,
    const godot::PackedInt32Array &p_live
);

godot::PackedInt32Array event_recipients(
    bool p_is_host,
    int64_t p_local_id,
    int64_t p_exclude,
    const godot::PackedInt32Array &p_live
);

bool row_admits_sender(
    const SetRow &p_row,
    int64_t p_sender,
    int64_t p_node_authority,
    int64_t p_controller
);

class SetModel {
    godot::HashMap<int64_t, godot::LocalVector<SetRow>> routes;
    godot::HashMap<int64_t, godot::HashMap<int64_t, int64_t>> descriptors;

    static void reindex(godot::LocalVector<SetRow> &r_rows);

public:
    int64_t declare(
        int64_t p_route,
        int64_t p_kind,
        const godot::StringName &p_key,
        int64_t p_comp,
        const godot::RID &p_set,
        int64_t p_record,
        int64_t p_schema_hash,
        int64_t p_policy = int64_t(WritePolicy::AUTHORITY),
        int64_t p_audience = SET_AUDIENCE_PUBLIC
    );

    void drop(
        int64_t p_route,
        int64_t p_kind,
        const godot::StringName &p_key,
        int64_t p_record
    );

    const SetRow *row(int64_t p_route, int64_t p_ordinal) const;

    const SetRow *row_for(
        int64_t p_route,
        int64_t p_kind,
        const godot::StringName &p_key,
        int64_t p_record
    ) const;

    const godot::LocalVector<SetRow> *route_rows(int64_t p_route) const;

    void note_descriptors(
        int64_t p_route,
        const godot::HashMap<int64_t, int64_t> &p_descriptors
    );

    bool admits_schema(int64_t p_route, int64_t p_ordinal) const;

    void clear_route(int64_t p_route);
    void clear();
};

} // namespace netw::repl
