#include "netw/connect/transport.hpp"

#include "godot/class_db.hpp"
#include "godot/multiplayer.hpp"
#include "godot/object.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"
#include "netw/api/connect_handle.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/connect/creation.hpp"
#include "netw/connect/script_transport.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw::connect {

void Transport::report(
    const StringName &p_step,
    const String &p_message,
    double p_ratio
) {
    const Creation *asked = creation_of(armed_ticket);
    if (asked != nullptr && !asked->published && asked->progress.is_valid()) {
        Array carried;
        carried.push_back(p_step);
        carried.push_back(p_message);
        carried.push_back(p_ratio);
        asked->progress.callv(carried);
    }
}

StringName peer_class_of(const Ref<MultiplayerPeer> &p_peer) {
    if (p_peer.is_null()) {
        return StringName();
    }
    Ref<Script> walked = p_peer->get_script();
    while (walked.is_valid()) {
        const StringName named = walked->get_global_name();
        if (named != StringName()) {
            return named;
        }
        walked = walked->get_base_script();
    }
    return p_peer->get_class();
}

StringName peer_class_of_type(const Variant &p_type) {
    if (p_type.get_type() == Variant::STRING_NAME
        || p_type.get_type() == Variant::STRING) {
        return StringName(p_type);
    }
    Object *type = p_type;
    if (type == nullptr) {
        return StringName();
    }
    Script *as_script = Object::cast_to<Script>(type);
    if (as_script != nullptr) {
        Ref<Script> walked = as_script;
        while (walked.is_valid()) {
            const StringName named = walked->get_global_name();
            if (named != StringName()) {
                return named;
            }
            walked = walked->get_base_script();
        }
        return StringName();
    }
    const Ref<MultiplayerPeer> as_peer = p_type;
    if (as_peer.is_valid()) {
        return peer_class_of(as_peer);
    }
    if (type->get_class() != StringName("GDScriptNativeClass")) {
        return type->get_class();
    }
    StringName named;
    const Variant made = type->call("new");
    Object *probe = gd::live_object(made);
    if (probe != nullptr) {
        named = probe->get_class();
        if (Object::cast_to<RefCounted>(probe) == nullptr) {
            memdelete(probe);
        }
    }
    return named;
}

bool Transport::recognizes_peer(const Ref<MultiplayerPeer> &p_peer) const {
    return peer_class_of(p_peer) == peer_class();
}

String Transport::address_label() const {
    return String("Address");
}

String Transport::address_placeholder() const {
    return String();
}

String Transport::address_help() const {
    return String();
}

Dictionary Transport::host_settings() const {
    return Dictionary();
}

Dictionary Transport::client_settings() const {
    return Dictionary();
}

void Transport::probe(const String &p_address) {
    fail(ERR_UNAVAILABLE, "this transport does not probe");
}

void Transport::publish_targets(
    const PackedStringArray &p_addresses,
    const PackedStringArray &p_names,
    const Array &p_infos
) {
    if (session == nullptr) {
        return;
    }
    session->discovery_publish(peer_class(), p_addresses, p_names, p_infos);
}

void Transport::arm(const RID &p_ticket, const Ref<NetwPromise> &p_outcome) {
    armed_ticket = p_ticket;
    outcome = p_outcome;
}

void Transport::deliver(const Ref<MultiplayerPeer> &p_peer) {
    if (outcome.is_valid() && !outcome->get_is_settled()) {
        outcome->resolve(p_peer);
    }
}

void Transport::deliver_probe(const Ref<NetwServerInfo> &p_info) {
    if (outcome.is_valid() && !outcome->get_is_settled()) {
        outcome->resolve(p_info);
    }
}

void Transport::fail(Error p_error, const String &p_message) {
    if (outcome.is_valid() && !outcome->get_is_settled()) {
        outcome->reject(p_error, p_message);
    }
}

Ref<MultiplayerPeer> Transport::make_probe_peer(const String &p_address) {
    return Ref<MultiplayerPeer>();
}

Dictionary Transport::diagnostics(int64_t p_peer_id) const {
    return Dictionary();
}

namespace {

RID_Owner<TransportSlot> &slot_owner() {
    static RID_Owner<TransportSlot> instance;
    static bool described = false;
    if (!described) {
        instance.set_description("netw::connect transport");
        described = true;
    }
    return instance;
}

} // namespace

TransportFacts facts_of(const Transport &p_transport) {
    TransportFacts facts;
    facts.display_name = p_transport.display_name();
    facts.address_label = p_transport.address_label();
    facts.address_placeholder = p_transport.address_placeholder();
    facts.address_help = p_transport.address_help();
    facts.host_settings = p_transport.host_settings();
    facts.client_settings = p_transport.client_settings();
    facts.is_available = p_transport.is_available();
    facts.can_host_here = p_transport.can_host_here();
    facts.can_probe = p_transport.can_probe();
    facts.can_browse = p_transport.can_browse();
    facts.accepts_empty_address = p_transport.accepts_empty_address();
    return facts;
}

RID mint_built_in_slot(const StringName &p_peer_class) {
    TransportSlot slot;
    slot.peer_class = p_peer_class;
    slot.source_kind = TRANSPORT_SOURCE_BUILT_IN;
    return slot_owner().make_rid(slot);
}

RID mint_registration_slot(
    const StringName &p_peer_class,
    ObjectID p_session,
    const Ref<Script> &p_script
) {
    TransportSlot slot;
    slot.peer_class = p_peer_class;
    slot.session = p_session;
    slot.source_kind = TRANSPORT_SOURCE_REGISTRATION;
    slot.script = p_script;
    return slot_owner().make_rid(slot);
}

RID mint_directory_slot(
    const StringName &p_peer_class,
    ObjectID p_session,
    ObjectID p_directory
) {
    TransportSlot slot;
    slot.peer_class = p_peer_class;
    slot.session = p_session;
    slot.source_kind = TRANSPORT_SOURCE_DIRECTORY;
    slot.directory = p_directory;
    return slot_owner().make_rid(slot);
}

void free_transport_slot(const RID &p_slot) {
    if (slot_owner().owns(p_slot)) {
        slot_owner().free(p_slot);
    }
}

const TransportSlot *transport_slot(const RID &p_slot) {
    return slot_owner().get_or_null(p_slot);
}

TransportBook &TransportBook::shared() {
    static TransportBook book;
    return book;
}

void TransportBook::install_native(
    const StringName &p_peer_class,
    TransportFactory p_make,
    const String &p_display_name
) {
    const int at = index_of(p_peer_class);
    if (at >= 0) {
        rows[at].factory = p_make;
        rows[at].display_name = p_display_name;
        return;
    }
    TransportRow row;
    row.peer_class = p_peer_class;
    row.factory = p_make;
    row.display_name = p_display_name;
    row.slot = mint_built_in_slot(p_peer_class);
    rows.push_back(row);
}

Transport *make_from_script(const Ref<Script> &p_script) {
    if (p_script.is_null()) {
        return nullptr;
    }
    Ref<NetwTransport> instance = Ref<NetwTransport>(
        Object::cast_to<NetwTransport>(p_script->call(StringName("new")))
    );
    if (instance.is_null()) {
        return nullptr;
    }
    return new ScriptTransport(instance);
}

StringName peer_class_of_script(const Ref<Script> &p_script) {
    if (p_script.is_null()) {
        return StringName();
    }
    Ref<NetwTransport> prototype = Ref<NetwTransport>(
        Object::cast_to<NetwTransport>(p_script->call(StringName("new")))
    );
    if (prototype.is_null()) {
        return StringName();
    }
    return prototype->peer_class();
}

void TransportBook::forget(const StringName &p_peer_class) {
    const int at = index_of(p_peer_class);
    if (at >= 0) {
        free_transport_slot(rows[at].slot);
        rows.remove_at(at);
    }
}

Transport *TransportBook::make(const StringName &p_peer_class) const {
    const int at = index_of(p_peer_class);
    if (at < 0) {
        return nullptr;
    }
    if (rows[at].factory != nullptr) {
        return rows[at].factory();
    }
    return nullptr;
}

Transport *TransportBook::make_for_peer(
    const Ref<MultiplayerPeer> &p_peer
) const {
    for (uint32_t at = 0; at < rows.size(); at++) {
        if (rows[at].factory == nullptr) {
            continue;
        }
        Transport *probe = rows[at].factory();
        if (probe->recognizes_peer(p_peer)) {
            return probe;
        }
        delete probe;
    }
    return nullptr;
}

bool TransportBook::holds(const StringName &p_peer_class) const {
    return index_of(p_peer_class) >= 0;
}

Array TransportBook::peer_classes() const {
    Array classes;
    for (uint32_t at = 0; at < rows.size(); at++) {
        classes.push_back(rows[at].peer_class);
    }
    return classes;
}

Array TransportBook::slots() const {
    Array held;
    for (uint32_t at = 0; at < rows.size(); at++) {
        held.push_back(rows[at].slot);
    }
    return held;
}

RID TransportBook::slot_of(const StringName &p_peer_class) const {
    const int at = index_of(p_peer_class);
    return at < 0 ? RID() : rows[at].slot;
}

const TransportRow *TransportBook::row(const StringName &p_peer_class) const {
    const int at = index_of(p_peer_class);
    return at < 0 ? nullptr : &rows[at];
}

void TransportBook::clear() {
    for (uint32_t at = 0; at < rows.size(); at++) {
        free_transport_slot(rows[at].slot);
    }
    rows.clear();
}

int TransportBook::index_of(const StringName &p_peer_class) const {
    for (uint32_t at = 0; at < rows.size(); at++) {
        if (rows[at].peer_class == p_peer_class) {
            return int(at);
        }
    }
    return -1;
}

TransportRegistry::~TransportRegistry() {
    clear();
}

RID TransportRegistry::register_script(
    const Ref<Script> &p_script,
    ObjectID p_session
) {
    if (p_script.is_null()) {
        return RID();
    }
    const StringName named = peer_class_of_script(p_script);
    if (named == StringName()) {
        NETW_ERROR(
            sys::SESSION,
            "a transport script that names no peer class cannot be "
            "registered"
        );
        return RID();
    }
    const int at = index_of(named);
    if (at >= 0) {
        if (rows[at].script == p_script) {
            return rows[at].slot;
        }
        NETW_ERROR(
            sys::SESSION,
            "this session already registers a transport for peer class "
            "'%s', so the second registration is refused. Unregister the "
            "standing one first",
            String(named)
        );
        return RID();
    }
    Transport *prototype = make_from_script(p_script);
    if (prototype == nullptr) {
        return RID();
    }
    TransportRegistration row;
    row.peer_class = named;
    row.script = p_script;
    row.display_name = prototype->display_name();
    row.slot = mint_registration_slot(named, p_session, p_script);
    delete prototype;
    rows.push_back(row);
    return row.slot;
}

Error TransportRegistry::unregister(const RID &p_slot) {
    const int at = index_of_slot(p_slot);
    if (at < 0) {
        return ERR_DOES_NOT_EXIST;
    }
    free_transport_slot(rows[at].slot);
    rows.remove_at(at);
    return OK;
}

Transport *TransportRegistry::make(const StringName &p_peer_class) const {
    const int at = index_of(p_peer_class);
    return at < 0 ? nullptr : make_from_script(rows[at].script);
}

Transport *TransportRegistry::make_for_peer(
    const Ref<MultiplayerPeer> &p_peer
) const {
    for (uint32_t at = 0; at < rows.size(); at++) {
        Transport *probe = make_from_script(rows[at].script);
        if (probe == nullptr) {
            continue;
        }
        if (probe->recognizes_peer(p_peer)) {
            return probe;
        }
        delete probe;
    }
    return nullptr;
}

bool TransportRegistry::holds(const StringName &p_peer_class) const {
    return index_of(p_peer_class) >= 0;
}

RID TransportRegistry::slot_of(const StringName &p_peer_class) const {
    const int at = index_of(p_peer_class);
    return at < 0 ? RID() : rows[at].slot;
}

RID TransportRegistry::slot_of_script(const Ref<Script> &p_script) const {
    if (p_script.is_null()) {
        return RID();
    }
    for (uint32_t at = 0; at < rows.size(); at++) {
        if (rows[at].script == p_script) {
            return rows[at].slot;
        }
    }
    return RID();
}

Array TransportRegistry::slots() const {
    Array held;
    for (uint32_t at = 0; at < rows.size(); at++) {
        held.push_back(rows[at].slot);
    }
    return held;
}

Array TransportRegistry::peer_classes() const {
    Array named;
    for (uint32_t at = 0; at < rows.size(); at++) {
        named.push_back(rows[at].peer_class);
    }
    return named;
}

void TransportRegistry::clear() {
    for (uint32_t at = 0; at < rows.size(); at++) {
        free_transport_slot(rows[at].slot);
    }
    rows.clear();
}

int TransportRegistry::index_of(const StringName &p_peer_class) const {
    for (uint32_t at = 0; at < rows.size(); at++) {
        if (rows[at].peer_class == p_peer_class) {
            return int(at);
        }
    }
    return -1;
}

int TransportRegistry::index_of_slot(const RID &p_slot) const {
    for (uint32_t at = 0; at < rows.size(); at++) {
        if (rows[at].slot == p_slot) {
            return int(at);
        }
    }
    return -1;
}

} // namespace netw::connect
