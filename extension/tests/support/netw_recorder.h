#pragma once

/* What happened, in what order.
 *
 * A recorder connects to a set of signals on one object and keeps every
 * emission: how many times each fired, the arguments each carried, and the
 * order they arrived across signals. Order across signals is the thing polled
 * state cannot express and the thing a session assertion almost always means.
 *
 * One spelling for both tiers. The module tier has `SIGNAL_WATCH` /
 * `SIGNAL_CHECK` and the hosted tier has nothing, so a recorder that delegated
 * would be two surfaces wearing one name. This is one implementation over a
 * custom callable, which both tiers bind identically.
 *
 * A recorder on a signal that does not exist FAILS THE CASE where it is
 * constructed. That is the whole reason it is a type and not a helper
 * function: a recorder wired to a misspelled signal is silently empty forever,
 * and an empty recorder reads exactly like a correct one that saw nothing.
 *
 * [codeblock]
 * Recorder r(api, { "peer_joined", "peer_left" });
 * rig.pump(4);
 * NETW_CHECK_EQ(r.count("peer_joined"), 1);
 * CHECK(r.args("peer_joined")[0] == Variant(7));
 * CHECK(r.order() == Vector<StringName>({ "peer_joined", "peer_left" }));
 * [/codeblock]
 */

// A sibling under `support/`, so the prelude is reached by its bare name here.
// A case file, which sits one directory up, writes `support/netw_test.h`.
#include "netw_test.h"

#include <memory>

#include "godot/callable.hpp"
#include "godot/object.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"

namespace netw_test {

// Every emission the recorder saw, in arrival order across all its signals.
// Held through a shared pointer because a connected callable is owned by the
// engine and may outlive the recorder that made it.
struct RecorderLog {
    struct Emission {
        godot::StringName signal;
        godot::Array args;
    };

    godot::Vector<Emission> emissions;
};

// One per watched signal. It knows which signal it is, so the shared log can
// record arrival order across all of them.
class RecorderSink final : public godot::CallableCustom {
    std::shared_ptr<RecorderLog> log;
    godot::StringName signal;
    godot::ObjectID source;

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
    RecorderSink(
        const std::shared_ptr<RecorderLog> &p_log,
        const godot::StringName &p_signal,
        const godot::Object *p_source
    )
        : log(p_log), signal(p_signal),
          source(netw::gd::instance_id(p_source)) {
    }

    uint32_t hash() const override {
        return uint32_t(uintptr_t(this));
    }

    godot::String get_as_text() const override {
        return godot::String("NetwRecorder::") + godot::String(signal);
    }

    CompareEqualFunc get_compare_equal_func() const override {
        return &RecorderSink::same;
    }

    CompareLessFunc get_compare_less_func() const override {
        return &RecorderSink::before;
    }

    godot::ObjectID get_object() const override {
        return source;
    }

    void call(
        const godot::Variant **p_arguments,
        int p_argcount,
        godot::Variant &r_return_value,
        netw::gd::CallError &r_call_error
    ) const override {
        godot::Array args;
        for (int index = 0; index < p_argcount; ++index) {
            args.push_back(*p_arguments[index]);
        }
        log->emissions.push_back({signal, args});
        r_return_value = godot::Variant();
        netw::gd::call_ok(r_call_error);
    }
};

// Connects at construction, disconnects at destruction, and never leaks a
// connection into the next case.
class Recorder {
    godot::Object *source = nullptr;
    std::shared_ptr<RecorderLog> log;
    godot::Vector<godot::StringName> signals;
    godot::Vector<godot::Callable> callables;

public:
    Recorder(
        godot::Object *p_object,
        const godot::Vector<godot::StringName> &p_signals
    )
        : source(p_object), log(std::make_shared<RecorderLog>()) {
        REQUIRE(p_object != nullptr);
        for (const godot::StringName &name : p_signals) {
            // The one failure a recorder must never absorb. An unconnected
            // recorder answers zero to every question and looks correct.
            REQUIRE_MESSAGE(
                p_object->has_signal(name),
                "no such signal on the recorded object"
            );
            if (!p_object->has_signal(name)) {
                continue;
            }
            const godot::Callable callable(
                memnew(RecorderSink(log, name, p_object))
            );
            p_object->connect(name, callable);
            signals.push_back(name);
            callables.push_back(callable);
        }
    }

    ~Recorder() {
        if (source == nullptr) {
            return;
        }
        for (int index = 0; index < signals.size(); ++index) {
            if (source->is_connected(signals[index], callables[index])) {
                source->disconnect(signals[index], callables[index]);
            }
        }
    }

    Recorder(const Recorder &) = delete;
    Recorder &operator=(const Recorder &) = delete;

    int count(const godot::StringName &p_signal) const {
        int total = 0;
        for (const RecorderLog::Emission &emission : log->emissions) {
            total += emission.signal == p_signal ? 1 : 0;
        }
        return total;
    }

    // The arguments of the nth emission of one signal. An out-of-range nth is
    // an empty array rather than a crash, so a wrong count fails on the count.
    godot::Array args(const godot::StringName &p_signal, int p_nth = 0) const {
        int seen = 0;
        for (const RecorderLog::Emission &emission : log->emissions) {
            if (emission.signal != p_signal) {
                continue;
            }
            if (seen == p_nth) {
                return emission.args;
            }
            ++seen;
        }
        return godot::Array();
    }

    // Emission order ACROSS signals, which is what polled state cannot say.
    godot::Vector<godot::StringName> order() const {
        godot::Vector<godot::StringName> names;
        for (const RecorderLog::Emission &emission : log->emissions) {
            names.push_back(emission.signal);
        }
        return names;
    }

    void clear() {
        log->emissions.clear();
    }
};

} // namespace netw_test
