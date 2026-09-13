#pragma once

#include "godot/callable.hpp"
#include "godot/multiplayer.hpp"
#include "godot/object.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"

namespace netw::connect {

class Transport;

enum PeerMode {
    PEER_MODE_HOST,
    PEER_MODE_CLIENT,
};

enum CreationKind {
    CREATION_PEER,
    CREATION_PROBE,
};

struct Creation {
    godot::ObjectID session;
    godot::RID transport;
    godot::StringName peer_class;
    int kind = CREATION_PEER;
    int mode = PEER_MODE_HOST;
    godot::String address;
    godot::Dictionary settings;
    godot::Callable completed;
    godot::Callable progress;
    Transport *provider = nullptr;
    bool published = false;
    bool dispatching = false;
};

godot::RID mint_creation(const Creation &p_seed);
Creation *creation_of(const godot::RID &p_ticket);
void free_creation(const godot::RID &p_ticket);

struct PeerOffer {
    godot::RID ticket;
    godot::ObjectID session;
    godot::Ref<godot::MultiplayerPeer> peer;
    bool claimed = false;
};

void offer_open(const PeerOffer &p_offer);
PeerOffer *offer_of_peer(const godot::Ref<godot::MultiplayerPeer> &p_peer);
PeerOffer *offer_of_ticket(const godot::RID &p_ticket);
void offer_close(const godot::RID &p_ticket);

} // namespace netw::connect
