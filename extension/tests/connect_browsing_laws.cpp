#include "support/netw_test.h"

#include "godot/variant.hpp"
#include "netw/api/connection.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/server_info.hpp"
#include "netw/connect/core.hpp"
#include "netw/connect/transport.hpp"
#include "support/directory_stubs.h"

namespace TestNetwConnectBrowsing {

using namespace godot;
using netw::NetwConnection;
using netw::NetwMultiplayer;
using netw::connect::TransportBook;

namespace {

const char *PROBED_PEER_CLASS = "BrowsingProbedPeer";

int &probes() {
    static int count = 0;
    return count;
}

class ProbedTransport : public netw::connect::Transport {
public:
    StringName peer_class() const override {
        return StringName(PROBED_PEER_CLASS);
    }
    String display_name() const override {
        return String("probed");
    }
    bool can_probe() const override {
        return true;
    }
    void make_peer(int, const String &, const Dictionary &) override {
        fail(ERR_UNCONFIGURED, String("probed"));
    }
    void probe(const String &) override {
        probes() += 1;
        Ref<netw::NetwServerInfo> info;
        info.instantiate();
        info->set_players(probes());
        deliver_probe(info);
    }
};

netw::connect::Transport *make_probed() {
    return new ProbedTransport();
}

const char *HELD_PEER_CLASS = "BrowsingHeldPeer";

class HeldTransport;

LocalVector<HeldTransport *> &held_probes() {
    static LocalVector<HeldTransport *> outstanding;
    return outstanding;
}

class HeldTransport : public netw::connect::Transport {
public:
    ~HeldTransport() override {
        for (uint32_t at = 0; at < held_probes().size(); at++) {
            if (held_probes()[at] == this) {
                held_probes().remove_at(at);
                return;
            }
        }
    }
    StringName peer_class() const override {
        return StringName(HELD_PEER_CLASS);
    }
    String display_name() const override {
        return String("held");
    }
    bool can_probe() const override {
        return true;
    }
    void make_peer(int, const String &, const Dictionary &) override {
        fail(ERR_UNCONFIGURED, String("held"));
    }
    void probe(const String &) override {
        held_probes().push_back(this);
    }
    void answer() {
        Ref<netw::NetwServerInfo> info;
        info.instantiate();
        deliver_probe(info);
    }
};

netw::connect::Transport *make_held() {
    return new HeldTransport();
}

struct Bench {
    Ref<NetwMultiplayer> core;
    netw_test::StubDirectory *directory = nullptr;

    Bench() {
        probes() = 0;
        TransportBook::shared().install_native(
            StringName(PROBED_PEER_CLASS),
            make_probed,
            String("probed")
        );
        core.instantiate();
        directory = memnew(netw_test::StubDirectory);
        core->service_register(directory, nullptr);
    }

    ~Bench() {
        core->service_unregister(directory, nullptr);
        memdelete(directory);
        core->embed_dispose();
        TransportBook::shared().forget(StringName(PROBED_PEER_CLASS));
        probes() = 0;
    }

    Ref<NetwConnection> connection() {
        return core->get_connection();
    }

    RID add_row() {
        return core->endpoint_add(
            core->transport_find(StringName(PROBED_PEER_CLASS)),
            String("probed.example:21253"),
            String()
        );
    }

    void poll(double p_seconds) {
        core->connect_plane().on_poll(p_seconds);
    }
};

} // namespace

TEST_CASE(
    "[Networked][Connect][Hosted] a directory is listed on the first poll "
    "after browsing starts and again every list interval while browsing, and "
    "never while browsing is off"
) {
    Bench bench;
    bench.directory->set_list_interval(1.0);
    const Ref<NetwConnection> connection = bench.connection();

    bench.poll(5.0);
    NETW_CHECK_EQ(bench.directory->browse_calls, 0);

    connection->set_browsing(true);
    CHECK(connection->is_browsing());
    NETW_CHECK_EQ(bench.directory->browse_calls, 0);
    bench.poll(0.5);
    NETW_CHECK_EQ(bench.directory->browse_calls, 1);
    bench.poll(0.5);
    NETW_CHECK_EQ(bench.directory->browse_calls, 1);
    bench.poll(0.5);
    NETW_CHECK_EQ(bench.directory->browse_calls, 2);
    bench.poll(1.0);
    NETW_CHECK_EQ(bench.directory->browse_calls, 3);

    connection->set_browsing(false);
    bench.poll(5.0);
    NETW_CHECK_EQ(bench.directory->browse_calls, 3);

    bench.directory->set_list_interval(0.0);
    connection->set_browsing(true);
    bench.poll(5.0);
    NETW_CHECK_EQ(bench.directory->browse_calls, 4);
}

TEST_CASE(
    "[Networked][Connect][Hosted] turning browsing off stops every "
    "directory's listing once, and leaves its lobby alone"
) {
    Bench bench;

    bench.core->endpoint_set_browsing(true);
    bench.poll(1.0);
    NETW_CHECK_EQ(bench.directory->stop_calls, 0);

    bench.core->endpoint_set_browsing(false);
    NETW_CHECK_EQ(bench.directory->stop_calls, 1);
    NETW_CHECK_EQ(bench.directory->leave_calls, 0);

    bench.core->endpoint_set_browsing(false);
    NETW_CHECK_EQ(bench.directory->stop_calls, 1);
}

TEST_CASE(
    "[Networked][Connect][Hosted] a row the game added is probed when "
    "browsing starts and again every probe interval, and not at all while "
    "browsing is off"
) {
    Bench bench;
    const bool stock_interval
        = bench.core->endpoint_get_probe_interval() == 10.0;
    CHECK(stock_interval);
    bench.core->endpoint_set_probe_interval(1.0);
    REQUIRE(bench.add_row().is_valid());

    bench.poll(5.0);
    NETW_CHECK_EQ(probes(), 0);

    bench.core->endpoint_set_browsing(true);
    NETW_CHECK_EQ(probes(), 1);
    bench.poll(0.5);
    NETW_CHECK_EQ(probes(), 1);
    bench.poll(0.5);
    NETW_CHECK_EQ(probes(), 2);

    bench.core->endpoint_set_browsing(false);
    bench.poll(5.0);
    NETW_CHECK_EQ(probes(), 2);

    bench.core->endpoint_set_probe_interval(0.0);
    bench.core->endpoint_set_browsing(true);
    bench.poll(5.0);
    NETW_CHECK_EQ(probes(), 3);
}

TEST_CASE(
    "[Networked][Connect][Hosted] rows stay listed with their last answer "
    "after browsing stops"
) {
    Bench bench;
    bench.directory->rows.push_back(String("42"));
    const RID added = bench.add_row();
    REQUIRE(added.is_valid());

    bench.core->endpoint_set_browsing(true);
    bench.poll(0.1);
    NETW_REQUIRE_EQ(int(bench.core->endpoint_list().size()), 2);
    const int64_t flags = bench.core->endpoint_get_state(
        added,
        NetwMultiplayer::ENDPOINT_STATE_FLAGS
    );
    const bool observed
        = (flags & NetwMultiplayer::ENDPOINT_FLAG_OBSERVED) != 0;
    REQUIRE(observed);
    const int64_t status = bench.core->endpoint_get_state(
        added,
        NetwMultiplayer::ENDPOINT_STATE_STATUS
    );
    const Ref<netw::NetwServerInfo> answered = bench.core->endpoint_get_state(
        added,
        NetwMultiplayer::ENDPOINT_STATE_INFO
    );
    REQUIRE(answered.is_valid());

    bench.core->endpoint_set_browsing(false);
    bench.poll(1.0);

    NETW_CHECK_EQ(int(bench.core->endpoint_list().size()), 2);
    const int64_t kept_flags = bench.core->endpoint_get_state(
        added,
        NetwMultiplayer::ENDPOINT_STATE_FLAGS
    );
    const bool still_observed
        = (kept_flags & NetwMultiplayer::ENDPOINT_FLAG_OBSERVED) != 0;
    CHECK(still_observed);
    NETW_CHECK_EQ(
        int64_t(bench.core->endpoint_get_state(
            added,
            NetwMultiplayer::ENDPOINT_STATE_STATUS
        )),
        status
    );
    const Ref<netw::NetwServerInfo> kept = bench.core->endpoint_get_state(
        added,
        NetwMultiplayer::ENDPOINT_STATE_INFO
    );
    const bool same_answer = kept == answered;
    CHECK(same_answer);
}

TEST_CASE(
    "[Networked][Connect][Hosted] an explicit probe counts toward the "
    "in-flight cap, so queued probes never exceed it once it settles"
) {
    TransportBook::shared()
        .install_native(StringName(HELD_PEER_CLASS), make_held, String("held"));
    held_probes().clear();
    {
        Ref<NetwMultiplayer> core;
        core.instantiate();
        const RID transport = core->transport_find(StringName(HELD_PEER_CLASS));
        LocalVector<RID> added;
        for (int at = 0; at < 8; at++) {
            added.push_back(core->endpoint_add(
                transport,
                String("held.example:") + itos(21000 + at),
                String()
            ));
        }
        const int cap = netw::connect::BrowseList::MAX_IN_FLIGHT_PROBES;

        core->endpoint_probe(added[0]);
        NETW_REQUIRE_EQ(int(held_probes().size()), 1);
        HeldTransport *explicit_probe = held_probes()[0];

        core->endpoint_set_browsing(true);
        int most = int(held_probes().size());

        explicit_probe->answer();
        for (int poll = 0; poll < 4; poll++) {
            core->connect_plane().on_poll(0.1);
            if (int(held_probes().size()) > most) {
                most = int(held_probes().size());
            }
        }
        const bool within_cap = most <= cap;
        CHECK(within_cap);
        NETW_CHECK_EQ(int(held_probes().size()), cap);

        core->endpoint_set_browsing(false);
        core->embed_dispose();
    }
    held_probes().clear();
    TransportBook::shared().forget(StringName(HELD_PEER_CLASS));
}

} // namespace TestNetwConnectBrowsing
