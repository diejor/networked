#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/plan.hpp"

namespace netw::repl {

class RetainedLane {
    struct Peer {
        wire::CodeRow held;
        bool has_held = false;
    };

    wire::WirePlan compiled;
    godot::Ref<SchemaRecord> declaration;
    godot::HashMap<int, Peer> peers;

public:
    static RetainedLane open(const wire::WirePlan &p_plan);

    static RetainedLane declare(const godot::Ref<SchemaRecord> &p_schema);

    bool gather(const godot::Array &p_values, wire::CodeRow &r_row) const;

    bool valid() const {
        return compiled.valid();
    }

    const wire::WirePlan &plan() const {
        return compiled;
    }

    bool knows(int p_peer) const;

    uint64_t send(int p_peer, const wire::CodeRow &p_row);

    void retain(const godot::LocalVector<int> &p_recipients);

    void forget(int p_peer);
};

} // namespace netw::repl
