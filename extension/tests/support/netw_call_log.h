#pragma once

#include "netw_test.h"

#include <memory>

#include "godot/callable.hpp"
#include "godot/ref_counted.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"

namespace netw_test {

struct CallLogEntries {
    godot::Vector<godot::StringName> tags;
    godot::Vector<godot::Array> args;
};

class CallLogSink final : public godot::CallableCustom {
    std::shared_ptr<CallLogEntries> entries;
    godot::StringName tag;
    godot::ObjectID anchor;
    godot::Variant answer;
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

    godot::Callable callable(const godot::StringName &p_tag) const {
        return godot::Callable(
            memnew(CallLogSink(entries, p_tag, anchor.ptr()))
        );
    }

    void note(const godot::StringName &p_tag) const {
        entries->tags.push_back(p_tag);
        entries->args.push_back(godot::Array());
    }

    godot::Callable answering(
        const godot::StringName &p_tag,
        const godot::Variant &p_answer
    ) const {
        return godot::Callable(
            memnew(CallLogSink(entries, p_tag, anchor.ptr(), p_answer))
        );
    }

    godot::Callable minting(const godot::StringName &p_tag) const {
        return godot::Callable(memnew(
            CallLogSink(entries, p_tag, anchor.ptr(), godot::Variant(), true)
        ));
    }

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

    godot::Vector<godot::StringName> order() const {
        return entries->tags;
    }

    void clear() {
        entries->tags.clear();
        entries->args.clear();
    }
};

} // namespace netw_test
