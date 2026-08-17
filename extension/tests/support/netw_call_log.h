#pragma once

/* Which callbacks ran, in what order.
 *
 * The recorder's counterpart. `Recorder` captures what an object EMITTED;
 * this captures what a `Callable` handed to the subject was CALLED with, which
 * is the only observation available for a surface whose whole contract is
 * "I will call you back". A queue that answered the wrong waiter, answered one
 * twice, or answered in the wrong order is indistinguishable from a correct one
 * by any amount of polled state.
 *
 * Every callable a log mints writes into the same ordered list, so order across
 * DIFFERENT callbacks is recorded the same way order across different signals
 * is.
 *
 * [codeblock]
 * CallLog log;
 * core->when_live(7, log.callable("live"), 4, false, log.callable("expired"));
 * core->flush_live(7);
 * NETW_CHECK_EQ(log.count("live"), 1);
 * CHECK(log.order() == Vector<StringName>({ "live" }));
 * [/codeblock]
 */

// A sibling under `support/`, so the prelude is reached by its bare name here.
// A case file, which sits one directory up, writes `support/netw_test.h`.
#include "netw_test.h"

#include <memory>

#include "godot/callable.hpp"
#include "godot/ref_counted.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"

namespace netw_test {

// Held through a shared pointer because a callable handed to a subject is owned
// by whatever holds it and may outlive the log that minted it.
struct CallLogEntries {
    godot::Vector<godot::StringName> tags;
    // Parallel to tags, so a call's arguments are found under the same index
    // that gives its order. An observation noted by the case carries none.
    godot::Vector<godot::Array> args;
};

// One per minted callable. It knows its own tag, so the shared list records
// arrival order across all of them.
class CallLogSink final : public godot::CallableCustom {
    std::shared_ptr<CallLogEntries> entries;
    godot::StringName tag;
    godot::ObjectID anchor;
    // What the sink answers. A driver callback is read for its return value,
    // so a sink that always answered nil could not stand in for one.
    godot::Variant answer;
    // A factory answers a NEW object per call, which is what makes "minted
    // once" observable: a fixed answer passes that law without holding it.
    bool mints = false;

    static bool same(
        const godot::CallableCustom *a,
        const godot::CallableCustom *b
    ) {
        return a == b;
    }

    static bool before(
        const godot::CallableCustom *a,
        const godot::CallableCustom *b
    ) {
        return a < b;
    }

public:
    CallLogSink(
        const std::shared_ptr<CallLogEntries> &p_entries,
        const godot::StringName &p_tag,
        const godot::Object *p_anchor,
        const godot::Variant &p_answer = godot::Variant(),
        bool p_mints = false
    )
        : entries(p_entries), tag(p_tag),
          anchor(netw::gd::instance_id(p_anchor)), answer(p_answer),
          mints(p_mints) {
    }

    uint32_t hash() const override {
        return uint32_t(uintptr_t(this));
    }

    godot::String get_as_text() const override {
        return godot::String("NetwCallLog::") + godot::String(tag);
    }

    CompareEqualFunc get_compare_equal_func() const override {
        return &CallLogSink::same;
    }

    CompareLessFunc get_compare_less_func() const override {
        return &CallLogSink::before;
    }

    // A custom callable with no object reads as INVALID, and a subject that
    // skips invalid callbacks then skips every one of these silently. The
    // anchor is the log's own object, so a minted callable is valid for exactly
    // as long as the log that answers questions about it.
    godot::ObjectID get_object() const override {
        return anchor;
    }

    void call(
        const godot::Variant **p_arguments,
        int p_count,
        godot::Variant &r_return_value,
        netw::gd::CallError &r_call_error
    ) const override {
        godot::Array carried;
        for (int index = 0; index < p_count; ++index) {
            carried.push_back(*p_arguments[index]);
        }
        entries->tags.push_back(tag);
        entries->args.push_back(carried);
        if (mints) {
            godot::Ref<godot::RefCounted> fresh;
            fresh.instantiate();
            r_return_value = fresh;
        } else {
            r_return_value = answer;
        }
        netw::gd::call_ok(r_call_error);
    }
};

class CallLog {
    std::shared_ptr<CallLogEntries> entries;
    godot::Ref<godot::RefCounted> anchor;

public:
    CallLog() : entries(std::make_shared<CallLogEntries>()) {
        anchor.instantiate();
    }

    // A fresh callable that writes p_tag into this log when it is called.
    godot::Callable callable(const godot::StringName &p_tag) const {
        return godot::Callable(
            memnew(CallLogSink(entries, p_tag, anchor.ptr()))
        );
    }

    // An observation the case makes itself, recorded in line with the
    // callbacks rather than beside them. Order across the two is exactly what
    // a callback contract is, so they cannot be two lists.
    void note(const godot::StringName &p_tag) const {
        entries->tags.push_back(p_tag);
        entries->args.push_back(godot::Array());
    }

    // A callable that records its call and answers p_answer, for a subject that
    // reads its callback's return value rather than only calling it.
    godot::Callable answering(
        const godot::StringName &p_tag,
        const godot::Variant &p_answer
    ) const {
        return godot::Callable(
            memnew(CallLogSink(entries, p_tag, anchor.ptr(), p_answer))
        );
    }

    // A callable that records its call and answers a FRESH object each time,
    // for a subject whose contract is that it asks for one only once.
    godot::Callable minting(const godot::StringName &p_tag) const {
        return godot::Callable(memnew(
            CallLogSink(entries, p_tag, anchor.ptr(), godot::Variant(), true)
        ));
    }

    // The arguments the p_index'th call under p_tag carried, empty when there
    // was no such call. What the callback was called WITH is half the contract
    // and a count cannot state it.
    godot::Array args(const godot::StringName &p_tag, int p_index = 0) const {
        int seen = 0;
        for (int at = 0; at < entries->tags.size(); ++at) {
            if (entries->tags[at] != p_tag) {
                continue;
            }
            if (seen == p_index) {
                return entries->args[at];
            }
            ++seen;
        }
        return godot::Array();
    }

    int count(const godot::StringName &p_tag) const {
        int total = 0;
        for (const godot::StringName &tag : entries->tags) {
            total += tag == p_tag ? 1 : 0;
        }
        return total;
    }

    // Call order ACROSS callbacks, which is what a per-callback count cannot
    // say.
    godot::Vector<godot::StringName> order() const {
        return entries->tags;
    }

    void clear() {
        entries->tags.clear();
        entries->args.clear();
    }
};

} // namespace netw_test
