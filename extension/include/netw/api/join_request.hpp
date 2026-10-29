#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/session.hpp"
#include "netw/wire/describe.hpp"

namespace netw {

class NetwMultiplayer;

class NetwJoinRequest : public godot::RefCounted {
    GDCLASS(NetwJoinRequest, godot::RefCounted)

    struct Proven {
        godot::String account;
        godot::String name;
        bool bound = false;
    };

    int64_t peer_id = 0;
    godot::StringName username;
    godot::Array args;
    godot::String challenge;
    bool challenge_bound = false;
    godot::LocalVector<Proven> proven;
    godot::String account;
    godot::String account_name;
    godot::ObjectID core_id;
    godot::Error refused_code = godot::OK;
    godot::String refused_reason;

    const Proven *find(const godot::String &p_account) const;
    void merge(
        const godot::String &p_account,
        const godot::String &p_name,
        bool p_bound
    );

protected:
    static void _bind_methods();

public:
    void bind_to(
        int64_t p_peer_id,
        const godot::StringName &p_username,
        const godot::Array &p_args,
        const godot::String &p_challenge,
        bool p_challenge_bound
    );
    void bind_session(NetwMultiplayer *p_core);
    godot::Ref<NetwSession> get_session() const;
    void add_account(
        const godot::String &p_account,
        const godot::String &p_name,
        bool p_answers_challenge = true
    );
    void add_vouched_account(
        const godot::String &p_account,
        const godot::String &p_name
    );
    void start_from_first_account();
    godot::String name_of(const godot::String &p_account) const;
    bool is_bound(const godot::String &p_account) const;
    int64_t get_peer_id() const {
        return peer_id;
    }
    godot::StringName get_username() const {
        return username;
    }
    godot::Array get_args() const {
        return args.duplicate(true);
    }
    godot::String get_challenge() const {
        return challenge;
    }
    godot::PackedStringArray get_accounts() const;
    void set_account(const godot::String &p_account) {
        account = p_account;
    }
    godot::String get_account() const {
        return account;
    }
    void set_account_name(const godot::String &p_name) {
        account_name = p_name;
    }
    godot::String get_account_name() const {
        return account_name;
    }
    godot::Error refuse(godot::Error p_code, const godot::String &p_reason);
    godot::String reason_for(godot::Error p_code) const;
};

struct JoinFrame {
    godot::StringName username;
    godot::PackedByteArray args;
    godot::PackedByteArray arg_types;
    godot::PackedByteArray proof;
    int64_t schema_hash = 0;
    int64_t peer_id = 0;
    int64_t app_tag = 0;
    int64_t wire_identity = 0;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&JoinFrame::username>(
            "username",
            netw::wire::string()
        ),
        netw::wire::field<&JoinFrame::args>(
            "args",
            netw::wire::bytes_capped(4095)
        ),
        netw::wire::field<&JoinFrame::arg_types>(
            "arg_types",
            netw::wire::bytes_capped(255)
        ),
        netw::wire::field<&JoinFrame::proof>(
            "proof",
            netw::wire::bytes_capped(16383)
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
        )
    );
};

struct JoinRequest {
    godot::StringName username;
    godot::Array arg_values;
    godot::PackedByteArray arg_bytes;
    godot::PackedByteArray arg_types;
    godot::PackedByteArray proof_bytes;
    int64_t schema_hash = 0;
    int64_t peer_id = 0;
    int64_t app_tag = 0;
    int64_t wire_identity = 0;

    godot::PackedByteArray serialize() const;
    bool deserialize(const godot::PackedByteArray &p_bytes);
};

} // namespace netw
