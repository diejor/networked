#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include <memory>

#include <godot_cpp/classes/scene_multiplayer.hpp>

#include "godot/callable.hpp"
#include "godot/scene_tree.hpp"
#include "godot/script.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwFrameServiceOrder {

using namespace godot;
using netw::LocalLoopbackSession;
using netw::LocalMultiplayerPeer;
using netw::NetwMultiplayer;

class QueueProbe final : public CallableCustom {
    std::shared_ptr<Vector<int>> samples;
    Ref<LocalMultiplayerPeer> watched;
    ObjectID anchor;

    static bool same(const CallableCustom *a, const CallableCustom *b) {
        return a == b;
    }

    static bool before(const CallableCustom *a, const CallableCustom *b) {
        return a < b;
    }

public:
    QueueProbe(
        const std::shared_ptr<Vector<int>> &p_samples,
        const Ref<LocalMultiplayerPeer> &p_watched,
        const Object *p_anchor
    )
        : samples(p_samples), watched(p_watched),
          anchor(netw::gd::instance_id(p_anchor)) {
    }

    uint32_t hash() const override {
        return uint32_t(uintptr_t(this));
    }

    String get_as_text() const override {
        return String("NetwQueueProbe");
    }

    CompareEqualFunc get_compare_equal_func() const override {
        return &QueueProbe::same;
    }

    CompareLessFunc get_compare_less_func() const override {
        return &QueueProbe::before;
    }

    ObjectID get_object() const override {
        return anchor;
    }

    void call(
        const Variant **,
        int,
        Variant &r_return_value,
        netw::gd::CallError &r_call_error
    ) const override {
        samples->push_back(int(watched->get_available_packet_count()));
        r_return_value = Variant();
        netw::gd::call_ok(r_call_error);
    }
};

Ref<NetwMultiplayer> rooted_session(const Ref<LocalMultiplayerPeer> &p_peer) {
    Ref<SceneMultiplayer> inner;
    inner.instantiate();
    inner->set_root_path(netw::gd::scene_root()->get_path());
    const Ref<NetwMultiplayer> session
        = NetwMultiplayer::make(inner, Ref<Script>());
    if (session.is_valid()) {
        session->set("multiplayer_peer", p_peer);
    }
    return session;
}

PackedByteArray a_foreign_packet() {
    PackedByteArray bytes;
    bytes.resize(4);
    bytes.set(0, 'G');
    bytes.set(1, 'A');
    bytes.set(2, 'M');
    bytes.set(3, 'E');
    return bytes;
}

TEST_CASE(
    "[Networked][Session][FrameOrder] FO1 a poll announces itself while the "
    "packets the carrier delivered are still unread, so anything serviced "
    "from that announcement lands in the very read that follows it rather "
    "than a frame later"
) {
    Ref<LocalLoopbackSession> bus;
    bus.instantiate();
    const Ref<LocalMultiplayerPeer> server_peer = bus->get_server_peer();
    const Ref<LocalMultiplayerPeer> client_peer = bus->create_client_peer();
    bus->poll();

    const Ref<NetwMultiplayer> host = rooted_session(server_peer);
    NETW_CHECK_EQ(int(host.is_valid()), 1);
    if (host.is_null()) {
        return;
    }

    client_peer->set_target_peer(1);
    client_peer->put_packet(a_foreign_packet());
    bus->poll();
    NETW_CHECK_EQ(int(server_peer->get_available_packet_count() > 0), 1);

    Ref<RefCounted> anchor;
    anchor.instantiate();
    const std::shared_ptr<Vector<int>> samples
        = std::make_shared<Vector<int>>();
    host->connect(
        StringName("session_poll_started"),
        Callable(memnew(QueueProbe(samples, server_peer, anchor.ptr())))
    );

    host->poll();

    NETW_CHECK_EQ(int(samples->size()), 1);
    if (samples->is_empty()) {
        return;
    }
    NETW_CHECK_EQ(int((*samples)[0] > 0), 1);
    NETW_CHECK_EQ(int(server_peer->get_available_packet_count()), 0);

    host->embed_dispose();
    bus->reset();
}

} // namespace TestNetwFrameServiceOrder

#endif
