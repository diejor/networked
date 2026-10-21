#pragma once

#include <cstdint>

#include "godot/variant.hpp"
#include "godot/ref_counted.hpp"
#include "netw/wire/describe.hpp"

namespace netw {

class NetwJoinRequest : public godot::RefCounted {
    GDCLASS(NetwJoinRequest, godot::RefCounted)

    int64_t peer_id = 0;
    godot::StringName username;
    godot::Array args;
    godot::String challenge;
    bool challenge_bound = false;
    godot::Variant proof;
    godot::PackedStringArray identities;
    godot::String account;
    godot::String account_name;

protected:
    static void _bind_methods();

public:
    void bind_to(
        int64_t p_peer_id,
        const godot::StringName &p_username,
        const godot::Array &p_args,
        const godot::String &p_challenge,
        bool p_challenge_bound,
        const godot::Variant &p_proof,
        const godot::PackedStringArray &p_identities,
        const godot::String &p_account_name
    );
    int64_t get_peer_id() const { return peer_id; }
    godot::StringName get_username() const { return username; }
    godot::Array get_args() const { return args.duplicate(true); }
    godot::String get_challenge() const { return challenge; }
    bool get_challenge_bound() const { return challenge_bound; }
    godot::Variant get_proof() const { return proof; }
    godot::PackedStringArray get_identities() const { return identities; }
    void set_account(const godot::String &p_account) { account = p_account; }
    godot::String get_account() const { return account; }
    void set_account_name(const godot::String &p_name) {
        account_name = p_name;
    }
    godot::String get_account_name() const { return account_name; }
};

struct JoinFrame {
    godot::StringName username;
    godot::PackedByteArray args;
    godot::PackedByteArray proof;
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
        netw::wire::field<&JoinFrame::proof>(
            "proof", netw::wire::bytes_capped(16383)
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
    godot::PackedByteArray proof_bytes;
    int64_t schema_hash = 0;
    int64_t peer_id = 0;
    int64_t app_tag = 0;
    int64_t wire_identity = 0;
    int64_t schema_identity = 0;

    godot::PackedByteArray serialize() const;
    bool deserialize(const godot::PackedByteArray &p_bytes);
};

} // namespace netw
