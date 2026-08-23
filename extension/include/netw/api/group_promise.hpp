#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwGroupPromise : public godot::RefCounted {
    GDCLASS(NetwGroupPromise, godot::RefCounted)

private:
    bool completed = false;
    bool failed = false;
    godot::Dictionary results;
    godot::LocalVector<int64_t> expected;
    int32_t code = 0;
    godot::String detail;
    godot::LocalVector<godot::Callable> then_callbacks;
    godot::LocalVector<godot::Callable> catch_callbacks;

    bool forget(int64_t p_peer);

protected:
    static void _bind_methods();

public:
    static godot::Ref<NetwGroupPromise> create(
        const godot::PackedInt32Array &p_peers
    );

    bool get_is_completed() const { return completed; }
    bool get_is_failed() const { return failed; }
    bool get_is_settled() const { return completed || failed; }
    godot::Dictionary get_results() const { return results; }
    godot::PackedInt32Array get_expected_peers() const;
    int get_code() const { return code; }
    godot::String get_detail() const { return detail; }

    godot::Ref<NetwGroupPromise> then(const godot::Callable &p_callback);
    godot::Ref<NetwGroupPromise> catch_error(
        const godot::Callable &p_callback
    );

    void resolve_peer(int64_t p_peer, const godot::Variant &p_value);
    void remove_peer(int64_t p_peer);
    void resolve_all();
    void reject(int p_code, const godot::String &p_detail);
};

} // namespace netw
