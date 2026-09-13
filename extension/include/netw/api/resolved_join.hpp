#pragma once

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/wire/describe.hpp"

namespace netw {

struct AcceptFrame {
    int64_t peer_id = 0;
    godot::StringName username;
    godot::PackedByteArray values;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&AcceptFrame::peer_id>(
            "peer_id",
            netw::wire::svarint(5)
        ),
        netw::wire::field<&AcceptFrame::username>(
            "username",
            netw::wire::string()
        ),
        netw::wire::field<&AcceptFrame::values>(
            "values",
            netw::wire::bytes_capped(4095)
        )
    );
};

class ResolvedJoin : public godot::RefCounted {
    GDCLASS(ResolvedJoin, godot::RefCounted)

    int64_t peer_id = 0;
    godot::StringName username;
    godot::Array arg_values;

protected:
    static void _bind_methods();

public:
    int64_t get_peer_id() const;
    void set_peer_id(int64_t value);
    godot::StringName get_username() const;
    void set_username(const godot::StringName &value);
    godot::Array get_arg_values() const;
    void set_arg_values(const godot::Array &value);

    AcceptFrame accept_frame() const;
    static godot::Ref<ResolvedJoin> of_frame(const AcceptFrame &frame);

    godot::PackedByteArray serialize() const;
    static godot::Ref<ResolvedJoin> deserialize(
        const godot::PackedByteArray &bytes
    );
};

} // namespace netw
