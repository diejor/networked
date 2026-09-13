#include "netw/connect/directory_transport.hpp"

#include "godot/object.hpp"
#include "godot/variant.hpp"
#include "netw/connect/creation.hpp"

using namespace godot;

namespace netw::connect {

const char *DirectoryTransport::SIG_PEER_READY = "lobby_peer_ready";
const char *DirectoryTransport::SIG_FAILED = "lobby_failed";
const char *DirectoryTransport::SIG_LIST_PUBLISHED = "lobby_list_published";

bool DirectoryTransport::is_directory(Object *p_node) {
    return Object::cast_to<netw::LobbyDirectory>(p_node) != nullptr;
}

StringName DirectoryTransport::peer_class_of_directory(Object *p_node) {
    netw::LobbyDirectory *found = Object::cast_to<netw::LobbyDirectory>(p_node);
    return found == nullptr ? StringName() : found->peer_class();
}

DirectoryTransport::DirectoryTransport(Object *p_directory)
    : directory(gd::instance_id(p_directory)) {
}

netw::LobbyDirectory *DirectoryTransport::reached() const {
    return Object::cast_to<netw::LobbyDirectory>(gd::object_of(directory));
}

StringName DirectoryTransport::peer_class() const {
    netw::LobbyDirectory *found = reached();
    return found == nullptr ? StringName() : found->peer_class();
}

String DirectoryTransport::display_name() const {
    netw::LobbyDirectory *found = reached();
    return found == nullptr ? String() : found->display_name();
}

bool DirectoryTransport::is_available() const {
    netw::LobbyDirectory *found = reached();
    return found != nullptr && found->is_available();
}

bool DirectoryTransport::can_host_here() const {
    netw::LobbyDirectory *found = reached();
    return found == nullptr || found->can_host_here();
}

bool DirectoryTransport::can_probe() const {
    netw::LobbyDirectory *found = reached();
    return found != nullptr && found->can_probe();
}

bool DirectoryTransport::can_browse() const {
    return reached() != nullptr;
}

String DirectoryTransport::address_label() const {
    netw::LobbyDirectory *found = reached();
    return found == nullptr ? String("Lobby") : found->address_label();
}

String DirectoryTransport::address_placeholder() const {
    netw::LobbyDirectory *found = reached();
    return found == nullptr ? String() : found->address_placeholder();
}

String DirectoryTransport::address_help() const {
    netw::LobbyDirectory *found = reached();
    return found == nullptr ? String() : found->address_help();
}

bool DirectoryTransport::accepts_empty_address() const {
    netw::LobbyDirectory *found = reached();
    return found != nullptr && found->accepts_empty_address();
}

Dictionary DirectoryTransport::host_settings() const {
    netw::LobbyDirectory *found = reached();
    return found == nullptr ? Dictionary() : found->host_settings();
}

Dictionary DirectoryTransport::client_settings() const {
    netw::LobbyDirectory *found = reached();
    return found == nullptr ? Dictionary() : found->client_settings();
}

void DirectoryTransport::make_peer(
    int p_mode,
    const String &p_address,
    const Dictionary &p_settings
) {
    netw::LobbyDirectory *found = reached();
    if (found == nullptr) {
        fail(ERR_UNAVAILABLE, "the lobby directory left the session.");
        return;
    }
    if (p_mode == PEER_MODE_CLIENT) {
        found->join_lobby(p_address);
        return;
    }
    found->host_lobby(p_settings);
}

void DirectoryTransport::browse() {
    netw::LobbyDirectory *found = reached();
    if (found != nullptr) {
        found->list_lobbies();
    }
}

String DirectoryTransport::join_address() const {
    netw::LobbyDirectory *found = reached();
    return found == nullptr ? String() : found->join_address();
}

double DirectoryTransport::timeout_hint() const {
    netw::LobbyDirectory *found = reached();
    return found == nullptr ? 20.0 : found->timeout_hint();
}

void DirectoryTransport::close() {
    netw::LobbyDirectory *found = reached();
    if (found != nullptr) {
        found->leave_lobby();
    }
}

void DirectoryTransport::close_query() {
}

} // namespace netw::connect
