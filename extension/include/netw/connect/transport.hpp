#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/multiplayer.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/server_info.hpp"

namespace netw {

class NetwMultiplayer;

} // namespace netw

namespace netw::connect {

godot::StringName peer_class_of(
    const godot::Ref<godot::MultiplayerPeer> &p_peer
);
godot::StringName peer_class_of_type(const godot::Variant &p_type);

class Transport {
public:
    virtual ~Transport() = default;

    virtual godot::StringName peer_class() const = 0;
    virtual bool recognizes_peer(
        const godot::Ref<godot::MultiplayerPeer> &p_peer
    ) const;
    virtual godot::String display_name() const = 0;
    virtual bool is_available() const {
        return true;
    }
    virtual bool can_host_here() const {
        return true;
    }
    virtual bool can_probe() const {
        return false;
    }
    virtual bool can_browse() const {
        return false;
    }
    virtual godot::String address_label() const;
    virtual godot::String address_placeholder() const;
    virtual godot::String address_help() const;
    virtual bool accepts_empty_address() const {
        return false;
    }
    virtual godot::Dictionary host_settings() const;
    virtual godot::Dictionary client_settings() const;

    virtual void make_peer(
        int p_mode,
        const godot::String &p_address,
        const godot::Dictionary &p_settings
    ) = 0;
    virtual void cancel_peer_creation() {
    }
    virtual void probe(const godot::String &p_address);
    virtual void browse() {
    }
    virtual godot::Ref<godot::MultiplayerPeer> make_probe_peer(
        const godot::String &p_address
    );

    virtual void adopt(const godot::Ref<godot::MultiplayerPeer> &p_peer) {
    }
    virtual void poll(double p_delta) {
    }
    virtual godot::String join_address() const {
        return godot::String();
    }
    virtual godot::Dictionary diagnostics(int64_t p_peer_id) const;
    virtual double timeout_hint() const {
        return 5.0;
    }
    virtual void close() {
    }
    virtual void close_query() {
        close();
    }

    virtual void bind_session(NetwMultiplayer *p_session) {
        session = p_session;
    }
    NetwMultiplayer *bound_session() const {
        return session;
    }

    void report(
        const godot::StringName &p_step,
        const godot::String &p_message,
        double p_ratio
    );

    void arm(
        const godot::RID &p_ticket,
        const godot::Ref<NetwPromise> &p_outcome
    );
    const godot::RID &ticket() const {
        return armed_ticket;
    }
    void publish_targets(
        const godot::PackedStringArray &p_addresses,
        const godot::PackedStringArray &p_names,
        const godot::Array &p_infos
    );
    void deliver(const godot::Ref<godot::MultiplayerPeer> &p_peer);
    void deliver_probe(const godot::Ref<NetwServerInfo> &p_info);
    void fail(godot::Error p_error, const godot::String &p_message);

protected:
    NetwMultiplayer *session = nullptr;
    godot::Ref<NetwPromise> outcome;
    godot::RID armed_ticket;
};

using TransportFactory = Transport *(*)();

enum TransportSourceKind {
    TRANSPORT_SOURCE_BUILT_IN,
    TRANSPORT_SOURCE_REGISTRATION,
    TRANSPORT_SOURCE_DIRECTORY,
};

struct TransportSlot {
    godot::StringName peer_class;
    godot::ObjectID session;
    TransportSourceKind source_kind = TRANSPORT_SOURCE_BUILT_IN;
    godot::ObjectID directory;
    godot::Ref<godot::Script> script;
};

struct TransportFacts {
    godot::String display_name;
    godot::String address_label;
    godot::String address_placeholder;
    godot::String address_help;
    godot::Dictionary host_settings;
    godot::Dictionary client_settings;
    bool is_available = false;
    bool can_host_here = false;
    bool can_probe = false;
    bool can_browse = false;
    bool accepts_empty_address = false;
};

TransportFacts facts_of(const Transport &p_transport);

godot::RID mint_built_in_slot(const godot::StringName &p_peer_class);
godot::RID mint_registration_slot(
    const godot::StringName &p_peer_class,
    godot::ObjectID p_session,
    const godot::Ref<godot::Script> &p_script
);
godot::RID mint_directory_slot(
    const godot::StringName &p_peer_class,
    godot::ObjectID p_session,
    godot::ObjectID p_directory
);
void free_transport_slot(const godot::RID &p_slot);
const TransportSlot *transport_slot(const godot::RID &p_slot);

Transport *make_from_script(const godot::Ref<godot::Script> &p_script);
godot::StringName peer_class_of_script(
    const godot::Ref<godot::Script> &p_script
);

struct TransportRow {
    godot::StringName peer_class;
    TransportFactory factory = nullptr;
    godot::String display_name;
    godot::RID slot;
};

class TransportBook {
public:
    static TransportBook &shared();

    void install_native(
        const godot::StringName &p_peer_class,
        TransportFactory p_make,
        const godot::String &p_display_name
    );
    void forget(const godot::StringName &p_peer_class);

    Transport *make(const godot::StringName &p_peer_class) const;
    Transport *make_for_peer(
        const godot::Ref<godot::MultiplayerPeer> &p_peer
    ) const;
    bool holds(const godot::StringName &p_peer_class) const;
    godot::Array peer_classes() const;
    godot::Array slots() const;
    godot::RID slot_of(const godot::StringName &p_peer_class) const;
    const TransportRow *row(const godot::StringName &p_peer_class) const;

    void clear();

private:
    godot::LocalVector<TransportRow> rows;

    int index_of(const godot::StringName &p_peer_class) const;
};

struct TransportRegistration {
    godot::StringName peer_class;
    godot::Ref<godot::Script> script;
    godot::String display_name;
    godot::RID slot;
};

class TransportRegistry {
public:
    ~TransportRegistry();

    godot::RID register_script(
        const godot::Ref<godot::Script> &p_script,
        godot::ObjectID p_session
    );
    godot::Error unregister(const godot::RID &p_slot);

    Transport *make(const godot::StringName &p_peer_class) const;
    Transport *make_for_peer(
        const godot::Ref<godot::MultiplayerPeer> &p_peer
    ) const;
    bool holds(const godot::StringName &p_peer_class) const;
    godot::RID slot_of(const godot::StringName &p_peer_class) const;
    godot::RID slot_of_script(const godot::Ref<godot::Script> &p_script) const;
    godot::Array slots() const;
    godot::Array peer_classes() const;
    void clear();

private:
    godot::LocalVector<TransportRegistration> rows;

    int index_of(const godot::StringName &p_peer_class) const;
    int index_of_slot(const godot::RID &p_slot) const;
};

void install_native_transports();

} // namespace netw::connect
