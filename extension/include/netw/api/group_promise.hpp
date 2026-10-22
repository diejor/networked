#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/promise.hpp"

namespace netw {

class NetwGroupPromise : public godot::RefCounted {
    GDCLASS(NetwGroupPromise, godot::RefCounted)

private:
    bool completed = false;
    bool failed = false;
    godot::Dictionary results;
    godot::LocalVector<int64_t> expected;
    godot::Error code = godot::OK;
    godot::String detail;
    godot::Error refusal = godot::OK;
    godot::String refusal_detail;
    godot::LocalVector<godot::Callable> then_callbacks;
    godot::LocalVector<godot::Callable> catch_callbacks;
    promise_wait::Waiting waiting;

    bool forget(int64_t p_peer);
    void settle_when_answered();

protected:
    static void _bind_methods();

public:
    static godot::Ref<NetwGroupPromise> create(
        const godot::PackedInt32Array &p_peers
    );
    static godot::Ref<NetwGroupPromise> refused(
        godot::Error p_code,
        const godot::String &p_detail
    );

    bool get_is_completed() const {
        return completed;
    }
    bool get_is_failed() const {
        return failed;
    }
    bool get_is_settled() const {
        return completed || failed;
    }
    godot::Dictionary get_results() const {
        return results;
    }
    godot::PackedInt32Array get_expected_peers() const;
    godot::Error get_code() const {
        return code;
    }
    godot::String get_detail() const {
        return detail;
    }

    godot::Ref<NetwGroupPromise> then(const godot::Callable &p_callback);
    godot::Ref<NetwGroupPromise> catch_error(const godot::Callable &p_callback);
    godot::Signal wait();
    godot::Variant answer() const;

    void resolve_peer(int64_t p_peer, const godot::Variant &p_value);
    void refuse_peer(
        int64_t p_peer,
        godot::Error p_code,
        const godot::String &p_detail
    );
    void remove_peer(int64_t p_peer);
    void resolve_all();
    void reject(godot::Error p_code, const godot::String &p_detail);
};

} // namespace netw
