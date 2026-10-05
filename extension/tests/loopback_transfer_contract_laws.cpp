#include "support/netw_recorder.h"
#include "support/netw_test.h"

#include <cstring>

#include "godot/callable.hpp"
#include "godot/templates.hpp"
#include "netw/api/loopback.hpp"

namespace TestLoopbackTransferContractLaws {

using namespace godot;
using netw::LocalLoopbackSession;
using netw::LocalMultiplayerPeer;
using netw_test::Recorder;

StringName relabelled() {
    return StringName("server_relabelled");
}

class Mesh {
public:
    Ref<LocalLoopbackSession> session;
    Vector<Ref<LocalMultiplayerPeer>> peers;

    explicit Mesh(int p_clients) {
        session.instantiate();
        session->enable_server_transfer();
        peers.push_back(session->get_server_peer());
        for (int at = 0; at < p_clients; ++at) {
            peers.push_back(session->create_client_peer());
        }
        session->poll();
    }

    ~Mesh() {
        session->reset();
    }

    Mesh(const Mesh &) = delete;
    Mesh &operator=(const Mesh &) = delete;

    Ref<LocalMultiplayerPeer> client(int p_index) const {
        return peers[p_index + 1];
    }

    Vector<Ref<LocalMultiplayerPeer>> survivors() const {
        Vector<Ref<LocalMultiplayerPeer>> alive;
        for (const Ref<LocalMultiplayerPeer> &peer : peers) {
            if (peer->get_connection_status()
                != MultiplayerPeer::CONNECTION_DISCONNECTED) {
                alive.push_back(peer);
            }
        }
        return alive;
    }

    int lowest_client_id() const {
        int lowest = 0;
        for (int at = 1; at < peers.size(); ++at) {
            const int id = peers[at]->get_unique_id();
            if (lowest == 0 || id < lowest) {
                lowest = id;
            }
        }
        return lowest;
    }
};

class Snapshot {
public:
    struct Seen {
        int unique_id = 0;
        int packet_peer = 0;
        int calls = 0;
    };

    std::shared_ptr<Seen> seen = std::make_shared<Seen>();
};

class SnapshotSink final : public CallableCustom {
    std::shared_ptr<Snapshot::Seen> seen;
    ObjectID source;

    static bool same(const CallableCustom *a, const CallableCustom *b) {
        return a == b;
    }

    static bool before(const CallableCustom *a, const CallableCustom *b) {
        return a < b;
    }

public:
    SnapshotSink(
        const std::shared_ptr<Snapshot::Seen> &p_seen,
        const Object *p_source
    )
        : seen(p_seen), source(netw::gd::instance_id(p_source)) {
    }

    uint32_t hash() const override {
        return uint32_t(uintptr_t(this));
    }

    String get_as_text() const override {
        return String("SnapshotSink");
    }

    CompareEqualFunc get_compare_equal_func() const override {
        return &SnapshotSink::same;
    }

    CompareLessFunc get_compare_less_func() const override {
        return &SnapshotSink::before;
    }

    ObjectID get_object() const override {
        return source;
    }

    void call(
        const Variant **p_arguments,
        int p_argcount,
        Variant &r_return_value,
        netw::gd::CallError &r_call_error
    ) const override {
        LocalMultiplayerPeer *peer = Object::cast_to<LocalMultiplayerPeer>(
            netw::gd::instance_from_id(source)
        );
        if (peer != nullptr) {
            seen->unique_id = peer->get_unique_id();
            seen->packet_peer = peer->get_packet_peer();
        }
        seen->calls += 1;
        r_return_value = Variant();
        netw::gd::call_ok(r_call_error);
    }
};

Snapshot watch_install(const Ref<LocalMultiplayerPeer> &p_peer) {
    Snapshot snapshot;
    p_peer->connect(
        relabelled(),
        Callable(memnew(SnapshotSink(snapshot.seen, p_peer.ptr())))
    );
    return snapshot;
}

bool sent(
    const Ref<LocalMultiplayerPeer> &p_from,
    int p_to,
    int p_value
) {
    PackedByteArray bytes;
    bytes.push_back(uint8_t(p_value));
    p_from->set_target_peer(p_to);
#if defined(NETW_TIER_MODULE)
    return p_from->put_packet(bytes.ptr(), bytes.size()) == OK;
#else
    return p_from->put_packet(bytes) == OK;
#endif
}

bool handed_on(const Mesh &p_mesh, int p_successor, bool p_keep_former) {
    return p_mesh.session->transfer_server(p_successor, p_keep_former) == OK;
}

int receive(const Ref<LocalMultiplayerPeer> &p_peer) {
#if defined(NETW_TIER_MODULE)
    const uint8_t *buffer = nullptr;
    int size = 0;
    if (p_peer->get_packet(&buffer, size) != Error::OK || size < 1) {
        return -1;
    }
    return int(buffer[0]);
#else
    const PackedByteArray bytes = p_peer->get_packet();
    return bytes.is_empty() ? -1 : int(bytes[0]);
#endif
}

bool relays(const Ref<LocalMultiplayerPeer> &p_peer) {
    return p_peer->NETW_PEER_VIRTUAL(is_server_relay_supported)();
}

Vector<Recorder *> settled_books(
    Mesh &p_mesh,
    const Vector<StringName> &p_signals
) {
    Vector<Recorder *> books;
    for (const Ref<LocalMultiplayerPeer> &peer : p_mesh.peers) {
        books.push_back(memnew(Recorder(peer.ptr(), p_signals)));
    }
    p_mesh.session->poll();
    for (Recorder *book : books) {
        book->clear();
    }
    return books;
}

void check_connected(const Mesh &p_mesh, int p_expected) {
    const Vector<Ref<LocalMultiplayerPeer>> alive = p_mesh.survivors();
    NETW_CHECK_EQ(int(alive.size()), p_expected);
    for (const Ref<LocalMultiplayerPeer> &peer : alive) {
        NETW_CHECK_EQ(
            int(peer->get_connection_status()),
            int(MultiplayerPeer::CONNECTION_CONNECTED)
        );
    }
}

TEST_CASE(
    "[Networked][Transport][Hosted] LT1 after the server is lost every "
    "survivor reaches every other directly, so nothing in flight waits on "
    "a hub that is gone"
) {
    Mesh mesh(3);
    NETW_REQUIRE_EQ(int(mesh.session->lose_server()), int(OK));
    mesh.session->poll();

    const Vector<Ref<LocalMultiplayerPeer>> alive = mesh.survivors();
    NETW_REQUIRE_EQ(int(alive.size()), 3);
    for (const Ref<LocalMultiplayerPeer> &from : alive) {
        CHECK_FALSE(relays(from));
        for (const Ref<LocalMultiplayerPeer> &to : alive) {
            if (from == to) {
                continue;
            }
            NETW_REQUIRE_EQ(int(from->is_linked_to(to->get_unique_id())), 1);
            REQUIRE(sent(from, to->get_unique_id(), 5));
            NETW_REQUIRE_EQ(to->get_available_packet_count(), 1);
            NETW_CHECK_EQ(to->get_packet_peer(), from->get_unique_id());
            NETW_CHECK_EQ(receive(to), 5);
        }
    }
}

TEST_CASE(
    "[Networked][Transport][Hosted] LT2 every survivor installs the same "
    "relabel, once, whether the server was lost or handed its seat on"
) {
    SUBCASE("lost") {
        Mesh mesh(3);
        const int lowest = mesh.lowest_client_id();
        Vector<Recorder *> books;
        for (const Ref<LocalMultiplayerPeer> &peer : mesh.peers) {
            books.push_back(memnew(Recorder(peer.ptr(), {relabelled()})));
        }
        NETW_REQUIRE_EQ(int(mesh.session->lose_server()), int(OK));
        mesh.session->poll();

        for (int at = 1; at < books.size(); ++at) {
            NETW_CHECK_EQ(books[at]->count(relabelled()), 1);
            const Array args = books[at]->args(relabelled());
            NETW_CHECK_EQ(int(args[0]), lowest);
            NETW_CHECK_EQ(int(args[1]), 0);
        }
        NETW_CHECK_EQ(books[0]->count(relabelled()), 0);
        for (Recorder *book : books) {
            memdelete(book);
        }
    }
    SUBCASE("handed on") {
        Mesh mesh(2);
        const int successor = mesh.client(1)->get_unique_id();
        Vector<Recorder *> books;
        for (const Ref<LocalMultiplayerPeer> &peer : mesh.peers) {
            books.push_back(memnew(Recorder(peer.ptr(), {relabelled()})));
        }
        REQUIRE(handed_on(mesh, successor, true));
        mesh.session->poll();

        const Array first = books[0]->args(relabelled());
        NETW_CHECK_EQ(int(first[0]), successor);
        CHECK(bool(int(first[1]) != 0));
        for (Recorder *book : books) {
            NETW_CHECK_EQ(book->count(relabelled()), 1);
            CHECK(bool(book->args(relabelled()) == first));
            memdelete(book);
        }
    }
}

TEST_CASE(
    "[Networked][Transport][Hosted] LT3 inside the relabel a peer already "
    "answers its new id and names a sender by its new id"
) {
    Mesh mesh(2);
    const Ref<LocalMultiplayerPeer> former = mesh.peers[0];
    const Ref<LocalMultiplayerPeer> successor = mesh.client(0);
    const Ref<LocalMultiplayerPeer> bystander = mesh.client(1);
    const int bystander_id = bystander->get_unique_id();
    const Snapshot at_former = watch_install(former);
    const Snapshot at_successor = watch_install(successor);
    const Snapshot at_bystander = watch_install(bystander);
    Recorder book(successor.ptr(), {relabelled()});

    REQUIRE(handed_on(mesh, successor->get_unique_id(), true));
    successor->poll();
    NETW_CHECK_EQ(at_successor.seen->calls, 1);
    NETW_CHECK_EQ(at_successor.seen->unique_id, 1);
    const int former_now = int(book.args(relabelled())[1]);
    REQUIRE(bool(former_now > 1));

    REQUIRE(sent(successor, bystander_id, 1));
    REQUIRE(sent(successor, former_now, 2));
    bystander->poll();
    former->poll();

    NETW_CHECK_EQ(at_bystander.seen->calls, 1);
    NETW_CHECK_EQ(at_bystander.seen->unique_id, bystander_id);
    NETW_CHECK_EQ(at_bystander.seen->packet_peer, 1);
    NETW_CHECK_EQ(at_former.seen->calls, 1);
    NETW_CHECK_EQ(at_former.seen->unique_id, former_now);
    NETW_CHECK_EQ(at_former.seen->packet_peer, 1);
}

TEST_CASE(
    "[Networked][Transport][Hosted] LT4 a packet sent before the cut is "
    "dropped where it is read after the install, and one sent after the cut "
    "waits for the reader's own install"
) {
    Mesh mesh(2);
    const Ref<LocalMultiplayerPeer> former = mesh.peers[0];
    const Ref<LocalMultiplayerPeer> successor = mesh.client(0);
    const Ref<LocalMultiplayerPeer> bystander = mesh.client(1);
    const int successor_was = successor->get_unique_id();
    const int bystander_id = bystander->get_unique_id();

    REQUIRE(sent(bystander, 1, 7));
    REQUIRE(sent(bystander, successor_was, 8));
    NETW_REQUIRE_EQ(former->get_available_packet_count(), 1);
    NETW_REQUIRE_EQ(successor->get_available_packet_count(), 1);

    REQUIRE(handed_on(mesh, successor_was, true));
    successor->poll();
    NETW_CHECK_EQ(successor->get_available_packet_count(), 0);

    REQUIRE(sent(bystander, successor_was, 6));
    NETW_CHECK_EQ(successor->get_available_packet_count(), 0);

    REQUIRE(sent(successor, bystander_id, 9));
    NETW_CHECK_EQ(bystander->get_available_packet_count(), 0);

    former->poll();
    NETW_CHECK_EQ(former->get_available_packet_count(), 0);

    bystander->poll();
    NETW_REQUIRE_EQ(bystander->get_available_packet_count(), 1);
    NETW_CHECK_EQ(bystander->get_packet_peer(), 1);
    NETW_CHECK_EQ(receive(bystander), 9);
}

TEST_CASE(
    "[Networked][Transport][Hosted] LT5 a relabel raises no peer_connected "
    "and no peer_disconnected, and a lost server is reported only by the "
    "relabel"
) {
    const Vector<StringName> edges = {
        StringName("peer_connected"),
        StringName("peer_disconnected"),
    };
    SUBCASE("lost") {
        Mesh mesh(3);
        const Vector<Recorder *> books = settled_books(mesh, edges);
        NETW_REQUIRE_EQ(int(mesh.session->lose_server()), int(OK));
        mesh.session->poll();
        mesh.session->poll();
        for (Recorder *book : books) {
            NETW_CHECK_EQ(book->count(edges[0]), 0);
            NETW_CHECK_EQ(book->count(edges[1]), 0);
            memdelete(book);
        }
    }
    SUBCASE("handed on") {
        Mesh mesh(2);
        const Vector<Recorder *> books = settled_books(mesh, edges);
        REQUIRE(handed_on(mesh, mesh.client(1)->get_unique_id(), true));
        mesh.session->poll();
        mesh.session->poll();
        for (Recorder *book : books) {
            NETW_CHECK_EQ(book->count(edges[0]), 0);
            NETW_CHECK_EQ(book->count(edges[1]), 0);
            memdelete(book);
        }
    }
}

TEST_CASE(
    "[Networked][Transport][Hosted] LT6 every survivor stays connected at "
    "every step of a staggered install"
) {
    SUBCASE("lost") {
        Mesh mesh(3);
        NETW_REQUIRE_EQ(int(mesh.session->lose_server()), int(OK));
        check_connected(mesh, 3);
        for (int at = 0; at < 3; ++at) {
            mesh.client(at)->poll();
            check_connected(mesh, 3);
        }
    }
    SUBCASE("handed on") {
        Mesh mesh(2);
        REQUIRE(handed_on(mesh, mesh.client(0)->get_unique_id(), false));
        check_connected(mesh, 2);
        for (int at = 0; at < 2; ++at) {
            mesh.client(at)->poll();
            check_connected(mesh, 2);
        }
    }
}

TEST_CASE(
    "[Networked][Transport][Hosted] LT7 a successor's old id is never handed "
    "out again and a former server's new id was never used before"
) {
    Mesh mesh(2);
    HashSet<int> seen;
    for (const Ref<LocalMultiplayerPeer> &peer : mesh.peers) {
        seen.insert(peer->get_unique_id());
    }
    const int successor_was = mesh.client(0)->get_unique_id();
    Recorder book(mesh.peers[0].ptr(), {relabelled()});

    REQUIRE(handed_on(mesh, successor_was, true));
    mesh.session->poll();
    const int former_now = int(book.args(relabelled())[1]);
    CHECK(bool(former_now != 0));
    CHECK_FALSE(seen.has(former_now));
    seen.insert(former_now);

    for (int at = 0; at < 6; ++at) {
        const Ref<LocalMultiplayerPeer> joiner
            = mesh.session->create_client_peer();
        mesh.peers.push_back(joiner);
        CHECK(bool(joiner->get_unique_id() != successor_was));
        CHECK_FALSE(seen.has(joiner->get_unique_id()));
        seen.insert(joiner->get_unique_id());
    }
    mesh.session->poll();

    REQUIRE(handed_on(mesh, mesh.client(1)->get_unique_id(), true));
    mesh.session->poll();
    const int again = int(book.args(relabelled(), 1)[1]);
    CHECK(bool(again != 0));
    CHECK(bool(again != successor_was));
    CHECK_FALSE(seen.has(again));
}

} // namespace TestLoopbackTransferContractLaws
