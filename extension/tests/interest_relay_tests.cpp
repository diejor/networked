#include "support/netw_test.h"

#include "netw/interest_relay.hpp"

namespace TestNetwInterestRelay {

using namespace godot;
using netw::InterestAwareness;
using netw::InterestRelay;

constexpr int64_t LAYER = InterestAwareness::LAYER;
constexpr int64_t OBSERVER = InterestAwareness::OBSERVER;
constexpr int64_t ENTER = InterestAwareness::ENTER;
constexpr int64_t EXIT = InterestAwareness::EXIT;

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

InterestRelay fresh() {
    return InterestRelay();
}

bool decodes(const Variant &p_row) {
    InterestAwareness edge;
    return InterestAwareness::from_array(p_row, edge);
}

TEST_CASE(
    "[Networked][Interest][Hosted] an awareness row survives the round trip "
    "it was built for"
) {
    const InterestAwareness layer
        = InterestAwareness::layer_edge(7, StringName("arena"), ENTER);
    NETW_CHECK_EQ(layer.type, int(LAYER));
    NETW_CHECK_EQ(layer.route, 7);
    CHECK(layer.layer_id == StringName("arena"));
    NETW_CHECK_EQ(layer.observer_peer, 0);
    NETW_CHECK_EQ(layer.kind, int(ENTER));
    CHECK(layer.to_array() == good_layer_row());

    const InterestAwareness observer
        = InterestAwareness::observer_edge(7, StringName("arena"), 4, EXIT);
    NETW_CHECK_EQ(observer.type, int(OBSERVER));
    NETW_CHECK_EQ(observer.observer_peer, 4);

    InterestAwareness reread;
    CHECK(InterestAwareness::from_array(observer.to_array(), reread));
    CHECK(reread.to_array() == observer.to_array());
}

TEST_CASE(
    "[Networked][Interest][Hosted] a row that is not five typed fields is "
    "refused"
) {
    CHECK_FALSE(decodes(Variant()));
    CHECK_FALSE(decodes(int64_t(7)));
    CHECK_FALSE(decodes(Array()));

    Array short_row = good_layer_row();
    short_row.remove_at(4);
    CHECK_FALSE(decodes(short_row));

    Array long_row = good_layer_row();
    long_row.push_back(int64_t(0));
    CHECK_FALSE(decodes(long_row));

    Array string_layer = good_layer_row();
    string_layer[2] = String("arena");
    CHECK_FALSE(decodes(string_layer));

    Array float_route = good_layer_row();
    float_route[1] = 7.0;
    CHECK_FALSE(decodes(float_route));
}

TEST_CASE(
    "[Networked][Interest][Hosted] every field is refused outside its own "
    "range"
) {
    Array bad_type = good_layer_row();
    bad_type[0] = int64_t(2);
    CHECK_FALSE(decodes(bad_type));

    Array no_route = good_layer_row();
    no_route[1] = int64_t(0);
    CHECK_FALSE(decodes(no_route));

    Array back_route = good_layer_row();
    back_route[1] = int64_t(-1);
    CHECK_FALSE(decodes(back_route));

    Array no_layer = good_layer_row();
    no_layer[2] = StringName();
    CHECK_FALSE(decodes(no_layer));

    Array bad_kind = good_layer_row();
    bad_kind[4] = int64_t(2);
    CHECK_FALSE(decodes(bad_kind));

    CHECK(decodes(good_layer_row()));
}

TEST_CASE(
    "[Networked][Interest][Hosted] the observer field is refused unless it "
    "agrees with the kind of edge"
) {
    Array layer_naming_a_peer = good_layer_row();
    layer_naming_a_peer[3] = int64_t(4);
    CHECK_FALSE(decodes(layer_naming_a_peer));

    Array observer_naming_nobody
        = row(OBSERVER, 7, StringName("arena"), 0, ENTER);
    CHECK_FALSE(decodes(observer_naming_nobody));

    Array negative_observer = row(OBSERVER, 7, StringName("arena"), -1, ENTER);
    CHECK_FALSE(decodes(negative_observer));

    NETW_CHECK_EQ(
        InterestAwareness::observer_edge(7, StringName("arena"), 0, ENTER)
            .route,
        0
    );
}

TEST_CASE(
    "[Networked][Interest][Hosted] a peer's backlog keeps the order it "
    "happened in"
) {
    InterestRelay relay = fresh();
    CHECK(relay.is_empty());

    relay.append(
        4,
        InterestAwareness::layer_edge(7, StringName("a"), ENTER)
    );
    relay.append(
        9,
        InterestAwareness::layer_edge(7, StringName("a"), ENTER)
    );
    relay.append(
        4,
        InterestAwareness::layer_edge(7, StringName("a"), EXIT)
    );

    CHECK(!relay.is_empty());
    PackedInt64Array expected;
    expected.push_back(4);
    expected.push_back(9);
    CHECK(relay.targets() == expected);

    const Array owed = relay.wire_for(4);
    NETW_CHECK_EQ(owed.size(), 2);
    CHECK(owed[0] == row(LAYER, 7, StringName("a"), 0, ENTER));
    CHECK(owed[1] == row(LAYER, 7, StringName("a"), 0, EXIT));
    NETW_CHECK_EQ(relay.wire_for(11).size(), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a refused edge is queued for nobody"
) {
    InterestRelay relay = fresh();

    relay.append(
        4,
        InterestAwareness::layer_edge(0, StringName("a"), ENTER)
    );

    CHECK(relay.is_empty());
    NETW_CHECK_EQ(relay.targets().size(), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a forgotten peer is owed nothing and is "
    "named by nothing"
) {
    InterestRelay relay = fresh();
    relay.append(
        4,
        InterestAwareness::layer_edge(7, StringName("a"), ENTER)
    );
    relay.append(
        9,
        InterestAwareness::layer_edge(7, StringName("a"), ENTER)
    );

    relay.forget(4);
    relay.forget(11);

    PackedInt64Array expected;
    expected.push_back(9);
    CHECK(relay.targets() == expected);
    NETW_CHECK_EQ(relay.wire_for(4).size(), 0);

    relay.clear();
    CHECK(relay.is_empty());
    NETW_CHECK_EQ(relay.targets().size(), 0);
}

} // namespace TestNetwInterestRelay
