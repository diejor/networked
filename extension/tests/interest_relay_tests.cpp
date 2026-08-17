// The awareness relay's laws.
//
// The codec reads bytes a peer chose, so its cases are mostly refusals. Each
// one perturbs exactly one field of a row that is otherwise well formed, which
// is what keeps the case naming the check it is aiming at.

#include "support/netw_test.h"

#include "netw/interest_relay.hpp"

namespace TestNetwInterestRelay {

using namespace godot;
using netw::NetwInterestAwareness;
using netw::NetwInterestRelay;

constexpr int64_t LAYER = NetwInterestAwareness::LAYER;
constexpr int64_t OBSERVER = NetwInterestAwareness::OBSERVER;
constexpr int64_t ENTER = NetwInterestAwareness::ENTER;
constexpr int64_t EXIT = NetwInterestAwareness::EXIT;

Array row(
    int64_t type,
    int64_t route,
    const StringName &layer_id,
    int64_t observer_peer,
    int64_t kind
) {
    Array out;
    out.push_back(type);
    out.push_back(route);
    out.push_back(layer_id);
    out.push_back(observer_peer);
    out.push_back(kind);
    return out;
}

Array good_layer_row() {
    return row(LAYER, 7, StringName("arena"), 0, ENTER);
}

Ref<NetwInterestRelay> fresh() {
    Ref<NetwInterestRelay> relay;
    relay.instantiate();
    return relay;
}

TEST_CASE(
    "[Networked][Interest][Hosted] an awareness row survives the round trip "
    "it was built for"
) {
    const Ref<NetwInterestAwareness> layer
        = NetwInterestAwareness::layer_edge(7, StringName("arena"), ENTER);
    CHECK(layer.is_valid());
    NETW_CHECK_EQ(layer->get_edge_type(), int(LAYER));
    NETW_CHECK_EQ(layer->get_route(), 7);
    CHECK(layer->get_layer_id() == StringName("arena"));
    NETW_CHECK_EQ(layer->get_observer_peer(), 0);
    NETW_CHECK_EQ(layer->get_kind(), int(ENTER));
    CHECK(layer->to_array() == good_layer_row());

    const Ref<NetwInterestAwareness> observer
        = NetwInterestAwareness::observer_edge(7, StringName("arena"), 4, EXIT);
    CHECK(observer.is_valid());
    NETW_CHECK_EQ(observer->get_edge_type(), int(OBSERVER));
    NETW_CHECK_EQ(observer->get_observer_peer(), 4);

    const Ref<NetwInterestAwareness> reread
        = NetwInterestAwareness::from_array(observer->to_array());
    CHECK(reread.is_valid());
    CHECK(reread->to_array() == observer->to_array());
}

TEST_CASE(
    "[Networked][Interest][Hosted] a row that is not five typed fields is "
    "refused"
) {
    CHECK(NetwInterestAwareness::from_array(Variant()).is_null());
    CHECK(NetwInterestAwareness::from_array(int64_t(7)).is_null());
    CHECK(NetwInterestAwareness::from_array(Array()).is_null());

    Array short_row = good_layer_row();
    short_row.remove_at(4);
    CHECK(NetwInterestAwareness::from_array(short_row).is_null());

    Array long_row = good_layer_row();
    long_row.push_back(int64_t(0));
    CHECK(NetwInterestAwareness::from_array(long_row).is_null());

    Array string_layer = good_layer_row();
    string_layer[2] = String("arena");
    CHECK(NetwInterestAwareness::from_array(string_layer).is_null());

    Array float_route = good_layer_row();
    float_route[1] = 7.0;
    CHECK(NetwInterestAwareness::from_array(float_route).is_null());
}

TEST_CASE(
    "[Networked][Interest][Hosted] every field is refused outside its own "
    "range"
) {
    Array bad_type = good_layer_row();
    bad_type[0] = int64_t(2);
    CHECK(NetwInterestAwareness::from_array(bad_type).is_null());

    Array no_route = good_layer_row();
    no_route[1] = int64_t(0);
    CHECK(NetwInterestAwareness::from_array(no_route).is_null());

    Array back_route = good_layer_row();
    back_route[1] = int64_t(-1);
    CHECK(NetwInterestAwareness::from_array(back_route).is_null());

    Array no_layer = good_layer_row();
    no_layer[2] = StringName();
    CHECK(NetwInterestAwareness::from_array(no_layer).is_null());

    Array bad_kind = good_layer_row();
    bad_kind[4] = int64_t(2);
    CHECK(NetwInterestAwareness::from_array(bad_kind).is_null());

    CHECK(NetwInterestAwareness::from_array(good_layer_row()).is_valid());
}

TEST_CASE(
    "[Networked][Interest][Hosted] the observer field is refused unless it "
    "agrees with the kind of edge"
) {
    // The two halves of this rule cannot be satisfied separately: an edge that
    // names nobody is a layer edge, and one that names somebody is an observer
    // edge, so a row disagreeing with itself is not a lesser mistake than a
    // malformed one.
    Array layer_naming_a_peer = good_layer_row();
    layer_naming_a_peer[3] = int64_t(4);
    CHECK(NetwInterestAwareness::from_array(layer_naming_a_peer).is_null());

    Array observer_naming_nobody
        = row(OBSERVER, 7, StringName("arena"), 0, ENTER);
    CHECK(NetwInterestAwareness::from_array(observer_naming_nobody).is_null());

    Array negative_observer = row(OBSERVER, 7, StringName("arena"), -1, ENTER);
    CHECK(NetwInterestAwareness::from_array(negative_observer).is_null());

    CHECK(NetwInterestAwareness::observer_edge(7, StringName("arena"), 0, ENTER)
              .is_null());
}

TEST_CASE(
    "[Networked][Interest][Hosted] a peer's backlog keeps the order it "
    "happened in"
) {
    const Ref<NetwInterestRelay> relay = fresh();
    CHECK(relay->is_empty());

    relay->append(
        4,
        NetwInterestAwareness::layer_edge(7, StringName("a"), ENTER)
    );
    relay->append(
        9,
        NetwInterestAwareness::layer_edge(7, StringName("a"), ENTER)
    );
    relay->append(
        4,
        NetwInterestAwareness::layer_edge(7, StringName("a"), EXIT)
    );

    CHECK(!relay->is_empty());
    PackedInt64Array expected;
    expected.push_back(4);
    expected.push_back(9);
    CHECK(relay->targets() == expected);

    const Array owed = relay->wire_for(4);
    NETW_CHECK_EQ(owed.size(), 2);
    CHECK(owed[0] == row(LAYER, 7, StringName("a"), 0, ENTER));
    CHECK(owed[1] == row(LAYER, 7, StringName("a"), 0, EXIT));
    NETW_CHECK_EQ(relay->wire_for(11).size(), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a refused edge is queued for nobody"
) {
    const Ref<NetwInterestRelay> relay = fresh();

    relay->append(
        4,
        NetwInterestAwareness::layer_edge(0, StringName("a"), ENTER)
    );

    CHECK(relay->is_empty());
    NETW_CHECK_EQ(relay->targets().size(), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a forgotten peer is owed nothing and is "
    "named by nothing"
) {
    const Ref<NetwInterestRelay> relay = fresh();
    relay->append(
        4,
        NetwInterestAwareness::layer_edge(7, StringName("a"), ENTER)
    );
    relay->append(
        9,
        NetwInterestAwareness::layer_edge(7, StringName("a"), ENTER)
    );

    relay->forget(4);
    relay->forget(11);

    PackedInt64Array expected;
    expected.push_back(9);
    CHECK(relay->targets() == expected);
    NETW_CHECK_EQ(relay->wire_for(4).size(), 0);

    relay->clear();
    CHECK(relay->is_empty());
    NETW_CHECK_EQ(relay->targets().size(), 0);
}

} // namespace TestNetwInterestRelay
