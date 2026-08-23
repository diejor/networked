#include "support/netw_test.h"

#include "godot/file_access.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/session_core.hpp"
#include "support/netw_recorder.h"

namespace TestNetwSessionTrace {

using namespace godot;
using netw::NetwMultiplayerCore;
using netw::SessionCore;
using netw_test::Recorder;

constexpr const char *GOLDEN
    = "res://tests/native/goldens/session_admission.txt";

constexpr int64_t NOW = 1'000'000;

String state_name(int p_state) {
    switch (p_state) {
        case SessionCore::STATE_OFFLINE:
            return "OFFLINE";
        case SessionCore::STATE_CONNECTING:
            return "CONNECTING";
        case SessionCore::STATE_ONLINE:
            return "ONLINE";
        case SessionCore::STATE_DISCONNECTING:
            return "DISCONNECTING";
    }
    return "INVALID";
}

String role_name(int p_role) {
    switch (p_role) {
        case SessionCore::ROLE_NONE:
            return "NONE";
        case SessionCore::ROLE_CLIENT:
            return "CLIENT";
        case SessionCore::ROLE_DEDICATED_SERVER:
            return "DEDICATED_SERVER";
        case SessionCore::ROLE_LISTEN_SERVER:
            return "LISTEN_SERVER";
    }
    return "INVALID";
}

struct Fields {
    Vector<String> keys;
    Vector<String> values;

    Fields &put(const String &p_key, const String &p_value) {
        keys.push_back(p_key);
        values.push_back(p_value);
        return *this;
    }

    Fields &put(const String &p_key, int64_t p_value) {
        return put(p_key, itos(p_value));
    }

    Fields &put(const String &p_key, bool p_value) {
        return put(p_key, String(p_value ? "true" : "false"));
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

struct Trace {
    Vector<String> rows;
    String scenario;

    void row(const String &p_kind, const Fields &p_fields) {
        rows.push_back(
            scenario + String("|") + p_kind + String("|") + p_fields.text()
        );
    }

    void state(const SessionCore &p_core, const String &p_at) {
        row("state",
            Fields()
                .put("at", p_at)
                .put("role", role_name(p_core.get_role()))
                .put("state", state_name(p_core.get_state())));
    }

    void signals(Recorder &p_recorder) {
        String order = "[";
        int changes = 0;
        const Vector<StringName> seen = p_recorder.order();
        for (int index = 0; index < seen.size(); ++index) {
            if (index > 0) {
                order += ",";
            }
            if (seen[index] != StringName("state_changed")) {
                order += String(seen[index]);
                continue;
            }
            const Array args = p_recorder.args("state_changed", changes);
            changes += 1;
            order += "state_changed(" + state_name(int(args[0])) + "->"
                + state_name(int(args[1])) + ")";
        }
        row("signals", Fields().put("order", order + "]"));
        p_recorder.clear();
    }
};

SessionCore fresh() {
    return SessionCore();
}

void every_legal_edge_runs_its_hooks(Trace &t) {
    Ref<NetwMultiplayerCore> host;
    host.instantiate();
    SessionCore &core = host->session_plane();
    Recorder recorder(
        host.ptr(),
        {"state_changed", "session_entered", "session_ended"}
    );

    const SessionCore::State walk[] = {
        SessionCore::STATE_CONNECTING,
        SessionCore::STATE_OFFLINE,
        SessionCore::STATE_CONNECTING,
        SessionCore::STATE_ONLINE,
        SessionCore::STATE_DISCONNECTING,
        SessionCore::STATE_OFFLINE,
    };
    for (const SessionCore::State next : walk) {
        core.transition(next);
        t.state(core, "to_" + state_name(next));
        t.signals(recorder);
    }

    core.transition(SessionCore::STATE_OFFLINE);
    t.state(core, "repeated");
    t.signals(recorder);
}

void an_honest_peer_stays_under_the_window(Trace &t) {
    SessionCore core = fresh();
    for (int attempt = 0; attempt < 2; ++attempt) {
        t.row(
            "submit",
            Fields()
                .put("attempt", int64_t(attempt))
                .put("flooded", core.join_flooded(7, NOW))
        );
    }
}

void a_flood_trips_at_the_limit(Trace &t) {
    SessionCore core = fresh();
    int tripped_at = -1;
    for (int attempt = 0; attempt < 20; ++attempt) {
        if (core.join_flooded(7, NOW)) {
            tripped_at = attempt;
            break;
        }
    }
    t.row("trip", Fields().put("at", int64_t(tripped_at)));
}

void the_host_self_join_is_never_limited(Trace &t) {
    SessionCore core = fresh();
    bool flooded = false;
    for (int attempt = 0; attempt < 40; ++attempt) {
        flooded = flooded || core.join_flooded(1, NOW);
    }
    t.row("host", Fields().put("flooded_once", flooded));
}

void each_peer_draws_its_own_budget(Trace &t) {
    SessionCore core = fresh();
    for (int attempt = 0; attempt < 20; ++attempt) {
        core.join_flooded(7, NOW);
    }
    t.row("neighbour", Fields().put("flooded", core.join_flooded(8, NOW)));
}

void the_fold_is_the_compatibility_gate(Trace &t) {
    const char *tags[] = {
        "",
        "networked",
        "networkee",
        "networked-demo-build-2026-08-06-with-a-long-tail",
        "bomber",
        "quick_start",
        "éé",
        "netw/1.0.0",
        "netw/1.0.1",
    };
    for (const char *tag : tags) {
        const String text = String::utf8(tag);
        t.row(
            "fold",
            Fields()
                .put("length", int64_t(text.length()))
                .put("tag", SessionCore::compute_app_tag(StringName(text)))
        );
    }
}

void run(Trace &t) {
    if (t.scenario == "edges/every_legal_edge_runs_its_hooks") {
        every_legal_edge_runs_its_hooks(t);
    } else if (t.scenario == "flood/an_honest_peer_stays_under_the_window") {
        an_honest_peer_stays_under_the_window(t);
    } else if (t.scenario == "flood/a_flood_trips_at_the_limit") {
        a_flood_trips_at_the_limit(t);
    } else if (t.scenario == "flood/the_host_self_join_is_never_limited") {
        the_host_self_join_is_never_limited(t);
    } else if (t.scenario == "flood/each_peer_draws_its_own_budget") {
        each_peer_draws_its_own_budget(t);
    } else if (t.scenario == "apptag/the_fold_is_the_compatibility_gate") {
        the_fold_is_the_compatibility_gate(t);
    } else {
        FAIL("unknown scenario in the golden");
    }
}

Vector<String> golden_rows(const String &p_family) {
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
        if (line.begins_with(p_family)) {
            rows.push_back(line);
        }
    }
    file->close();
    return rows;
}

void replay(const String &p_family) {
    const Vector<String> expected = golden_rows(p_family);
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
        for (int row = 0; row < trace.rows.size(); ++row) {
            actual.push_back(trace.rows[row]);
        }
    }

    NETW_CHECK_EQ(actual.size(), expected.size());
    for (int index = 0; index < expected.size(); ++index) {
        const String produced
            = index < actual.size() ? actual[index] : String("<missing>");
        NETW_FORMAT_TEXT(want, expected[index].utf8().get_data());
        NETW_FORMAT_TEXT(got, produced.utf8().get_data());
        CAPTURE(want);
        CAPTURE(got);
        CHECK(bool(produced == expected[index]));
    }
}

TEST_CASE("[Networked][Session] T1 the machine reproduces the recorded edges") {
    replay("edges/every_legal_edge_runs_its_hooks");
}

TEST_CASE(
    "[Networked][Session] T2 an honest peer reproduces its recorded submissions"
) {
    replay("flood/an_honest_peer_stays_under_the_window");
}

TEST_CASE(
    "[Networked][Session] T3 a flood trips where it was recorded tripping"
) {
    replay("flood/a_flood_trips_at_the_limit");
}

TEST_CASE("[Networked][Session] T4 the host self-join stays unlimited") {
    replay("flood/the_host_self_join_is_never_limited");
}

TEST_CASE("[Networked][Session] T5 each peer draws its own recorded budget") {
    replay("flood/each_peer_draws_its_own_budget");
}

TEST_CASE("[Networked][Session] T6 the recorded app tags fold the same way") {
    replay("apptag/the_fold_is_the_compatibility_gate");
}

} // namespace TestNetwSessionTrace
