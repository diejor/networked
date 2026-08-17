#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

using namespace godot;

class NetwPromise : public RefCounted {
    GDCLASS(NetwPromise, RefCounted)

private:
    bool completed = false;
    bool failed = false;
    Variant result;
    int32_t code = 0;
    String detail;
    LocalVector<Callable> then_callbacks;
    LocalVector<Callable> catch_callbacks;

protected:
    static void _bind_methods();

public:
    static Ref<NetwPromise> resolved(const Variant &p_value);
    static Ref<NetwPromise> rejected(int p_code, const String &p_detail);

    bool get_is_completed() const { return completed; }
    bool get_is_failed() const { return failed; }
    bool get_is_settled() const { return completed || failed; }
    Variant get_result() const { return result; }
    int get_code() const { return code; }
    String get_detail() const { return detail; }

    Ref<NetwPromise> then(const Callable &callback);
    Ref<NetwPromise> catch_error(const Callable &callback);

    void resolve(const Variant &value);
    void reject(int error_code, const String &error_detail);
};

} // namespace netw
