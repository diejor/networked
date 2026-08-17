#pragma once

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

namespace netw {

using namespace godot;

class NetwDisplayOffset : public RefCounted {
    GDCLASS(NetwDisplayOffset, RefCounted)

private:
    Variant offset;
    bool armed = false;

protected:
    static void _bind_methods();

public:
    void clear();
    void arm(bool p_pending);
    bool is_armed() const;
    bool is_held() const;
    Variant held() const;

    void absorb(const Variant &p_recovery, double p_limit);

    Variant apply(
        const Variant &p_target,
        double p_glide,
        double p_limit,
        const Variant &p_displayed,
        int64_t p_mode
    );
};

} // namespace netw
