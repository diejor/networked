#include "support/netw_test.h"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "godot/multiplayer.hpp"
#include "godot/resource_loader.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"
#include "netw/api/connect_handle.hpp"
#include "netw/api/link_conditions.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/session_config.hpp"
#include "netw/connect/creation.hpp"
#include "netw/connect/transport.hpp"

#if defined(NETW_TIER_HOSTED)

#include "support/declared_seams.h"
#include "support/minted_script.h"
#include <godot_cpp/classes/class_db_singleton.hpp>

namespace TestNetwConnectCreation {

using namespace godot;
using netw::LocalMultiplayerPeer;
using netw::NetwConnectHandle;
using netw::NetwMultiplayer;
using netw::connect::Transport;
using netw::connect::TransportBook;

namespace {

Ref<netw::NetwSessionConfig> shaping(
    const Ref<netw::NetwLinkConditions> &p_conditions
) {
    Ref<netw::NetwSessionConfig> settings;
    settings.instantiate();
    settings->set_link_conditions(p_conditions);
    return settings;
}

const char *PEER_CLASS = "CreationTestPeer";

enum Answering {
    ANSWER_NOTHING,
    ANSWER_PEER,
    ANSWER_ERROR,
};

struct ProviderLog {
    int built = 0;
    int cancelled = 0;
    int closed = 0;
    int destroyed = 0;
    RID ticket;
    int mode = -1;
    String address;
    Dictionary settings;
    int answering = ANSWER_NOTHING;
    Transport *standing = nullptr;
};

ProviderLog &provider() {
    static ProviderLog instance;
    return instance;
}

class CreationProvider : public Transport {
public:
    ~CreationProvider() override {
        provider().destroyed++;
        if (provider().standing == this) {
            provider().standing = nullptr;
        }
    }

    StringName peer_class() const override {
        return StringName(PEER_CLASS);
    }
    String display_name() const override {
        return String("Creation");
    }

    void make_peer(
        int p_mode,
        const String &p_address,
        const Dictionary &p_settings
    ) override {
        provider().built++;
        provider().ticket = ticket();
        provider().mode = p_mode;
        provider().address = p_address;
        provider().settings = p_settings;
        provider().standing = this;
        report(StringName("constructing"), String("building"), 0.5);
        if (provider().answering == ANSWER_PEER) {
            Ref<LocalMultiplayerPeer> made;
            made.instantiate();
            made->create_client(7);
            deliver(made);
        } else if (provider().answering == ANSWER_ERROR) {
            fail(ERR_CANT_CONNECT, String("the provider refused"));
        }
    }

    void cancel_peer_creation() override {
        provider().cancelled++;
    }
    void close() override {
        provider().closed++;
    }
};

Transport *make_creation_provider() {
    return new CreationProvider();
}

struct AnswerLog {
    int calls = 0;
    Ref<MultiplayerPeer> peer;
    int64_t error = 0;
    String detail;
    int progress_calls = 0;
    String progress_step;
    bool assign = false;
    bool assign_wrapped = false;
    Ref<MultiplayerPeer> wrapper;
    bool reenter = false;
    bool cancel_own = false;
    RID own_ticket;
    RID reentered;
    NetwMultiplayer *target = nullptr;
    NetwMultiplayer *foreign = nullptr;
    Ref<MultiplayerPeer> peer_at_entry;
    int state_at_entry = -1;
};

AnswerLog &answer() {
    static AnswerLog instance;
    return instance;
}

RID slot_of(const Ref<NetwMultiplayer> &p_core) {
    return p_core->transport_find(StringName(PEER_CLASS));
}

void on_progress(StringName p_step, String p_message, double p_ratio) {
    answer().progress_calls++;
    answer().progress_step = String(p_step);
}

void on_created(Ref<MultiplayerPeer> p_peer, int64_t p_error, String p_detail) {
    answer().calls++;
    answer().peer = p_peer;
    answer().error = p_error;
    answer().detail = p_detail;
    if (answer().target != nullptr) {
        answer().peer_at_entry = answer().target->get_multiplayer_peer();
        answer().state_at_entry = int(answer().target->session_get_state());
    }
    if (answer().cancel_own && answer().target != nullptr) {
        answer().target->transport_cancel_peer_creation(answer().own_ticket);
    }
    if (answer().foreign != nullptr && p_peer.is_valid()) {
        answer().foreign->NETW_API_VIRTUAL(set_multiplayer_peer)(p_peer);
    }
    if (answer().assign && p_peer.is_valid() && answer().target != nullptr) {
        answer().target->NETW_API_VIRTUAL(set_multiplayer_peer)(p_peer);
    }
    if (answer().assign_wrapped && p_peer.is_valid()
        && answer().target != nullptr) {
        Ref<netw::NetwLinkConditions> conditions;
        conditions.instantiate();
        conditions->set_simulate_lag(true);
        answer().wrapper = conditions->wrap_peer(p_peer);
        answer().target->NETW_API_VIRTUAL(set_multiplayer_peer)(
            answer().wrapper
        );
    }
    if (answer().reenter && answer().target != nullptr) {
        answer().reenter = false;
        answer().reentered = answer().target->transport_create_peer(
            slot_of(Ref<NetwMultiplayer>(answer().target)),
            NetwMultiplayer::TRANSPORT_MODE_HOST,
            String(),
            Dictionary(),
            callable_mp_static(&on_created),
            Callable()
        );
    }
}

struct Bench {
    Ref<NetwMultiplayer> core;

    Bench() {
        provider() = ProviderLog();
        answer() = AnswerLog();
        TransportBook::shared().install_native(
            StringName(PEER_CLASS),
            make_creation_provider,
            String("Creation")
        );
        core.instantiate();
        answer().target = core.ptr();
    }

    ~Bench() {
        answer() = AnswerLog();
        if (core.is_valid()) {
            core->embed_dispose();
            core = Ref<NetwMultiplayer>();
        }
        TransportBook::shared().forget(StringName(PEER_CLASS));
    }

    RID transport() const {
        return slot_of(core);
    }

    RID ask(
        int p_mode = NetwMultiplayer::TRANSPORT_MODE_CLIENT,
        const String &p_address = String("somewhere"),
        const Dictionary &p_settings = Dictionary()
    ) {
        return core->transport_create_peer(
            transport(),
            p_mode,
            p_address,
            p_settings,
            callable_mp_static(&on_created),
            callable_mp_static(&on_progress)
        );
    }

    void drive() {
        core->connect_plane().on_poll(0.0);
    }
};

const char *HELD_SCRIPT = netw_test::gdsrc::TRANSPORT_THAT_HOLDS;
const char *PROMPT_SCRIPT = netw_test::gdsrc::TRANSPORT_THAT_SETTLES;

Ref<Script> script_at(const char *p_path) {
    return netw_test::script_from(p_path);
}

Ref<NetwMultiplayer> lone_session() {
    Ref<NetwMultiplayer> made;
    made.instantiate();
    return made;
}

Ref<Script> receiver_script() {
    const Ref<Script> shape
        = ClassDBSingleton::get_singleton()->instantiate("GDScript");
    shape->set_source_code(String(
        "extends Object\n"
        "var calls := 0\n"
        "func on_created(_p, _e, _d) -> void:\n"
        "\tcalls += 1\n"
    ));
    shape->reload();
    return shape;
}

} // namespace

TEST_CASE(
    "[Networked][Connect][Creation] a provider that answers inside its own "
    "build still reports after create returns, because a caller stores the "
    "ticket from the return value and a callback that beat it would have "
    "nothing to cancel"
) {
    Bench bench;
    provider().answering = ANSWER_PEER;

    const RID ticket = bench.ask();

    CHECK(ticket.is_valid());
    NETW_CHECK_EQ(answer().calls, 0);
    NETW_CHECK_EQ(provider().built, 0);

    bench.drive();

    NETW_CHECK_EQ(answer().calls, 1);
    NETW_CHECK_EQ(answer().error, int64_t(OK));
    CHECK(answer().peer.is_valid());
    NETW_CHECK_EQ(provider().mode, int(NetwMultiplayer::TRANSPORT_MODE_CLIENT));
    CHECK(provider().address == String("somewhere"));
    CHECK(provider().ticket == ticket);
}

TEST_CASE(
    "[Networked][Connect][Creation] a delivered peer is not the session's "
    "peer until the callback assigns it, so a provider that drives itself "
    "cannot announce a connection the session is not yet listening for"
) {
    Bench bench;
    provider().answering = ANSWER_PEER;
    answer().assign = false;

    bench.ask();
    bench.drive();

    NETW_CHECK_EQ(answer().calls, 1);
    CHECK(answer().peer_at_entry.is_null());
    NETW_CHECK_EQ(
        answer().state_at_entry,
        int(NetwMultiplayer::SESSION_STATE_OFFLINE)
    );
    CHECK(bench.core->get_multiplayer_peer().is_null());
}

TEST_CASE(
    "[Networked][Connect][Creation] a creation that fails leaves the peer "
    "already assigned exactly where it was, because a failed attempt to "
    "reach somewhere else is not a reason to leave where you are"
) {
    Bench bench;
    Ref<LocalMultiplayerPeer> standing;
    standing.instantiate();
    standing->create_server();
    bench.core->NETW_API_VIRTUAL(set_multiplayer_peer)(standing);
    REQUIRE(bench.core->get_multiplayer_peer() == standing);

    provider().answering = ANSWER_ERROR;
    bench.ask();
    bench.drive();

    NETW_CHECK_EQ(answer().calls, 1);
    NETW_CHECK_EQ(answer().error, int64_t(ERR_CANT_CONNECT));
    CHECK(answer().peer.is_null());
    CHECK(bench.core->get_multiplayer_peer() == standing);
}

TEST_CASE(
    "[Networked][Connect][Creation] a cancellation before the provider "
    "starts and one taken while the provider still holds the build both "
    "answer exactly one skipped callback and install nothing"
) {
    Bench bench;
    provider().answering = ANSWER_NOTHING;

    const RID early = bench.ask();
    bench.core->transport_cancel_peer_creation(early);

    NETW_CHECK_EQ(answer().calls, 1);
    NETW_CHECK_EQ(answer().error, int64_t(ERR_SKIP));
    CHECK(answer().peer.is_null());
    NETW_CHECK_EQ(provider().built, 0);

    bench.drive();
    NETW_CHECK_EQ(answer().calls, 1);

    answer() = AnswerLog();
    answer().target = bench.core.ptr();

    const RID held = bench.ask();
    bench.drive();
    NETW_CHECK_EQ(provider().built, 1);
    NETW_CHECK_EQ(answer().calls, 0);

    bench.core->transport_cancel_peer_creation(held);

    NETW_CHECK_EQ(answer().calls, 1);
    NETW_CHECK_EQ(answer().error, int64_t(ERR_SKIP));
    NETW_CHECK_EQ(provider().cancelled, 1);
    CHECK(bench.core->get_multiplayer_peer().is_null());

    bench.core->transport_cancel_peer_creation(held);
    NETW_CHECK_EQ(answer().calls, 1);
}

TEST_CASE(
    "[Networked][Connect][Creation] a success the callback declines is "
    "closed by the session rather than leaked, so ignoring an offer costs "
    "the caller no release call and a freed receiver costs it nothing either"
) {
    Bench bench;
    provider().answering = ANSWER_PEER;
    answer().assign = false;

    bench.ask();
    bench.drive();

    NETW_CHECK_EQ(answer().calls, 1);
    REQUIRE(answer().peer.is_valid());
    NETW_CHECK_EQ(
        int(answer().peer->get_connection_status()),
        int(MultiplayerPeer::CONNECTION_DISCONNECTED)
    );
    NETW_CHECK_EQ(provider().destroyed, 1);
    NETW_CHECK_EQ(provider().closed, 1);

    Object *receiver = Object::cast_to<Object>(receiver_script()->call("new"));
    REQUIRE(receiver != nullptr);
    const Callable dying(receiver, StringName("on_created"));
    bench.core->transport_create_peer(
        bench.transport(),
        NetwMultiplayer::TRANSPORT_MODE_HOST,
        String(),
        Dictionary(),
        dying,
        Callable()
    );
    memdelete(receiver);
    bench.drive();

    NETW_CHECK_EQ(provider().destroyed, 2);
    NETW_CHECK_EQ(provider().closed, 2);
}

TEST_CASE(
    "[Networked][Connect][Creation] an assignment inside the callback pins "
    "the exact provider that built the peer, and neither the ticket retiring "
    "nor the transport unregistering afterward closes it"
) {
    Bench bench;
    provider().answering = ANSWER_PEER;
    answer().assign = true;

    const RID ticket = bench.ask();
    bench.drive();

    NETW_CHECK_EQ(answer().calls, 1);
    REQUIRE(answer().peer.is_valid());
    CHECK(bench.core->get_multiplayer_peer() == answer().peer);
    NETW_CHECK_EQ(provider().destroyed, 0);
    NETW_CHECK_EQ(provider().closed, 0);
    NETW_CHECK_EQ(
        int(answer().peer->get_connection_status()),
        int(MultiplayerPeer::CONNECTION_CONNECTING)
    );

    bench.core->transport_cancel_peer_creation(ticket);

    NETW_CHECK_EQ(answer().calls, 1);
    NETW_CHECK_EQ(provider().destroyed, 0);
    NETW_CHECK_EQ(provider().closed, 0);
    CHECK(bench.core->get_multiplayer_peer() == answer().peer);

    SUBCASE("and clearing the peer is what releases that provider") {
        bench.core->NETW_API_VIRTUAL(set_multiplayer_peer)(
            Ref<MultiplayerPeer>()
        );

        NETW_CHECK_EQ(provider().closed, 1);
        NETW_CHECK_EQ(provider().destroyed, 1);
        CHECK(bench.core->NETW_API_VIRTUAL(get_multiplayer_peer)().is_null());
    }
}

TEST_CASE(
    "[Networked][Connect][Creation] withdrawing a registration settles the "
    "builds it has not handed over and leaves the peer it already did, so "
    "cancellation and unregistration are one mechanism rather than two"
) {
    const Ref<NetwMultiplayer> core = lone_session();
    answer() = AnswerLog();
    answer().target = core.ptr();

    const Ref<Script> prompt = script_at(PROMPT_SCRIPT);
    REQUIRE(prompt.is_valid());
    const RID delivering = core->transport_register(prompt);
    REQUIRE(delivering.is_valid());

    answer().assign = true;
    core->transport_create_peer(
        delivering,
        NetwMultiplayer::TRANSPORT_MODE_CLIENT,
        String("here"),
        Dictionary(),
        callable_mp_static(&on_created),
        Callable()
    );
    core->connect_plane().on_poll(0.0);

    NETW_CHECK_EQ(answer().calls, 1);
    REQUIRE(answer().peer.is_valid());
    const Ref<MultiplayerPeer> assigned = answer().peer;
    CHECK(core->get_multiplayer_peer() == assigned);

    const Ref<Script> held = script_at(HELD_SCRIPT);
    REQUIRE(held.is_valid());
    const RID holding = core->transport_register(held);
    REQUIRE(holding.is_valid());

    answer() = AnswerLog();
    answer().target = core.ptr();
    core->transport_create_peer(
        holding,
        NetwMultiplayer::TRANSPORT_MODE_CLIENT,
        String("nowhere"),
        Dictionary(),
        callable_mp_static(&on_created),
        Callable()
    );
    core->connect_plane().on_poll(0.0);
    NETW_CHECK_EQ(answer().calls, 0);

    NETW_CHECK_EQ(int64_t(core->transport_unregister(holding)), int64_t(OK));

    NETW_CHECK_EQ(answer().calls, 1);
    NETW_CHECK_EQ(answer().error, int64_t(ERR_UNAVAILABLE));
    CHECK(answer().peer.is_null());
    CHECK(core->get_multiplayer_peer() == assigned);
    NETW_CHECK_EQ(
        int(assigned->get_connection_status()),
        int(MultiplayerPeer::CONNECTION_CONNECTING)
    );

    NETW_CHECK_EQ(int64_t(core->transport_unregister(delivering)), int64_t(OK));
    CHECK(core->get_multiplayer_peer() == assigned);
    NETW_CHECK_EQ(
        int(assigned->get_connection_status()),
        int(MultiplayerPeer::CONNECTION_CONNECTING)
    );

    answer() = AnswerLog();
    core->embed_dispose();
}

TEST_CASE(
    "[Networked][Connect][Creation] a creation started from inside a "
    "completion callback is a new request answered in a later turn, and no "
    "request answers twice however it is replaced or torn down"
) {
    Bench bench;
    provider().answering = ANSWER_PEER;
    answer().reenter = true;

    bench.ask();
    bench.drive();

    NETW_CHECK_EQ(answer().calls, 1);
    CHECK(answer().reentered.is_valid());
    NETW_CHECK_EQ(provider().built, 1);

    bench.drive();

    NETW_CHECK_EQ(answer().calls, 2);
    NETW_CHECK_EQ(provider().built, 2);

    const int settled = answer().calls;
    answer().cancel_own = true;
    answer().own_ticket = answer().reentered;
    bench.core->transport_cancel_peer_creation(answer().reentered);
    NETW_CHECK_EQ(answer().calls, settled);

    provider().answering = ANSWER_NOTHING;
    bench.ask();
    bench.drive();
    const int before_teardown = answer().calls;
    NETW_CHECK_EQ(before_teardown, settled);

    bench.core->embed_dispose();
    NETW_CHECK_EQ(answer().calls, before_teardown);

    bench.core = Ref<NetwMultiplayer>();
    NETW_CHECK_EQ(answer().calls, before_teardown);
    NETW_CHECK_EQ(provider().destroyed, provider().built);
    NETW_CHECK_EQ(provider().closed, provider().built);
}

TEST_CASE(
    "[Networked][Connect][Creation] a peer offered to one session is "
    "refused by another, and a ticket a session never minted cancels "
    "nothing, so a stale answer cannot reach a newer match"
) {
    Bench bench;
    Ref<NetwMultiplayer> other;
    other.instantiate();

    provider().answering = ANSWER_PEER;
    answer().assign = false;
    answer().foreign = other.ptr();

    const RID ticket = bench.ask();
    bench.drive();

    NETW_CHECK_EQ(answer().calls, 1);
    REQUIRE(answer().peer.is_valid());
    CHECK(other->get_multiplayer_peer().is_null());
    CHECK(bench.core->get_multiplayer_peer().is_null());

    answer().foreign = nullptr;
    other->transport_cancel_peer_creation(ticket);
    NETW_CHECK_EQ(answer().calls, 1);

    provider().answering = ANSWER_NOTHING;
    const RID mine = bench.ask();
    bench.drive();
    other->transport_cancel_peer_creation(mine);
    NETW_CHECK_EQ(answer().calls, 1);
    NETW_CHECK_EQ(provider().cancelled, 0);

    other->embed_dispose();
}

TEST_CASE(
    "[Networked][Connect][Creation] the mode a caller asked for is what "
    "reaches the provider whatever the peer answers for its own id, because "
    "a host is a request rather than a number read back off a peer"
) {
    Bench bench;
    provider().answering = ANSWER_PEER;
    answer().assign = true;

    Dictionary settings;
    settings["port"] = 4242;
    bench.ask(NetwMultiplayer::TRANSPORT_MODE_HOST, String(), settings);
    bench.drive();

    NETW_CHECK_EQ(provider().mode, int(NetwMultiplayer::TRANSPORT_MODE_HOST));
    NETW_CHECK_EQ(int64_t(provider().settings["port"]), int64_t(4242));
    NETW_CHECK_EQ(answer().calls, 1);
    REQUIRE(answer().peer.is_valid());
    NETW_CHECK_EQ(int64_t(answer().peer->get_unique_id()), int64_t(7));
    CHECK(bench.core->get_multiplayer_peer() == answer().peer);
    NETW_CHECK_EQ(answer().progress_calls, 1);
    CHECK(answer().progress_step == String("constructing"));
}

TEST_CASE(
    "[Networked][Connect][Creation] a peer a game built and assigned itself "
    "owes the creation seam nothing, so no ticket, no callback and no offer "
    "stands behind an ordinary assignment"
) {
    Bench bench;
    provider().answering = ANSWER_PEER;
    answer().assign = false;

    bench.ask();
    bench.drive();
    NETW_CHECK_EQ(answer().calls, 1);
    const Ref<MultiplayerPeer> declined = answer().peer;
    REQUIRE(declined.is_valid());
    CHECK(netw::connect::offer_of_peer(declined) == nullptr);

    Ref<LocalMultiplayerPeer> own;
    own.instantiate();
    own->create_server();
    CHECK(netw::connect::offer_of_peer(own) == nullptr);

    bench.core->NETW_API_VIRTUAL(set_multiplayer_peer)(own);

    CHECK(bench.core->get_multiplayer_peer() == own);
    NETW_CHECK_EQ(answer().calls, 1);
    NETW_CHECK_EQ(provider().built, 1);
    CHECK(netw::connect::offer_of_peer(own) == nullptr);

    bench.core->NETW_API_VIRTUAL(set_multiplayer_peer)(Ref<MultiplayerPeer>());
    CHECK(bench.core->get_multiplayer_peer().is_null());
    NETW_CHECK_EQ(answer().calls, 1);
}

TEST_CASE(
    "[Networked][Connect][Creation] a transport this session does not hold "
    "answers a valid ticket and one deferred refusal rather than a silent "
    "invalid handle, because a caller reads its reason from the callback"
) {
    Bench bench;

    const RID ticket = bench.core->transport_create_peer(
        RID(),
        NetwMultiplayer::TRANSPORT_MODE_CLIENT,
        String("somewhere"),
        Dictionary(),
        callable_mp_static(&on_created),
        Callable()
    );

    CHECK(ticket.is_valid());
    NETW_CHECK_EQ(answer().calls, 0);

    bench.drive();

    NETW_CHECK_EQ(answer().calls, 1);
    NETW_CHECK_EQ(answer().error, int64_t(ERR_DOES_NOT_EXIST));
    CHECK(answer().peer.is_null());

    const RID uncallable = bench.core->transport_create_peer(
        bench.transport(),
        NetwMultiplayer::TRANSPORT_MODE_CLIENT,
        String(),
        Dictionary(),
        Callable(),
        Callable()
    );
    CHECK_FALSE(uncallable.is_valid());
    bench.drive();
    NETW_CHECK_EQ(answer().calls, 1);
}

TEST_CASE(
    "[Networked][Connect][Creation] a peer a provider built is shaped by the "
    "session's link conditions exactly as one the game built is, and a peer "
    "that already carries shaping is installed as authored rather than "
    "wrapped a second time"
) {
    REQUIRE_MESSAGE(
        ClassDB::class_exists(StringName("LaggyMultiplayerPeer")),
        "shaping is only observable where the impairment peer is installed"
    );

    Bench bench;
    Ref<netw::NetwLinkConditions> conditions;
    conditions.instantiate();
    conditions->set_simulate_lag(true);
    conditions->set_one_way_delay_min(0.0);
    conditions->set_one_way_delay_max(0.0);
    conditions->set_lag_packet_loss_percent(100.0);
    bench.core->session_initialize(shaping(conditions));

    provider().answering = ANSWER_PEER;
    answer().assign = true;
    const RID ticket = bench.ask();
    REQUIRE(ticket.is_valid());
    bench.drive();

    NETW_CHECK_EQ(answer().calls, 1);
    REQUIRE(answer().peer.is_valid());
    CHECK(
        bench.core->NETW_API_VIRTUAL(get_multiplayer_peer)() == answer().peer
    );

    const Ref<MultiplayerPeer> polled
        = bench.core->session_get_inner()->get_multiplayer_peer();
    REQUIRE(polled.is_valid());
    CHECK(polled != answer().peer);
    CHECK(polled->is_class(StringName("LaggyMultiplayerPeer")));

    SUBCASE("and a session handed that shaped peer installs it unchanged") {
        const Ref<NetwMultiplayer> second = lone_session();
        second->session_initialize(shaping(conditions));
        second->NETW_API_VIRTUAL(set_multiplayer_peer)(polled);

        CHECK(second->session_get_inner()->get_multiplayer_peer() == polled);
        CHECK(second->NETW_API_VIRTUAL(get_multiplayer_peer)() == polled);

        second->NETW_API_VIRTUAL(set_multiplayer_peer)(Ref<MultiplayerPeer>());
        second->embed_dispose();
    }
}

TEST_CASE(
    "[Networked][Connect][Creation] a caller that assigns a wrapper around "
    "the peer it was offered has claimed that offer, so the offered peer "
    "keeps its socket and the transport it was built from goes live"
) {
    REQUIRE_MESSAGE(
        ClassDB::class_exists(StringName("LaggyMultiplayerPeer")),
        "a wrapped assignment is only observable where a wrapper is installed"
    );

    Bench bench;
    provider().answering = ANSWER_PEER;
    answer().assign_wrapped = true;

    const RID ticket = bench.ask();
    REQUIRE(ticket.is_valid());
    bench.drive();

    NETW_CHECK_EQ(answer().calls, 1);
    REQUIRE(answer().peer.is_valid());
    REQUIRE(answer().wrapper.is_valid());
    CHECK(answer().wrapper != answer().peer);

    CHECK_MESSAGE(
        answer().peer->get_connection_status()
            != MultiplayerPeer::CONNECTION_DISCONNECTED,
        "the offered peer was closed as a declined offer"
    );
    CHECK(
        bench.core->NETW_API_VIRTUAL(get_multiplayer_peer)() == answer().wrapper
    );
    NETW_CHECK_EQ(provider().closed, 0);
    NETW_CHECK_EQ(provider().destroyed, 0);
}

} // namespace TestNetwConnectCreation

#endif
