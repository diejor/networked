#pragma once

#include <cstdint>

#include "godot/variant.hpp"
#include "netw/api/resolved_join.hpp"
#include "netw/wire/describe.hpp"

namespace netw {

struct JoinFrame {
    godot::StringName username;
    godot::PackedByteArray args;
    int64_t schema_hash = 0;
    int64_t peer_id = 0;
    int64_t app_tag = 0;
    int64_t wire_identity = 0;
    int64_t schema_identity = 0;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&JoinFrame::username>(
            "username",
            netw::wire::string()
        ),
        netw::wire::field<&JoinFrame::args>(
            "args",
            netw::wire::bytes_capped(4095)
        ),
        netw::wire::field<&JoinFrame::schema_hash>(
            "schema_hash",
            netw::wire::bits(64)
        ),
        netw::wire::field<&JoinFrame::peer_id>(
            "peer_id",
            netw::wire::svarint(5)
        ),
        netw::wire::field<&JoinFrame::app_tag>("app_tag", netw::wire::bits(64)),
        netw::wire::field<&JoinFrame::wire_identity>(
            "wire_identity",
            netw::wire::bits(64)
        ),
        netw::wire::field<&JoinFrame::schema_identity>(
            "schema_identity",
            netw::wire::bits(64)
        )
    );
};

struct JoinRequest {
    godot::StringName username;
    godot::Array arg_values;
    godot::PackedByteArray arg_bytes;
    int64_t schema_hash = 0;
    int64_t peer_id = 0;
    int64_t app_tag = 0;
    int64_t wire_identity = 0;
    int64_t schema_identity = 0;

    godot::Ref<ResolvedJoin> resolve() const;
    godot::PackedByteArray serialize() const;
    bool deserialize(const godot::PackedByteArray &p_bytes);
};

} // namespace netw
