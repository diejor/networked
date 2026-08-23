#include "support/netw_test.h"

#include "netw/api/group_promise.hpp"
#include "support/netw_call_log.h"

namespace TestNetwGroupPromise {

using namespace godot;
using netw::NetwGroupPromise;
using netw_test::CallLog;

PackedInt32Array peers(int p_a, int p_b) {
    PackedInt32Array out;
    out.push_back(p_a);
    if (p_b != 0) {
        out.push_back(p_b);
    }
    return out;
}

TEST_CASE(
    "[Networked][Session][Hosted] a group settles once the last awaited peer "
    "answers, and never before"
) {
    const Ref<NetwGroupPromise> group = NetwGroupPromise::create(peers(2, 3));
    NETW_CHECK_EQ(group->get_expected_peers().size(), 2);
    NETW_CHECK_EQ(int(group->get_is_settled()), 0);

    group->resolve_peer(2, Variant(11));
    NETW_CHECK_EQ(int(group->get_is_settled()), 0);
    NETW_CHECK_EQ(group->get_expected_peers().size(), 1);
    NETW_CHECK_EQ(int(group->get_results().size()), 1);

    group->resolve_peer(3, Variant(22));
    NETW_CHECK_EQ(int(group->get_is_completed()), 1);
    NETW_CHECK_EQ(int(group->get_is_settled()), 1);
    NETW_CHECK_EQ(int(group->get_results().size()), 2);
    NETW_CHECK_EQ(int(group->get_results()[2]), 11);
    NETW_CHECK_EQ(int(group->get_results()[3]), 22);
}

TEST_CASE(
    "[Networked][Session][Hosted] a peer outside the awaited set answers for "
    "nobody"
) {
    const Ref<NetwGroupPromise> group = NetwGroupPromise::create(peers(2, 0));

    group->resolve_peer(9, Variant(11));
    NETW_CHECK_EQ(int(group->get_results().size()), 0);
    NETW_CHECK_EQ(int(group->get_is_settled()), 0);
    NETW_CHECK_EQ(group->get_expected_peers().size(), 1);

    group->remove_peer(9);
    NETW_CHECK_EQ(int(group->get_is_settled()), 0);

    group->remove_peer(2);
    NETW_CHECK_EQ(int(group->get_is_completed()), 1);
    NETW_CHECK_EQ(int(group->get_results().size()), 0);
}

TEST_CASE(
    "[Networked][Session][Hosted] a group that settled is an answer, so a "
    "second answer changes nothing"
) {
    const Ref<NetwGroupPromise> group = NetwGroupPromise::create(peers(2, 3));
    group->reject(int(ERR_TIMEOUT), String("no quorum"));

    NETW_CHECK_EQ(int(group->get_is_failed()), 1);
    NETW_CHECK_EQ(group->get_code(), int(ERR_TIMEOUT));

    group->resolve_peer(2, Variant(11));
    group->remove_peer(3);
    group->resolve_all();
    group->reject(int(ERR_BUSY), String("later"));

    NETW_CHECK_EQ(int(group->get_is_completed()), 0);
    NETW_CHECK_EQ(group->get_code(), int(ERR_TIMEOUT));
    NETW_CHECK_EQ(int(group->get_results().size()), 0);
}

TEST_CASE(
    "[Networked][Session][Hosted] a callback chained after a group settled "
    "fires immediately, so subscribing never races settling"
) {
    CallLog log;

    const Ref<NetwGroupPromise> group = NetwGroupPromise::create(peers(2, 0));
    group->then(log.callable("early"));
    NETW_CHECK_EQ(log.count("early"), 0);

    group->resolve_peer(2, Variant(11));
    NETW_CHECK_EQ(log.count("early"), 1);

    group->then(log.callable("late"));
    NETW_CHECK_EQ(log.count("late"), 1);

    group->catch_error(log.callable("never"));
    NETW_CHECK_EQ(log.count("never"), 0);
}

} // namespace TestNetwGroupPromise
