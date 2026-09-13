#include "netw/connect/creation.hpp"

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"

using namespace godot;

namespace netw::connect {

namespace {

RID_Owner<Creation> &creation_owner() {
    static RID_Owner<Creation> instance;
    static bool described = false;
    if (!described) {
        instance.set_description("netw::connect peer creation");
        described = true;
    }
    return instance;
}

LocalVector<PeerOffer> &offers() {
    static LocalVector<PeerOffer> instance;
    return instance;
}

} // namespace

RID mint_creation(const Creation &p_seed) {
    return creation_owner().make_rid(p_seed);
}

Creation *creation_of(const RID &p_ticket) {
    if (!p_ticket.is_valid()) {
        return nullptr;
    }
    return creation_owner().get_or_null(p_ticket);
}

void free_creation(const RID &p_ticket) {
    if (creation_of(p_ticket) != nullptr) {
        creation_owner().free(p_ticket);
    }
}

void offer_open(const PeerOffer &p_offer) {
    offers().push_back(p_offer);
}

PeerOffer *offer_of_peer(const Ref<MultiplayerPeer> &p_peer) {
    if (p_peer.is_null()) {
        return nullptr;
    }
    LocalVector<PeerOffer> &held = offers();
    for (uint32_t at = 0; at < held.size(); at++) {
        if (held[at].peer == p_peer) {
            return &held[at];
        }
    }
    return nullptr;
}

PeerOffer *offer_of_ticket(const RID &p_ticket) {
    if (!p_ticket.is_valid()) {
        return nullptr;
    }
    LocalVector<PeerOffer> &held = offers();
    for (uint32_t at = 0; at < held.size(); at++) {
        if (held[at].ticket == p_ticket) {
            return &held[at];
        }
    }
    return nullptr;
}

void offer_close(const RID &p_ticket) {
    LocalVector<PeerOffer> &held = offers();
    for (uint32_t at = held.size(); at > 0; at--) {
        if (held[at - 1].ticket == p_ticket) {
            held.remove_at(at - 1);
        }
    }
}

} // namespace netw::connect
