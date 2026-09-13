#include "netw/api/netw_transport.hpp"

#include "godot/callable.hpp"
#include "godot/class_db.hpp"
#include "godot/object.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/connect/creation.hpp"
#include "netw/connect/probe_client.hpp"
#include "netw/connect/transport.hpp"

using namespace godot;

namespace netw {

NetwTransport::~NetwTransport() {
    if (probing != nullptr) {
        probing->close();
        memdelete(probing);
        probing = nullptr;
    }
}

void NetwTransport::probe_authenticating(int64_t p_peer_id) {
    if (probing != nullptr) {
        probing->on_authenticating(p_peer_id);
    }
}

void NetwTransport::probe_auth_received(
    int64_t p_peer_id,
    const PackedByteArray &p_data
) {
    if (probing != nullptr) {
        probing->on_auth_received(p_peer_id, p_data);
    }
}

void NetwTransport::probe_connection_failed() {
    if (probing != nullptr) {
        probing->on_connection_failed();
    }
}

void NetwTransport::probe_authentication_failed(int64_t p_peer_id) {
    if (probing != nullptr) {
        probing->on_authentication_failed(p_peer_id);
    }
}

void NetwTransport::_bind_methods() {
    ClassDB::bind_method(D_METHOD("peer_class"), &NetwTransport::peer_class);
    ClassDB::bind_method(
        D_METHOD("peer_class_default"),
        &NetwTransport::peer_class_default
    );
    ClassDB::bind_method(
        D_METHOD("recognizes_peer", "peer"),
        &NetwTransport::recognizes_peer
    );
    ClassDB::bind_method(
        D_METHOD("recognizes_peer_default", "peer"),
        &NetwTransport::recognizes_peer_default
    );
    ClassDB::bind_method(
        D_METHOD("display_name"),
        &NetwTransport::display_name
    );
    ClassDB::bind_method(
        D_METHOD("display_name_default"),
        &NetwTransport::display_name_default
    );
    ClassDB::bind_method(
        D_METHOD("is_available"),
        &NetwTransport::is_available
    );
    ClassDB::bind_method(
        D_METHOD("is_available_default"),
        &NetwTransport::is_available_default
    );
    ClassDB::bind_method(
        D_METHOD("can_host_here"),
        &NetwTransport::can_host_here
    );
    ClassDB::bind_method(
        D_METHOD("can_host_here_default"),
        &NetwTransport::can_host_here_default
    );
    ClassDB::bind_method(D_METHOD("can_probe"), &NetwTransport::can_probe);
    ClassDB::bind_method(
        D_METHOD("can_probe_default"),
        &NetwTransport::can_probe_default
    );
    ClassDB::bind_method(
        D_METHOD("address_label"),
        &NetwTransport::address_label
    );
    ClassDB::bind_method(
        D_METHOD("address_label_default"),
        &NetwTransport::address_label_default
    );
    ClassDB::bind_method(
        D_METHOD("address_placeholder"),
        &NetwTransport::address_placeholder
    );
    ClassDB::bind_method(
        D_METHOD("address_placeholder_default"),
        &NetwTransport::address_placeholder_default
    );
    ClassDB::bind_method(
        D_METHOD("address_help"),
        &NetwTransport::address_help
    );
    ClassDB::bind_method(
        D_METHOD("address_help_default"),
        &NetwTransport::address_help_default
    );
    ClassDB::bind_method(
        D_METHOD("accepts_empty_address"),
        &NetwTransport::accepts_empty_address
    );
    ClassDB::bind_method(
        D_METHOD("accepts_empty_address_default"),
        &NetwTransport::accepts_empty_address_default
    );
    ClassDB::bind_method(
        D_METHOD("host_settings"),
        &NetwTransport::host_settings
    );
    ClassDB::bind_method(
        D_METHOD("host_settings_default"),
        &NetwTransport::host_settings_default
    );
    ClassDB::bind_method(
        D_METHOD("client_settings"),
        &NetwTransport::client_settings
    );
    ClassDB::bind_method(
        D_METHOD("client_settings_default"),
        &NetwTransport::client_settings_default
    );
    ClassDB::bind_method(
        D_METHOD("make_probe_peer", "address"),
        &NetwTransport::make_probe_peer
    );
    ClassDB::bind_method(
        D_METHOD("make_probe_peer_default", "address"),
        &NetwTransport::make_probe_peer_default
    );
    ClassDB::bind_method(
        D_METHOD("join_address"),
        &NetwTransport::join_address
    );
    ClassDB::bind_method(
        D_METHOD("join_address_default"),
        &NetwTransport::join_address_default
    );
    ClassDB::bind_method(
        D_METHOD("diagnostics", "peer_id"),
        &NetwTransport::diagnostics
    );
    ClassDB::bind_method(
        D_METHOD("diagnostics_default", "peer_id"),
        &NetwTransport::diagnostics_default
    );
    ClassDB::bind_method(
        D_METHOD("timeout_hint"),
        &NetwTransport::timeout_hint
    );
    ClassDB::bind_method(
        D_METHOD("timeout_hint_default"),
        &NetwTransport::timeout_hint_default
    );

    ClassDB::bind_method(
        D_METHOD("report", "ticket", "step", "message", "ratio"),
        &NetwTransport::report
    );
    ClassDB::bind_method(
        D_METHOD("probe_default", "ticket", "address"),
        &NetwTransport::probe_default
    );
    ClassDB::bind_method(
        D_METHOD("deliver", "ticket", "peer"),
        &NetwTransport::deliver
    );
    ClassDB::bind_method(
        D_METHOD("publish_targets", "addresses", "names", "infos"),
        &NetwTransport::publish_targets,
        DEFVAL(PackedStringArray()),
        DEFVAL(Array())
    );
    ClassDB::bind_method(D_METHOD("can_browse"), &NetwTransport::can_browse);
    ClassDB::bind_method(
        D_METHOD("can_browse_default"),
        &NetwTransport::can_browse_default
    );
    ClassDB::bind_method(
        D_METHOD("deliver_probe", "ticket", "info"),
        &NetwTransport::deliver_probe
    );
    ClassDB::bind_method(
        D_METHOD("fail", "ticket", "error", "message"),
        &NetwTransport::fail,
        DEFVAL(String())
    );
    ClassDB::bind_method(D_METHOD("get_session"), &NetwTransport::get_session);

    ClassDB::bind_static_method(
        "NetwTransport",
        D_METHOD("peer_class_of", "peer"),
        &NetwTransport::peer_class_of
    );

    ADD_PROPERTY(
        PropertyInfo(
            Variant::OBJECT,
            "session",
            PROPERTY_HINT_RESOURCE_TYPE,
            "NetwMultiplayer"
        ),
        "",
        "get_session"
    );

    GDVIRTUAL_BIND(_peer_class);
    GDVIRTUAL_BIND(_recognizes_peer, "peer");
    GDVIRTUAL_BIND(_display_name);
    GDVIRTUAL_BIND(_is_available);
    GDVIRTUAL_BIND(_can_host_here);
    GDVIRTUAL_BIND(_can_probe);
    GDVIRTUAL_BIND(_address_label);
    GDVIRTUAL_BIND(_address_placeholder);
    GDVIRTUAL_BIND(_address_help);
    GDVIRTUAL_BIND(_accepts_empty_address);
    GDVIRTUAL_BIND(_host_settings);
    GDVIRTUAL_BIND(_client_settings);
    GDVIRTUAL_BIND(_make_peer, "ticket", "mode", "address", "settings");
    GDVIRTUAL_BIND(_cancel_peer_creation, "ticket");
    GDVIRTUAL_BIND(_probe, "ticket", "address");
    GDVIRTUAL_BIND(_can_browse);
    GDVIRTUAL_BIND(_browse);
    GDVIRTUAL_BIND(_make_probe_peer, "address");
    GDVIRTUAL_BIND(_adopt, "peer");
    GDVIRTUAL_BIND(_poll, "delta");
    GDVIRTUAL_BIND(_join_address);
    GDVIRTUAL_BIND(_diagnostics, "peer_id");
    GDVIRTUAL_BIND(_timeout_hint);
    GDVIRTUAL_BIND(_close);
}

StringName NetwTransport::peer_class_of(const Ref<MultiplayerPeer> &p_peer) {
    return connect::peer_class_of(p_peer);
}

void NetwTransport::bind_session(NetwMultiplayer *p_session) {
    session_id = gd::instance_id(p_session);
}

Ref<NetwMultiplayer> NetwTransport::get_session() const {
    return Ref<NetwMultiplayer>(
        Object::cast_to<NetwMultiplayer>(gd::object_of(session_id))
    );
}

void NetwTransport::report(
    const RID &p_ticket,
    const StringName &p_step,
    const String &p_message,
    double p_ratio
) {
    if (!speaks_for(p_ticket)) {
        return;
    }
    const connect::Creation *asked = connect::creation_of(p_ticket);
    if (asked != nullptr && !asked->published && asked->progress.is_valid()) {
        Array carried;
        carried.push_back(p_step);
        carried.push_back(p_message);
        carried.push_back(p_ratio);
        asked->progress.callv(carried);
    }
}

bool NetwTransport::speaks_for(const RID &p_ticket) const {
    return armed_ticket.is_valid() && p_ticket == armed_ticket;
}

void NetwTransport::arm(
    const RID &p_ticket,
    const Ref<NetwPromise> &p_outcome
) {
    armed_ticket = p_ticket;
    outcome = p_outcome;
}

void NetwTransport::deliver(
    const RID &p_ticket,
    const Ref<MultiplayerPeer> &p_peer
) {
    if (!speaks_for(p_ticket)) {
        return;
    }
    if (outcome.is_valid() && !outcome->get_is_settled()) {
        outcome->resolve(p_peer);
    }
}

void NetwTransport::deliver_probe(
    const RID &p_ticket,
    const Ref<NetwServerInfo> &p_info
) {
    if (!speaks_for(p_ticket)) {
        return;
    }
    if (outcome.is_valid() && !outcome->get_is_settled()) {
        outcome->resolve(p_info);
    }
}

void NetwTransport::fail(
    const RID &p_ticket,
    Error p_error,
    const String &p_message
) {
    if (!speaks_for(p_ticket)) {
        return;
    }
    if (outcome.is_valid() && !outcome->get_is_settled()) {
        outcome->reject(p_error, p_message);
    }
}

void NetwTransport::probe_default(
    const RID &p_ticket,
    const String &p_address
) {
    const Ref<MultiplayerPeer> peer = make_probe_peer(p_address);
    if (peer.is_null()) {
        fail(p_ticket, ERR_UNAVAILABLE, "this transport does not probe");
        return;
    }
    if (probing == nullptr) {
        probing = memnew(connect::ProbeClient);
    } else {
        probing->close();
    }
    connect::ProbeHooks hooks;
    hooks.authenticating
        = callable_mp(this, &NetwTransport::probe_authenticating);
    hooks.auth_received
        = callable_mp(this, &NetwTransport::probe_auth_received);
    hooks.connection_failed
        = callable_mp(this, &NetwTransport::probe_connection_failed);
    hooks.authentication_failed
        = callable_mp(this, &NetwTransport::probe_authentication_failed);
    probing->open(peer, timeout_hint(), outcome, hooks);
}

StringName NetwTransport::peer_class() {
    StringName answered;
    if (GDVIRTUAL_CALL(_peer_class, answered)) {
        return answered;
    }
    return peer_class_default();
}

StringName NetwTransport::peer_class_default() {
    return StringName();
}

bool NetwTransport::recognizes_peer(const Ref<MultiplayerPeer> &p_peer) {
    bool answered = false;
    if (GDVIRTUAL_CALL(_recognizes_peer, p_peer, answered)) {
        return answered;
    }
    return recognizes_peer_default(p_peer);
}

bool NetwTransport::recognizes_peer_default(
    const Ref<MultiplayerPeer> &p_peer
) {
    return peer_class_of(p_peer) == peer_class();
}

String NetwTransport::display_name() {
    String answered;
    if (GDVIRTUAL_CALL(_display_name, answered)) {
        return answered;
    }
    return display_name_default();
}

String NetwTransport::display_name_default() {
    return String("Generic");
}

bool NetwTransport::is_available() {
    bool answered = false;
    if (GDVIRTUAL_CALL(_is_available, answered)) {
        return answered;
    }
    return is_available_default();
}

bool NetwTransport::is_available_default() {
    return true;
}

bool NetwTransport::can_host_here() {
    bool answered = false;
    if (GDVIRTUAL_CALL(_can_host_here, answered)) {
        return answered;
    }
    return can_host_here_default();
}

bool NetwTransport::can_host_here_default() {
    return true;
}

bool NetwTransport::can_probe() {
    bool answered = false;
    if (GDVIRTUAL_CALL(_can_probe, answered)) {
        return answered;
    }
    return can_probe_default();
}

bool NetwTransport::can_probe_default() {
    return false;
}

String NetwTransport::address_label() {
    String answered;
    if (GDVIRTUAL_CALL(_address_label, answered)) {
        return answered;
    }
    return address_label_default();
}

String NetwTransport::address_label_default() {
    return String("Address");
}

String NetwTransport::address_placeholder() {
    String answered;
    if (GDVIRTUAL_CALL(_address_placeholder, answered)) {
        return answered;
    }
    return address_placeholder_default();
}

String NetwTransport::address_placeholder_default() {
    return String();
}

String NetwTransport::address_help() {
    String answered;
    if (GDVIRTUAL_CALL(_address_help, answered)) {
        return answered;
    }
    return address_help_default();
}

String NetwTransport::address_help_default() {
    return String();
}

bool NetwTransport::accepts_empty_address() {
    bool answered = false;
    if (GDVIRTUAL_CALL(_accepts_empty_address, answered)) {
        return answered;
    }
    return accepts_empty_address_default();
}

bool NetwTransport::accepts_empty_address_default() {
    return false;
}

Dictionary NetwTransport::host_settings() {
    Dictionary answered;
    if (GDVIRTUAL_CALL(_host_settings, answered)) {
        return answered;
    }
    return host_settings_default();
}

Dictionary NetwTransport::host_settings_default() {
    return Dictionary();
}

Dictionary NetwTransport::client_settings() {
    Dictionary answered;
    if (GDVIRTUAL_CALL(_client_settings, answered)) {
        return answered;
    }
    return client_settings_default();
}

Dictionary NetwTransport::client_settings_default() {
    return Dictionary();
}

void NetwTransport::make_peer(
    const RID &p_ticket,
    int p_mode,
    const String &p_address,
    const Dictionary &p_settings
) {
    if (GDVIRTUAL_CALL(
            _make_peer,
            p_ticket,
            int64_t(p_mode),
            p_address,
            p_settings
        )) {
        return;
    }
    fail(p_ticket, ERR_UNCONFIGURED, "this transport cannot create a peer");
}

void NetwTransport::cancel_peer_creation(const RID &p_ticket) {
    GDVIRTUAL_CALL(_cancel_peer_creation, p_ticket);
}

bool NetwTransport::can_browse() {
    bool answered = false;
    if (GDVIRTUAL_CALL(_can_browse, answered)) {
        return answered;
    }
    return can_browse_default();
}

bool NetwTransport::can_browse_default() {
    return false;
}

void NetwTransport::browse() {
    GDVIRTUAL_CALL(_browse);
}

void NetwTransport::publish_targets(
    const PackedStringArray &p_addresses,
    const PackedStringArray &p_names,
    const Array &p_infos
) {
    const Ref<NetwMultiplayer> api = get_session();
    if (api.is_null()) {
        return;
    }
    api->discovery_publish(peer_class(), p_addresses, p_names, p_infos);
}

void NetwTransport::probe(const RID &p_ticket, const String &p_address) {
    if (GDVIRTUAL_CALL(_probe, p_ticket, p_address)) {
        return;
    }
    probe_default(p_ticket, p_address);
}

Ref<MultiplayerPeer> NetwTransport::make_probe_peer(const String &p_address) {
    Ref<MultiplayerPeer> answered;
    if (GDVIRTUAL_CALL(_make_probe_peer, p_address, answered)) {
        return answered;
    }
    return make_probe_peer_default(p_address);
}

Ref<MultiplayerPeer> NetwTransport::make_probe_peer_default(const String &) {
    return Ref<MultiplayerPeer>();
}

void NetwTransport::adopt(const Ref<MultiplayerPeer> &p_peer) {
    if (GDVIRTUAL_CALL(_adopt, p_peer)) {
        return;
    }
}

void NetwTransport::poll(double p_delta) {
    if (GDVIRTUAL_CALL(_poll, p_delta)) {
        return;
    }
}

String NetwTransport::join_address() {
    String answered;
    if (GDVIRTUAL_CALL(_join_address, answered)) {
        return answered;
    }
    return join_address_default();
}

String NetwTransport::join_address_default() {
    return String();
}

Dictionary NetwTransport::diagnostics(int64_t p_peer_id) {
    Dictionary answered;
    if (GDVIRTUAL_CALL(_diagnostics, p_peer_id, answered)) {
        return answered;
    }
    return diagnostics_default(p_peer_id);
}

Dictionary NetwTransport::diagnostics_default(int64_t) {
    return Dictionary();
}

double NetwTransport::timeout_hint() {
    double answered = 0.0;
    if (GDVIRTUAL_CALL(_timeout_hint, answered)) {
        return answered;
    }
    return timeout_hint_default();
}

double NetwTransport::timeout_hint_default() {
    return 5.0;
}

void NetwTransport::close() {
    if (GDVIRTUAL_CALL(_close)) {
        return;
    }
}

} // namespace netw
