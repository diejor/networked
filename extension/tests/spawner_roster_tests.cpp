#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/spawner_roster.hpp"

namespace TestNetwSpawnerRoster {

using namespace godot;
using netw::NetwSpawnerRoster;

Ref<NetwSpawnerRoster> fresh() {
    Ref<NetwSpawnerRoster> roster;
    roster.instantiate();
    return roster;
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a spawner is enrolled once, and one that is "
    "gone drops out as the roster is read"
) {
    const Ref<NetwSpawnerRoster> roster = fresh();
    Node *first = memnew(Node);
    Node *second = memnew(Node);

    CHECK(roster->enrol(first));
    CHECK_FALSE(roster->enrol(first));
    CHECK(roster->enrol(second));
    CHECK_FALSE(roster->enrol(nullptr));
    NETW_CHECK_EQ(roster->size(), 2);
    NETW_CHECK_EQ(roster->live().size(), 2);

    memdelete(second);

    NETW_CHECK_EQ(roster->live().size(), 1);
    NETW_CHECK_EQ(roster->size(), 1);
    CHECK(Object::cast_to<Node>(roster->live()[0]) == first);

    memdelete(first);
    NETW_CHECK_EQ(roster->live().size(), 0);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a route's producing spawner is taken by the "
    "despawn that needs it"
) {
    const Ref<NetwSpawnerRoster> roster = fresh();
    Node *spawner = memnew(Node);

    CHECK(roster->take_producer(31) == nullptr);
    roster->note_producer(31, spawner);
    NETW_CHECK_EQ(roster->produced_count(spawner), 1);

    CHECK(roster->take_producer(31) == spawner);
    CHECK(roster->take_producer(31) == nullptr);
    NETW_CHECK_EQ(roster->produced_count(spawner), 0);

    memdelete(spawner);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] the produced count is per spawner, and a "
    "spawner that produced nothing counts nothing"
) {
    const Ref<NetwSpawnerRoster> roster = fresh();
    Node *busy = memnew(Node);
    Node *quiet = memnew(Node);

    roster->note_producer(31, busy);
    roster->note_producer(32, busy);
    roster->note_producer(33, quiet);

    NETW_CHECK_EQ(roster->produced_count(busy), 2);
    NETW_CHECK_EQ(roster->produced_count(quiet), 1);
    NETW_CHECK_EQ(roster->produced_count(nullptr), 0);

    roster->clear();

    NETW_CHECK_EQ(roster->produced_count(busy), 0);
    NETW_CHECK_EQ(roster->size(), 0);

    memdelete(busy);
    memdelete(quiet);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a producing spawner that is gone is answered "
    "as nothing rather than as an instance"
) {
    const Ref<NetwSpawnerRoster> roster = fresh();
    Node *spawner = memnew(Node);
    roster->note_producer(31, spawner);
    memdelete(spawner);

    CHECK(roster->take_producer(31) == nullptr);
    CHECK(roster->take_producer(31) == nullptr);
}

} // namespace TestNetwSpawnerRoster
