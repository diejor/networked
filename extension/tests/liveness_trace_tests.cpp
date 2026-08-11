// The record plane against the trace recorded from the GDScript arm it
// replaces.
//
// Liveness has no seam and no byte grammar, so nothing here can be certified
// the way a codec can. What it has instead is a lifecycle trace: scenarios
// scripted through the GDScript record plane and committed BEFORE any of it was
// native, so the native arm has something to reproduce other than itself.
//
// The scenarios below are the golden's `record/` half, re-driven against the
// core directly rather than through the interface that wraps it. That is the
// half that outlives the arm: the `shell/` half drives wrappers and nodes and
// stays GDScript, and the arm keeps checking it until the shell itself goes.
//
// One antecedent is pinned rather than reproduced. The recording ran with no
// clock configured, so every wait there aged against the frame counter, and
// these scenarios arm on the frame counter for the same reason. A clock-armed
// wait is a law of its own in liveness_core_tests.cpp.
//
// The golden lives in the project, so this file runs in the tier that can read
// res:// and carries no [Hosted] tag.

#include "support/netw_test.h"

#include "godot/file_access.hpp"
#include "netw/liveness_core.hpp"
#include "support/netw_call_log.h"

namespace TestNetwLivenessTrace {

using namespace godot;
using netw::NetwLivenessCore;
using netw_test::CallLog;

constexpr const char *GOLDEN = "res://tests/native/goldens/liveness_lifecycle.txt";

// What the interface above this one falls back to when no clock has registered,
// and therefore what the recording's default timeouts were measured in.
constexpr int CLOCKLESS_TICKRATE = 30;

String state_name(NetwLivenessCore::State p_state) {
    switch (p_state) {
        case NetwLivenessCore::STATE_UNKNOWN:
            return "UNKNOWN";
        case NetwLivenessCore::STATE_LIVE:
            return "LIVE";
        case NetwLivenessCore::STATE_LINGERING:
            return "LINGERING";
        case NetwLivenessCore::STATE_DEAD:
            return "DEAD";
    }
    return "INVALID";
}

String routes_text(const PackedInt32Array &p_routes) {
    String out = "[";
    for (int index = 0; index < p_routes.size(); ++index) {
        out += (index > 0 ? "," : "") + itos(p_routes[index]);
    }
    return out + "]";
}

String routes_text(const PackedInt64Array &p_routes) {
    String out = "[";
    for (int index = 0; index < p_routes.size(); ++index) {
        out += (index > 0 ? "," : "") + itos(p_routes[index]);
    }
    return out + "]";
}

// One row's fields, sorted by key when they are written out, so the order a
// case happens to name them in can never move a row.
struct Fields {
    Vector<String> keys;
    Vector<String> values;

    Fields &put(const String &p_key, const String &p_value) {
        keys.push_back(p_key);
        values.push_back(p_value);
        return *this;
    }

    Fields &put(const String &p_key, int p_value) {
        return put(p_key, itos(p_value));
    }

    String text() const {
        Vector<int> order;
        for (int index = 0; index < keys.size(); ++index) {
            int at = 0;
            while (at < order.size() && keys[order[at]] < keys[index]) {
                at += 1;
            }
            order.insert(at, index);
        }
        String out;
        for (int index = 0; index < order.size(); ++index) {
            out += (index > 0 ? " " : "") + keys[order[index]] + "="
                + values[order[index]];
        }
        return out;
    }
};

// The trace under construction. Rows and callback arrivals share one list,
// because when a callback ran relative to what was observed around it is the
// whole content of a waiting-room contract.
struct Trace {
    CallLog log;
    String scenario;

    void row(const String &p_kind, const Fields &p_fields) const {
        log.note(scenario + String("|") + p_kind + String("|") + p_fields.text());
    }

    Callable callback(const String &p_kind, const Fields &p_fields) const {
        return log.callable(
            scenario + String("|") + p_kind + String("|") + p_fields.text()
        );
    }

    // The pair of readings every transition is judged by.
    void state(
        const Ref<NetwLivenessCore> &p_core,
        const String &p_at,
        int p_route,
        const RID &p_entity
    ) const {
        row("state",
            Fields()
                .put("at", p_at)
                .put("entity", state_name(p_core->state_of(p_entity)))
                .put("route", state_name(p_core->route_state(p_route))));
    }
};

Ref<NetwLivenessCore> fresh() {
    Ref<NetwLivenessCore> core;
    core.instantiate();
    return core;
}

PackedInt64Array one(int p_route) {
    PackedInt64Array routes;
    routes.push_back(p_route);
    return routes;
}

void mint_bind_tombstone(const Trace &t) {
    Ref<NetwLivenessCore> core = fresh();
    const RID entity = core->entity_create();
    const int route = core->reserve_route();
    t.state(core, "minted", route, entity);

    core->bind_route(entity, route);
    t.state(core, "bound", route, entity);
    t.row("route_of", Fields().put("route", core->route_of(entity)));
    t.row("live_routes", Fields().put("routes", routes_text(core->live_routes())));

    core->tombstone_routes_data(one(route));
    t.state(core, "tombstoned", route, entity);
    t.row("live_routes", Fields().put("routes", routes_text(core->live_routes())));

    t.row(
        "unreserved",
        Fields().put("state", state_name(core->route_state(route + 1000)))
    );
}

void bulk_claim_and_release(const Trace &t) {
    Ref<NetwLivenessCore> core = fresh();
    PackedInt64Array routes;
    for (int index = 0; index < 3; ++index) {
        routes.push_back(core->reserve_route());
    }
    core->bind_routes_data(routes);

    t.row("claimed", Fields().put("routes", routes_text(routes)));
    t.row("live_routes", Fields().put("routes", routes_text(core->live_routes())));
    for (int index = 0; index < routes.size(); ++index) {
        t.row(
            "state",
            Fields()
                .put("route", int(routes[index]))
                .put("state", state_name(core->route_state(int(routes[index]))))
        );
    }

    core->tombstone_routes_data(one(int(routes[1])));
    t.row("live_routes", Fields().put("routes", routes_text(core->live_routes())));
    for (int index = 0; index < routes.size(); ++index) {
        t.row(
            "state",
            Fields()
                .put("route", int(routes[index]))
                .put("state", state_name(core->route_state(int(routes[index]))))
        );
    }
}

void revival_is_a_new_epoch(const Trace &t) {
    Ref<NetwLivenessCore> core = fresh();
    const RID first = core->entity_create();
    const int route = core->reserve_route();
    core->bind_route(first, route);
    core->tombstone_routes_data(one(route));
    t.state(core, "first_dead", route, first);

    const RID second = core->entity_create();
    core->bind_route(second, route);
    t.state(core, "second_live", route, second);
    t.row(
        "identity",
        Fields()
            .put("first_still_dead", state_name(core->state_of(first)))
            .put(
                "route_holds_second",
                core->rid_from_route(route) == second ? "true" : "false"
            )
    );
}

void pending_live_flush(const Trace &t) {
    Ref<NetwLivenessCore> core = fresh();
    const int route = core->reserve_route() + 1;

    core->when_live(
        route,
        t.callback("cb", Fields().put("route", route)),
        core->frame() + CLOCKLESS_TICKRATE,
        false,
        Callable()
    );
    t.row("pending", Fields().put("count", core->pending_live_count()));

    PackedInt64Array claimed = one(core->reserve_route());
    core->bind_routes_data(claimed);
    t.row("claimed", Fields().put("routes", routes_text(claimed)));
    t.row("pending", Fields().put("count", core->pending_live_count()));

    core->bind_routes_data(one(route));
    t.row("pending", Fields().put("count", core->pending_live_count()));
}

void pending_live_timeout(const Trace &t) {
    Ref<NetwLivenessCore> core = fresh();
    const int route = core->reserve_route() + 5;
    t.row("clock", Fields().put("configured", "false"));

    core->when_live(
        route,
        t.callback("cb", Fields().put("route", route)),
        core->frame() + 3,
        false,
        t.callback("timeout", Fields().put("route", route))
    );
    for (int step = 0; step < 4; ++step) {
        core->poll(0);
        t.row(
            "polled",
            Fields().put("pending", core->pending_live_count()).put("step", step)
        );
    }
}

void pending_live_two_deadlines(const Trace &t) {
    Ref<NetwLivenessCore> core = fresh();
    const int route = core->reserve_route() + 5;

    core->when_live(
        route,
        t.callback("cb", Fields().put("deadline", 2)),
        core->frame() + 2,
        false,
        t.callback("timeout", Fields().put("deadline", 2))
    );
    core->when_live(
        route,
        t.callback("cb", Fields().put("deadline", 4)),
        core->frame() + 4,
        false,
        t.callback("timeout", Fields().put("deadline", 4))
    );
    for (int step = 0; step < 5; ++step) {
        core->poll(0);
        t.row(
            "polled",
            Fields().put("pending", core->pending_live_count()).put("step", step)
        );
    }
}

void run(const Trace &t) {
    if (t.scenario == String("record/mint_bind_tombstone")) {
        mint_bind_tombstone(t);
    } else if (t.scenario == String("record/bulk_claim_and_release")) {
        bulk_claim_and_release(t);
    } else if (t.scenario == String("record/revival_is_a_new_epoch")) {
        revival_is_a_new_epoch(t);
    } else if (t.scenario == String("record/pending_live_flush")) {
        pending_live_flush(t);
    } else if (t.scenario == String("record/pending_live_timeout")) {
        pending_live_timeout(t);
    } else if (t.scenario == String("record/pending_live_two_deadlines")) {
        pending_live_two_deadlines(t);
    } else {
        FAIL("unknown scenario in the golden");
    }
}

Vector<String> golden_rows() {
    Vector<String> rows;
    Ref<FileAccess> file = FileAccess::open(GOLDEN, FileAccess::READ);
    REQUIRE(file.is_valid());
    if (file.is_null()) {
        return rows;
    }
    while (!file->eof_reached()) {
        const String line = file->get_line().strip_edges();
        if (line.is_empty() || line.begins_with("#")) {
            continue;
        }
        // The wrapper plane's rows are the arm's to check, not this tier's.
        if (line.begins_with("record/")) {
            rows.push_back(line);
        }
    }
    file->close();
    return rows;
}

TEST_CASE(
    "[Networked][Liveness] the native record plane reproduces the recorded "
    "lifecycle trace"
) {
    const Vector<String> expected = golden_rows();
    // A golden nobody read is a golden that passes.
    REQUIRE(expected.size() > 0);

    Vector<String> actual;
    String scenario;
    for (int index = 0; index < expected.size(); ++index) {
        const String next = expected[index].get_slice("|", 0);
        if (next == scenario) {
            continue;
        }
        scenario = next;

        Trace trace;
        trace.scenario = scenario;
        run(trace);
        const Vector<StringName> produced = trace.log.order();
        for (int row = 0; row < produced.size(); ++row) {
            actual.push_back(String(produced[row]));
        }
    }

    NETW_CHECK_EQ(actual.size(), expected.size());
    for (int index = 0; index < expected.size(); ++index) {
        const String produced
            = index < actual.size() ? actual[index] : String("<missing>");
        // Captured through a char array rather than a pointer, and compared as
        // one bool rather than two operands. Either shortcut hands this tier's
        // printer something it dies on, and it dies only on the first FAILING
        // row, which is the one row anybody needed to read.
        NETW_FORMAT_TEXT(want, expected[index].utf8().get_data());
        NETW_FORMAT_TEXT(got, produced.utf8().get_data());
        CAPTURE(want);
        CAPTURE(got);
        CHECK(bool(produced == expected[index]));
    }
}

} // namespace TestNetwLivenessTrace
