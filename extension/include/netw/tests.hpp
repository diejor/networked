#pragma once

#include "godot/variant.hpp"

#if defined(NETW_TESTS)

#include "godot/ref_counted.hpp"

namespace netw {

int run_native_tests(
    const godot::String &filter,
    const godot::String &report_path,
    const godot::String &cells_path
);

// The one class a test build adds, so a headless driver can reach the embedded
// suite and emit instrumentation samples. Builds that do not compile the suite
// in do not carry it, and nothing outside this header names it.
class NetwNativeTests : public godot::RefCounted {
    GDCLASS(NetwNativeTests, godot::RefCounted)

protected:
    static void _bind_methods();

public:
    godot::Dictionary run(
        const godot::String &filter = godot::String(),
        const godot::String &report_path = "res://reports/native/results.xml",
        const godot::String &cells_path = godot::String()
    );
    void instrumentation_probe(int64_t value) const;

    godot::Dictionary wire_spec() const;
};

} // namespace netw

#endif // NETW_TESTS
