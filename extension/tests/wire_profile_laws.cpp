#include "support/netw_test.h"

#include "netw/store/validate.hpp"
#include "netw/wire/profiler.hpp"

#if defined(NETW_TIER_HOSTED)
#include "support/alloc_probe.h"
#endif

#if defined(DEBUG_ENABLED)

#include "godot/node.hpp"

namespace TestWireProfileLaws {

using namespace godot;
using netw::wire::NetworkProfiler;
using netw::wire::ProfileArrivalScope;
using netw::wire::ProfileKey;
using netw::wire::WireRegistry;

struct Recording {
    NetworkProfiler book;

    Recording() {
        book.activate(true, true);
        book.prepare(WireRegistry::create_default());
    }

    uint32_t subject(int64_t p_owner = 1) {
        ProfileKey key;
        key.owner = p_owner;
        key.channel = 3;
        key.member = "shoot";
        return book.admit(key, "Car.shoot()");
    }
};

TEST_CASE(
    "[Networked][Wire][Hosted] profile intervals retain frame counts and "
    "byte sums and spend only the emitted interval"
) {
    Recording recording;
    NetworkProfiler &book = recording.book;
    const uint32_t subject = recording.subject();
    const uint32_t epoch = book.epoch();
    book.observe(subject, epoch, 2, true, 23);
    book.observe(subject, epoch, 3, true, 37);
    book.observe(subject, epoch, 2, false, 41);
    Array rpc;
    Array sync;
    book.collect(rpc, sync);
    NETW_CHECK_EQ(rpc.size(), 7);
    NETW_CHECK_EQ(int64_t(rpc[0]), int64_t(6));
    NETW_CHECK_EQ(int64_t(rpc[3]), int64_t(2));
    NETW_CHECK_EQ(int64_t(rpc[4]), int64_t(60));
    NETW_CHECK_EQ(int64_t(rpc[5]), int64_t(1));
    NETW_CHECK_EQ(int64_t(rpc[6]), int64_t(41));
    const int64_t identity = rpc[1];
    book.collect(rpc, sync);
    NETW_CHECK_EQ(rpc.size(), 1);
    book.observe(subject, epoch, 2, false, 13);
    book.collect(rpc, sync);
    NETW_CHECK_EQ(int64_t(rpc[1]), identity);
    NETW_CHECK_EQ(int64_t(rpc[5]), int64_t(1));
    NETW_CHECK_EQ(int64_t(rpc[6]), int64_t(13));
}

TEST_CASE(
    "[Networked][Wire][Hosted] profile activation isolates staged epochs "
    "and keeps row identity and the lifetime subject budget"
) {
    Recording recording;
    NetworkProfiler &book = recording.book;
    const uint32_t subject = recording.subject();
    const uint32_t old_epoch = book.epoch();
    book.observe(subject, old_epoch, 2, false, 20);
    book.activate(false, false);
    book.observe(subject, old_epoch, 2, false, 20);
    book.activate(true, true);
    book.observe(subject, old_epoch, 2, false, 20);
    Array rpc;
    Array sync;
    book.collect(rpc, sync);
    NETW_CHECK_EQ(rpc.size(), 1);
    NETW_CHECK_EQ(recording.subject(), subject);
    NETW_CHECK_EQ(book.subject_count(), uint32_t(1));
    book.observe(subject, book.epoch(), 2, false, 30);
    book.collect(rpc, sync);
    NETW_CHECK_EQ(int64_t(rpc[5]), int64_t(1));
}

TEST_CASE(
    "[Networked][Wire][Hosted] physical profile arrivals count once "
    "across nested dispatch and later deferred replay"
) {
    Recording recording;
    NetworkProfiler &book = recording.book;
    const uint32_t first = recording.subject();
    const uint32_t second = recording.subject(2);
    {
        ProfileArrivalScope outer(book, true, 2, 50, first);
        {
            ProfileArrivalScope inner(book, true, 3, 70, second);
        }
        book.name_arrival(first);
    }
    book.name_arrival(second);
    {
        ProfileArrivalScope local(book, false, 2, 90, first);
    }
    {
        ProfileArrivalScope suppressed(book, true, 2, 80, first);
        book.name_arrival(NetworkProfiler::SKIP);
    }
    NETW_CHECK_EQ(book.row(first)->incoming_count, int64_t(1));
    NETW_CHECK_EQ(book.row(first)->incoming_bytes, int64_t(50));
    NETW_CHECK_EQ(book.row(second)->incoming_count, int64_t(1));
    NETW_CHECK_EQ(book.row(second)->incoming_bytes, int64_t(70));
}

TEST_CASE(
    "[Networked][Wire][Hosted] profile churn stays bounded and existing "
    "subjects stay detailed when the lifetime budget is exhausted"
) {
    Recording recording;
    NetworkProfiler &book = recording.book;
    const uint32_t first = recording.subject();
    for (uint32_t at = 2; at <= NetworkProfiler::SUBJECT_LIMIT; ++at) {
        recording.subject(at);
    }
    NETW_CHECK_EQ(book.subject_count(), NetworkProfiler::SUBJECT_LIMIT);
    NETW_CHECK_EQ(recording.subject(), first);
    const uint32_t overflow = recording.subject(999999);
    NETW_CHECK_EQ(overflow, book.aggregate(3));
    NETW_CHECK_EQ(book.row_count(), NetworkProfiler::SUBJECT_LIMIT + 258);
    CHECK(book.row(overflow)->label.begins_with("(other) "));
}

TEST_CASE(
    "[Networked][Wire][Hosted] reply correlation reuses bounded storage "
    "and keeps peer and request direction separate under churn"
) {
    Recording recording;
    NetworkProfiler &book = recording.book;
    const uint32_t call = recording.subject();
    book.remember_reply(2, 1, false, call);
    const uint32_t reply = book.take_reply(2, 1, false);
    CHECK(book.row(reply)->label == String("Car.shoot() reply"));
    for (int64_t txn = 2; txn < 40000; ++txn) {
        book.remember_reply(2, txn, false, call);
        book.remember_reply(3, txn, true, call);
        if (book.take_reply(2, txn, false) != reply
            || book.take_reply(3, txn, true) != reply) {
            FAIL("reply identity changed under request churn");
            break;
        }
    }
    NETW_CHECK_EQ(book.subject_count(), uint32_t(2));
    book.remember_reply(2, 40000, false, call);
    book.remember_reply(3, 40000, false, call);
    book.forget_peer(2);
    NETW_CHECK_EQ(book.take_reply(2, 40000, false), book.aggregate(4));
    NETW_CHECK_EQ(book.take_reply(3, 40000, false), reply);
    book.remember_reply(2, 40001, false, call);
    book.forget_transaction(40001);
    NETW_CHECK_EQ(book.take_reply(2, 40001, false), book.aggregate(4));
}

TEST_CASE(
    "[Networked][Wire][Hosted] prepared profile counters and recurring "
    "request correlation allocate nothing through the extension allocator"
) {
    Recording recording;
    NetworkProfiler &book = recording.book;
    const uint32_t call = recording.subject();
    book.remember_reply(2, 1, false, call);
    const uint32_t reply = book.take_reply(2, 1, false);
#if defined(NETW_TIER_HOSTED)
    netw_test::AllocProbe probe;
    probe.arm();
#endif
    const int64_t before = netw::store::allocations_seen();
    for (int64_t txn = 2; txn < 10002; ++txn) {
        book.observe(call, book.epoch(), 2, false, 32);
        book.remember_reply(2, txn, false, call);
        const uint32_t found = book.take_reply(2, txn, false);
        book.observe(found, book.epoch(), 2, true, 24);
    }
    const int64_t allocated = netw::store::allocations_seen() - before;
#if defined(NETW_TIER_HOSTED)
    probe.disarm();
#endif
    NETW_CHECK_EQ(allocated, int64_t(0));
    NETW_CHECK_EQ(book.row(call)->outgoing_count, int64_t(10000));
    NETW_CHECK_EQ(book.row(reply)->incoming_count, int64_t(10000));
}

TEST_CASE(
    "[Networked][Wire][Hosted] synchronizer profile rows carry real "
    "object IDs and stay separate from named traffic"
) {
    Recording recording;
    NetworkProfiler &book = recording.book;
    Node *sync_node = memnew(Node);
    Node *root_node = memnew(Node);
    Ref<RefCounted> config;
    config.instantiate();
    const ObjectID sync_id = ObjectID(uint64_t(sync_node->get_instance_id()));
    const ObjectID root_id = ObjectID(uint64_t(root_node->get_instance_id()));
    const ObjectID config_id(uint64_t(config->get_instance_id()));
    const uint32_t subject = book.synchronizer(1, sync_id, root_id, config_id);
    book.observe(subject, book.epoch(), 2, false, 60);
    Array rpc;
    Array sync;
    book.collect(rpc, sync);
    NETW_CHECK_EQ(rpc.size(), 1);
    NETW_CHECK_EQ(sync.size(), 8);
    NETW_CHECK_EQ(int64_t(sync[1]), int64_t(uint64_t(sync_id)));
    NETW_CHECK_EQ(int64_t(sync[2]), int64_t(uint64_t(config_id)));
    NETW_CHECK_EQ(int64_t(sync[3]), int64_t(uint64_t(root_id)));
    NETW_CHECK_EQ(int64_t(sync[6]), int64_t(1));
    NETW_CHECK_EQ(int64_t(sync[7]), int64_t(60));
    memdelete(sync_node);
    memdelete(root_node);
}

} // namespace TestWireProfileLaws

#endif
