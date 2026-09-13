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
    group->reject(ERR_TIMEOUT, String("no quorum"));

    NETW_CHECK_EQ(int(group->get_is_failed()), 1);
    NETW_CHECK_EQ(group->get_code(), int(ERR_TIMEOUT));

    group->resolve_peer(2, Variant(11));
    group->remove_peer(3);
    group->resolve_all();
    group->reject(ERR_BUSY, String("later"));

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

TEST_CASE(
    "[Networked][Session][Hosted] GW1 a settled batch answers its results "
    "when every peer arrived and its code when it failed, so one channel "
    "carries both outcomes"
) {
    const Ref<NetwGroupPromise> done = NetwGroupPromise::create(peers(1, 0));
    done->resolve_peer(1, Variant(7));
    CHECK(done->answer().get_type() == Variant::DICTIONARY);

    const Ref<NetwGroupPromise> bad = NetwGroupPromise::create(peers(1, 0));
    bad->reject(ERR_TIMEOUT, String());
    CHECK(bad->answer() == Variant(int(ERR_TIMEOUT)));
}

TEST_CASE(
    "[Networked][Session][Hosted] GW2 a pending batch emits ready once on "
    "its settle edge and every waiter is listening to that one emission"
) {
    const CallLog answer;

    const Ref<NetwGroupPromise> batch = NetwGroupPromise::create(peers(1, 2));
    batch->wait();
    batch->connect(StringName("ready"), answer.callable("first"));
    batch->connect(StringName("ready"), answer.callable("second"));

    batch->resolve_peer(1, Variant(7));
    NETW_CHECK_EQ(answer.count("first"), 0);

    batch->resolve_peer(2, Variant(7));
    NETW_CHECK_EQ(answer.count("first"), 1);
    NETW_CHECK_EQ(answer.count("second"), 1);
}

TEST_CASE(
    "[Networked][Session][Hosted] GW3 wait() on a settled batch notifies "
    "nobody during the call itself, so a caller that waits after the answer "
    "arrived still subscribes before it is delivered"
) {
    const CallLog answer;

    const Ref<NetwGroupPromise> batch = NetwGroupPromise::create(peers(1, 0));
    batch->connect(StringName("settled"), answer.callable("settled"));
    batch->resolve_peer(1, Variant(7));
    NETW_CHECK_EQ(answer.count("settled"), 1);

    batch->wait();
    batch->wait();

    NETW_CHECK_EQ(answer.count("settled"), 1);
}

} // namespace TestNetwGroupPromise
