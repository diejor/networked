#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

using namespace godot;

class NetwGroupPromise : public RefCounted {
    GDCLASS(NetwGroupPromise, RefCounted)

private:
    bool completed = false;
    bool failed = false;
    Dictionary results;
    LocalVector<int64_t> expected;
    int32_t code = 0;
    String detail;
    LocalVector<Callable> then_callbacks;
    LocalVector<Callable> catch_callbacks;

    bool forget(int64_t p_peer);

protected:
    static void _bind_methods();

public:
    static Ref<NetwGroupPromise> create(const PackedInt32Array &p_peers);

    bool get_is_completed() const { return completed; }
    bool get_is_failed() const { return failed; }
    bool get_is_settled() const { return completed || failed; }
    Dictionary get_results() const { return results; }
    PackedInt32Array get_expected_peers() const;
    int get_code() const { return code; }
    String get_detail() const { return detail; }

    Ref<NetwGroupPromise> then(const Callable &p_callback);
    Ref<NetwGroupPromise> catch_error(const Callable &p_callback);

    void resolve_peer(int64_t p_peer, const Variant &p_value);
    void remove_peer(int64_t p_peer);
    void resolve_all();
    void reject(int p_code, const String &p_detail);
};

} // namespace netw
