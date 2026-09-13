#include "support/netw_test.h"

#include "netw/settle_queue.hpp"
#include "support/netw_call_log.h"

#include <memory>

#include "godot/callable.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace TestNetwSettleQueue {

using namespace godot;
using netw::SettleQueue;
using netw_test::CallLog;

// An effect that puts something back on the queue from inside the pass that is
// running it. Nothing else distinguishes a drain that takes the whole queue per
// pass from one that walks a list it is still being appended to, and a
// `again` holding this sink's own callable is the only way to write a cycle.
class Rescheduler final : public CallableCustom {
    SettleQueue *queue;
    Callable mark;
    std::shared_ptr<Callable> again;
    StringName key;
    ObjectID anchor;

    static bool same(const CallableCustom *a, const CallableCustom *b) {
        return a == b;
    }

    static bool before(const CallableCustom *a, const CallableCustom *b) {
        return a < b;
    }

public:
    Rescheduler(
        SettleQueue *p_queue,
        const Callable &p_mark,
        const std::shared_ptr<Callable> &p_again,
        const StringName &p_key,
        const Object *p_anchor
    )
        : queue(p_queue), mark(p_mark), again(p_again), key(p_key),
          anchor(netw::gd::instance_id(p_anchor)) {
    }

    uint32_t hash() const override {
        return uint32_t(uintptr_t(this));
    }

    String get_as_text() const override {
        return String("Rescheduler");
    }

    CompareEqualFunc get_compare_equal_func() const override {
        return &Rescheduler::same;
    }

    CompareLessFunc get_compare_less_func() const override {
        return &Rescheduler::before;
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
        if (mark.is_valid()) {
            mark.call();
        }
        if (again && !again->is_null()) {
            queue->schedule(*again, key);
        }
        r_return_value = Variant();
        netw::gd::call_ok(r_call_error);
    }
};

TEST_CASE("[Networked][Settle][Hosted] unkeyed effects run in enqueue order") {
    SettleQueue queue;
    CallLog log;

    queue.schedule(log.callable("first"), StringName());
    queue.schedule(log.callable("second"), StringName());
    queue.schedule(log.callable("third"), StringName());

    NETW_CHECK_EQ(queue.size(), 3);
    NETW_CHECK_EQ(queue.drain().size(), 0);

    CHECK(log.order() == Vector<StringName>({"first", "second", "third"}));
    NETW_CHECK_EQ(queue.is_empty(), true);
}

TEST_CASE("[Networked][Settle][Hosted] a named key coalesces to the back") {
    SettleQueue queue;
    CallLog log;

    queue.schedule(log.callable("keyed"), "k");
    queue.schedule(log.callable("other"), StringName());
    queue.schedule(log.callable("keyed-again"), "k");

    NETW_CHECK_EQ(queue.size(), 2);
    queue.drain();

    CHECK(log.order() == Vector<StringName>({"other", "keyed-again"}));
    NETW_CHECK_EQ(log.count("keyed"), 0);
}

TEST_CASE(
    "[Networked][Settle][Hosted] cancelling an unqueued key is no error"
) {
    SettleQueue queue;
    CallLog log;

    queue.cancel("never-queued");
    queue.schedule(log.callable("kept"), "keep");
    queue.schedule(log.callable("dropped"), "drop");

    NETW_CHECK_EQ(queue.has("drop"), true);
    queue.cancel("drop");
    NETW_CHECK_EQ(queue.has("drop"), false);
    NETW_CHECK_EQ(queue.has("keep"), true);

    queue.drain();

    NETW_CHECK_EQ(log.count("kept"), 1);
    NETW_CHECK_EQ(log.count("dropped"), 0);
}

TEST_CASE("[Networked][Settle][Hosted] an effect scheduled mid-pass waits") {
    SettleQueue queue;
    CallLog log;
    Ref<RefCounted> anchor;
    anchor.instantiate();

    std::shared_ptr<Callable> deferred
        = std::make_shared<Callable>(log.callable("scheduled-during"));
    queue.schedule(
        Callable(memnew(Rescheduler(
            &queue,
            Callable(),
            deferred,
            StringName(),
            anchor.ptr()
        ))),
        StringName()
    );
    queue.schedule(log.callable("same-pass"), StringName());

    NETW_CHECK_EQ(queue.drain().size(), 0);

    // What the first row scheduled runs AFTER the row already queued beside it,
    // which is what "the next pass of the same drain" means.
    CHECK(log.order() == Vector<StringName>({"same-pass", "scheduled-during"}));
}

TEST_CASE(
    "[Networked][Settle][Hosted] a keyed cycle reports rather than hangs"
) {
    SettleQueue queue;
    CallLog log;
    Ref<RefCounted> anchor;
    anchor.instantiate();

    std::shared_ptr<Callable> again = std::make_shared<Callable>();
    const Callable cycle = Callable(memnew(
        Rescheduler(&queue, log.callable("pass"), again, "cycle", anchor.ptr())
    ));
    *again = cycle;
    queue.schedule(cycle, "cycle");

    const PackedStringArray pending = queue.drain();

    NETW_CHECK_EQ(pending.size(), 1);
    CHECK(pending[0] == String("cycle"));
    // Bounded, so the body ran the bound rather than whatever the machine had
    // time for, and the queue is left empty rather than poisoned for the pump
    // that comes after it.
    NETW_CHECK_EQ(log.count("pass"), SettleQueue::MAX_PASSES);
    NETW_CHECK_EQ(queue.is_empty(), true);
}

TEST_CASE("[Networked][Settle][Hosted] an unkeyed cycle names itself") {
    SettleQueue queue;
    CallLog log;
    Ref<RefCounted> anchor;
    anchor.instantiate();

    std::shared_ptr<Callable> again = std::make_shared<Callable>();
    const Callable cycle = Callable(memnew(
        Rescheduler(&queue, Callable(), again, StringName(), anchor.ptr())
    ));
    *again = cycle;
    queue.schedule(cycle, StringName());

    const PackedStringArray pending = queue.drain();

    NETW_CHECK_EQ(pending.size(), 1);
    CHECK(pending[0] == String("<unkeyed>"));
}

TEST_CASE(
    "[Networked][Settle][Hosted] a window is spent by pumps, not drains"
) {
    SettleQueue queue;
    CallLog log;
    queue.schedule_after(log.callable("freed"), StringName(), 2);

    // A clocked session drains on its polls as well as its ticks, so a window
    // that a drain spent would close in half the time it was asked for.
    queue.drain();
    queue.drain();
    NETW_CHECK_EQ(log.count("freed"), 0);

    queue.advance_windows();
    queue.drain();
    NETW_CHECK_EQ(log.count("freed"), 0);

    queue.advance_windows();
    queue.drain();
    NETW_CHECK_EQ(log.count("freed"), 1);
    NETW_CHECK_EQ(queue.size(), 0);
}

TEST_CASE("[Networked][Settle][Hosted] an open window is not a cycle") {
    SettleQueue queue;
    CallLog log;
    queue.schedule_after(log.callable("later"), StringName("later"), 4);
    queue.schedule(log.callable("now"), StringName());

    const PackedStringArray pending = queue.drain();

    // The drain reached a fixed point: the held row is waiting rather than
    // failing to settle, so reporting it would name a cycle that is not one,
    // and dropping it would cancel a window nobody closed.
    NETW_CHECK_EQ(pending.size(), 0);
    NETW_CHECK_EQ(log.count("now"), 1);
    NETW_CHECK_EQ(queue.has(StringName("later")), true);
}

TEST_CASE("[Networked][Settle][Hosted] a window survives a reported cycle") {
    SettleQueue queue;
    CallLog log;
    queue.schedule_after(log.callable("later"), StringName("later"), 4);
    Ref<RefCounted> anchor;
    anchor.instantiate();
    std::shared_ptr<Callable> again = std::make_shared<Callable>();
    const Callable spin = Callable(memnew(
        Rescheduler(&queue, log.callable("spun"), again, "spin", anchor.ptr())
    ));
    *again = spin;
    queue.schedule(spin, "spin");

    const PackedStringArray pending = queue.drain();

    NETW_CHECK_EQ(pending.size(), 1);
    NETW_CHECK_EQ(log.count("spun"), SettleQueue::MAX_PASSES);
    // The cycle's rows are dropped and the window's is not, because the drain
    // gave up on the cascade rather than on the session.
    NETW_CHECK_EQ(queue.has(StringName("later")), true);
    NETW_CHECK_EQ(log.count("later"), 0);
}

TEST_CASE("[Networked][Settle][Hosted] a re-declared window replaces the old") {
    SettleQueue queue;
    CallLog log;
    queue.schedule_after(log.callable("first"), StringName("free"), 1);
    queue.schedule_after(log.callable("second"), StringName("free"), 3);

    queue.advance_windows();
    queue.drain();

    // A key coalesces whether or not it carries a window, so a re-declared
    // teardown waits out the window it was last given rather than the shortest
    // one anybody ever asked for.
    NETW_CHECK_EQ(log.count("first"), 0);
    NETW_CHECK_EQ(log.count("second"), 0);
    NETW_CHECK_EQ(queue.size(), 1);
}

} // namespace TestNetwSettleQueue
