#include "support/fault_log.h"
#include "support/netw_test.h"

#include <vector>

#include "godot/variant.hpp"
#include "netw/api/connection.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/promise.hpp"
#include "netw/connect/core.hpp"
#include "netw/connect/transport.hpp"

namespace TestNetwConnectAttempt {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwPromise;
using netw::connect::TransportBook;

namespace {

const char *PEER_CLASS = "AttemptTestPeer";

struct AttemptLog {
    double hint = 5.0;
    int built = 0;
    int cancelled = 0;
    Dictionary settings;
    std::vector<int64_t> states;
    int failed = 0;
    int64_t failed_error = OK;
    int progressed = 0;
    String progress_message;
    double progress_ratio = -1.0;
};

AttemptLog &attempt_log() {
    static AttemptLog log;
    return log;
}

class SilentTransport : public netw::connect::Transport {
public:
    StringName peer_class() const override {
        return StringName(PEER_CLASS);
    }
    String display_name() const override {
        return String("silent");
    }
    double timeout_hint() const override {
        return attempt_log().hint;
    }
    Dictionary client_settings() const override {
        Dictionary built;
        built["a"] = String("built-in");
        built["b"] = String("built-in");
        built["c"] = String("built-in");
        built["d"] = String("built-in");
        return built;
    }
    void make_peer(int, const String &, const Dictionary &p_settings) override {
        attempt_log().built += 1;
        attempt_log().settings = p_settings;
    }
    void cancel_peer_creation() override {
        attempt_log().cancelled += 1;
    }
};

netw::connect::Transport *make_silent() {
    return new SilentTransport();
}

void record_state(int64_t p_state) {
    attempt_log().states.push_back(p_state);
}

void record_failed(int64_t p_error, String) {
    attempt_log().failed += 1;
    attempt_log().failed_error = p_error;
}

void record_progress(String p_message, double p_ratio) {
    attempt_log().progressed += 1;
    attempt_log().progress_message = p_message;
    attempt_log().progress_ratio = p_ratio;
}

struct Bench {
    Ref<NetwMultiplayer> core;

    Bench() {
        attempt_log() = AttemptLog();
        TransportBook::shared().install_native(
            StringName(PEER_CLASS),
            make_silent,
            String("silent")
        );
        core.instantiate();
        core->observe(
            NetwMultiplayer::CONNECTION_STATE_CHANGED,
            callable_mp_static(&record_state)
        );
        core->observe(
            NetwMultiplayer::CONNECTION_FAILED,
            callable_mp_static(&record_failed)
        );
        core->observe(
            NetwMultiplayer::CONNECTION_PROGRESSED,
            callable_mp_static(&record_progress)
        );
    }

    ~Bench() {
        core->embed_dispose();
        TransportBook::shared().forget(StringName(PEER_CLASS));
        attempt_log() = AttemptLog();
    }

    RID transport() {
        return core->transport_find(StringName(PEER_CLASS));
    }

    Ref<NetwPromise> join() {
        return core->transport_join(
            transport(),
            String("nowhere.example"),
            Dictionary()
        );
    }

    void poll(double p_seconds) {
        core->connect_plane().on_poll(p_seconds);
    }
};

} // namespace

TEST_CASE(
    "[Networked][Connect][Hosted] AT3 an attempt whose transport never "
    "answers fails once with ERR_TIMEOUT at the transport's timeout hint and "
    "cancels the creation it was waiting on"
) {
    Bench bench;
    attempt_log().hint = 0.2;
    const Ref<NetwPromise> outcome = bench.join();
    bench.poll(0.0);
    NETW_REQUIRE_EQ(attempt_log().built, 1);

    bench.poll(0.1);
    NETW_CHECK_EQ(attempt_log().failed, 0);
    bench.poll(0.15);

    NETW_CHECK_EQ(attempt_log().failed, 1);
    NETW_CHECK_EQ(attempt_log().failed_error, int64_t(ERR_TIMEOUT));
    NETW_CHECK_EQ(attempt_log().cancelled, 1);
    NETW_CHECK_EQ(
        bench.core->transport_get_attempt_state(),
        int64_t(netw::connect::ATTEMPT_OFFLINE)
    );
    CHECK(outcome->get_is_failed());
    NETW_CHECK_EQ(int64_t(outcome->get_code()), int64_t(ERR_TIMEOUT));

    bench.poll(1.0);
    NETW_CHECK_EQ(attempt_log().failed, 1);
}

TEST_CASE(
    "[Networked][Connect][Hosted] AT4 cancelling an attempt while it creates "
    "goes offline and settles the promise with ERR_SKIP without reporting a "
    "failure"
) {
    Bench bench;
    const Ref<NetwPromise> outcome = bench.join();
    NETW_CHECK_EQ(
        bench.core->transport_get_attempt_state(),
        int64_t(netw::connect::ATTEMPT_CREATING)
    );

    bench.core->transport_cancel_attempt();

    NETW_CHECK_EQ(attempt_log().failed, 0);
    NETW_REQUIRE_EQ(int(attempt_log().states.size()), 2);
    NETW_CHECK_EQ(
        attempt_log().states[0],
        int64_t(netw::connect::ATTEMPT_CREATING)
    );
    NETW_CHECK_EQ(
        attempt_log().states[1],
        int64_t(netw::connect::ATTEMPT_OFFLINE)
    );
    CHECK(outcome->get_is_failed());
    NETW_CHECK_EQ(int64_t(outcome->get_code()), int64_t(ERR_SKIP));

    bench.poll(10.0);
    NETW_CHECK_EQ(attempt_log().failed, 0);
    NETW_CHECK_EQ(attempt_log().built, 0);
}

TEST_CASE(
    "[Networked][Connect][Hosted] AT7 joining while the session is online "
    "settles ERR_ALREADY_IN_USE and leaves the attempt and the peer as they "
    "were"
) {
    Bench bench;
    Ref<netw::LocalMultiplayerPeer> hosting;
    hosting.instantiate();
    hosting->create_server();
    bench.core->NETW_API_VIRTUAL(set_multiplayer_peer)(hosting);
    REQUIRE(bench.core->is_online());
    attempt_log().states.clear();

    const Ref<NetwPromise> outcome = bench.join();
    bench.poll(0.0);

    CHECK(outcome->get_is_failed());
    NETW_CHECK_EQ(int64_t(outcome->get_code()), int64_t(ERR_ALREADY_IN_USE));
    NETW_CHECK_EQ(int(attempt_log().states.size()), 0);
    NETW_CHECK_EQ(attempt_log().failed, 0);
    NETW_CHECK_EQ(attempt_log().built, 0);
    NETW_CHECK_EQ(
        bench.core->transport_get_attempt_state(),
        int64_t(netw::connect::ATTEMPT_ONLINE)
    );
    const bool kept = bench.core->get_multiplayer_peer() == hosting;
    CHECK(kept);
    bench.core->NETW_API_VIRTUAL(set_multiplayer_peer)(Ref<MultiplayerPeer>());
}

TEST_CASE(
    "[Networked][Connect][Hosted] AT8 a listed row joins with the transport's "
    "own settings, under its defaults, under the row's metadata, under the "
    "settings the caller passed"
) {
    Bench bench;
    const RID row = bench.core->endpoint_add(
        bench.transport(),
        String("listed.example"),
        String()
    );
    REQUIRE(row.is_valid());
    Dictionary metadata;
    metadata["c"] = String("metadata");
    metadata["d"] = String("metadata");
    bench.core->connect_plane().list().row(row)->metadata = metadata;
    Dictionary preset;
    preset["b"] = String("defaults");
    preset["c"] = String("defaults");
    preset["d"] = String("defaults");
    NETW_REQUIRE_EQ(
        int64_t(bench.core->transport_set_defaults(bench.transport(), preset)),
        int64_t(OK)
    );
    Dictionary passed;
    passed["d"] = String("call");

    bench.core->endpoint_join(row, passed);
    bench.poll(0.0);

    NETW_REQUIRE_EQ(attempt_log().built, 1);
    const Dictionary &got = attempt_log().settings;
    CHECK(String(got.get("a", String())) == String("built-in"));
    CHECK(String(got.get("b", String())) == String("defaults"));
    CHECK(String(got.get("c", String())) == String("metadata"));
    CHECK(String(got.get("d", String())) == String("call"));

    const Dictionary read_back
        = bench.core->transport_get_defaults(bench.transport());
    CHECK(String(read_back.get("b", String())) == String("defaults"));
}

TEST_CASE(
    "[Networked][Connect][Hosted] AT9 a transport that reports nothing still "
    "progresses by the time spent over its timeout hint"
) {
    Bench bench;
    attempt_log().hint = 1.0;
    bench.join();
    bench.poll(0.0);
    NETW_CHECK_EQ(attempt_log().progressed, 0);

    bench.poll(0.25);

    NETW_CHECK_EQ(attempt_log().progressed, 1);
    const bool quarter = attempt_log().progress_ratio == 0.25;
    CHECK(quarter);
    CHECK(attempt_log().progress_message.is_empty());
}

int heard_on_signal = 0;

void hear_failed(int64_t, String) {
    heard_on_signal += 1;
}

int64_t unhandled_after_timed_out_join(Bench &p_bench) {
    const netw_test::FaultWatch faults;
    attempt_log().hint = 0.2;
    p_bench.join();
    p_bench.poll(0.0);
    p_bench.poll(0.25);
    return faults.warnings_containing("nothing handled it");
}

TEST_CASE(
    "[Networked][Connect][Hosted] AT10 a failed join whose promise is dropped "
    "warns "
    "only when nothing heard the failure"
) {
    {
        Bench bench;
        bench.core->unobserve(
            NetwMultiplayer::CONNECTION_FAILED,
            callable_mp_static(&record_failed)
        );
        heard_on_signal = 0;
        bench.core->get_connection()->connect(
            StringName("failed"),
            callable_mp_static(&hear_failed)
        );
        NETW_CHECK_EQ(unhandled_after_timed_out_join(bench), 0);
        NETW_CHECK_EQ(heard_on_signal, 1);
    }
    {
        Bench bench;
        NETW_CHECK_EQ(unhandled_after_timed_out_join(bench), 0);
        NETW_CHECK_EQ(attempt_log().failed, 1);
    }
    {
        Bench bench;
        bench.core->unobserve(
            NetwMultiplayer::CONNECTION_FAILED,
            callable_mp_static(&record_failed)
        );
        NETW_CHECK_EQ(unhandled_after_timed_out_join(bench), 1);
    }
}

} // namespace TestNetwConnectAttempt
