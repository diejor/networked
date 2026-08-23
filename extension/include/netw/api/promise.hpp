#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwPromise : public godot::RefCounted {
    GDCLASS(NetwPromise, godot::RefCounted)

private:
    bool completed = false;
    bool failed = false;
    godot::Variant result;
    int32_t code = 0;
    godot::String detail;
    godot::LocalVector<godot::Callable> then_callbacks;
    godot::LocalVector<godot::Callable> catch_callbacks;

protected:
    static void _bind_methods();

public:
    static godot::Ref<NetwPromise> resolved(const godot::Variant &p_value);
    static godot::Ref<NetwPromise> rejected(
        int p_code,
        const godot::String &p_detail
    );

    bool get_is_completed() const { return completed; }
    bool get_is_failed() const { return failed; }
    bool get_is_settled() const { return completed || failed; }
    godot::Variant get_result() const { return result; }
    int get_code() const { return code; }
    godot::String get_detail() const { return detail; }

    godot::Ref<NetwPromise> then(const godot::Callable &callback);
    godot::Ref<NetwPromise> catch_error(const godot::Callable &callback);
    godot::Ref<NetwPromise> when_settled(const godot::Callable &callback);

    void resolve(const godot::Variant &value);
    void reject(int error_code, const godot::String &error_detail);
};

} // namespace netw
