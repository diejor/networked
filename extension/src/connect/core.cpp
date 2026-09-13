#include "netw/connect/core.hpp"

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/multiplayer.hpp"
#include "godot/net_peers.hpp"
#include "godot/object.hpp"
#include "godot/resource.hpp"
#include "godot/utility.hpp"
#include "godot/variant.hpp"
#include "netw/api/link_conditions.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/session_config.hpp"
#include "netw/connect/creation.hpp"
#include "netw/connect/directory_transport.hpp"
#include "netw/connect/probe_client.hpp"
#include "netw/connect/transport.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw::connect {

namespace {

const char *OFFLINE_PEER_CLASS = "OfflineMultiplayerPeer";

enum WaitKind {
    WAIT_PROBE,
    WAIT_LIST_PROBE,
    WAIT_CREATE_PEER,
};

struct Waiter {
    ConnectCore *core = nullptr;
    int kind = 0;
    int64_t ticket_ok = 0;
    int64_t ticket_fail = 0;
    bool ready = false;
    bool ok = false;
    Variant value;
    int64_t code = 0;
    String detail;
    Ref<NetwPromise> out;
    RID target;
    RID creation;
    Transport *scratch = nullptr;
};

struct ProbeWait {
    ConnectCore *core = nullptr;
    Ref<MultiplayerPeer> peer;
    Ref<NetwPromise> done;
};

LocalVector<Waiter *> waiters;
LocalVector<ProbeWait *> probe_queue;
int64_t next_ticket = 1;
bool running_steps = false;

int64_t mint_ticket() {
    return next_ticket++;
}

Waiter *make_waiter(ConnectCore *p_core, int p_kind) {
    Waiter *made = new Waiter();
    made->core = p_core;
    made->kind = p_kind;
    waiters.push_back(made);
    return made;
}

Waiter *find_waiter(int64_t p_ticket, bool &r_is_fail) {
    if (p_ticket == 0) {
        return nullptr;
    }
    for (uint32_t at = 0; at < waiters.size(); at++) {
        if (waiters[at]->ticket_ok == p_ticket) {
            r_is_fail = false;
            return waiters[at];
        }
        if (waiters[at]->ticket_fail == p_ticket) {
            r_is_fail = true;
            return waiters[at];
        }
    }
    return nullptr;
}

Waiter *take_ready(ConnectCore *p_core) {
    for (uint32_t at = 0; at < waiters.size(); at++) {
        if (waiters[at]->core == p_core && waiters[at]->ready) {
            Waiter *found = waiters[at];
            waiters.remove_at(at);
            return found;
        }
    }
    return nullptr;
}

void release_waiter(Waiter *p_waiter, NetwMultiplayer *) {
    if (p_waiter->scratch != nullptr) {
        p_waiter->scratch->close();
        delete p_waiter->scratch;
        p_waiter->scratch = nullptr;
    }
    if (p_waiter->out.is_valid() && !p_waiter->out->get_is_settled()) {
        p_waiter->out->reject(ERR_SKIP, "the connect flow was released.");
    }
    free_creation(p_waiter->creation);
    delete p_waiter;
}

void clear_waits(ConnectCore *p_core, NetwMultiplayer *p_session) {
    uint32_t at = 0;
    while (at < waiters.size()) {
        Waiter *held = waiters[at];
        if (held->core == p_core) {
            waiters.remove_at(at);
            release_waiter(held, p_session);
        } else {
            at++;
        }
    }
}

void clear_probe_queue(ConnectCore *p_core) {
    uint32_t at = 0;
    while (at < probe_queue.size()) {
        ProbeWait *held = probe_queue[at];
        if (held->core != p_core) {
            at++;
            continue;
        }
        probe_queue.remove_at(at);
        if (held->done.is_valid() && !held->done->get_is_settled()) {
            held->done->reject(ERR_SKIP, "the probe queue was cleared.");
        }
        delete held;
    }
}

void serve_probes(
    ConnectCore *p_core,
    ProbeClient *&r_prober,
    const ProbeHooks &p_hooks
) {
    if (r_prober != nullptr && r_prober->is_open()) {
        return;
    }
    for (uint32_t at = 0; at < probe_queue.size(); at++) {
        if (probe_queue[at]->core != p_core) {
            continue;
        }
        ProbeWait *next = probe_queue[at];
        probe_queue.remove_at(at);
        if (r_prober == nullptr) {
            r_prober = memnew(ProbeClient);
        }
        r_prober->open(
            next->peer,
            ProbeClient::DEFAULT_TIMEOUT,
            next->done,
            p_hooks
        );
        delete next;
        return;
    }
}

void arm_waiter(Waiter *p_waiter) {
    p_waiter->ready = true;
    if (p_waiter->core != nullptr && !running_steps) {
        p_waiter->core->on_poll(0.0);
    }
}

void step_resolved(Variant p_value, int64_t p_ticket) {
    bool is_fail = false;
    Waiter *found = find_waiter(p_ticket, is_fail);
    if (found == nullptr || found->ready) {
        return;
    }
    found->ok = true;
    found->value = p_value;
    arm_waiter(found);
}

void step_rejected(int64_t p_code, String p_detail, int64_t p_ticket) {
    bool is_fail = false;
    Waiter *found = find_waiter(p_ticket, is_fail);
    if (found == nullptr || found->ready) {
        return;
    }
    found->ok = false;
    found->code = p_code;
    found->detail = p_detail;
    arm_waiter(found);
}

void watch_promise(Waiter *p_waiter, const Ref<NetwPromise> &p_promise) {
    p_waiter->ticket_ok = mint_ticket();
    p_waiter->ticket_fail = mint_ticket();
    p_promise->then(
        callable_mp_static(&step_resolved).bind(p_waiter->ticket_ok)
    );
    p_promise->catch_error(
        callable_mp_static(&step_rejected).bind(p_waiter->ticket_fail)
    );
}

Ref<MultiplayerPeer> peer_of(const Variant &p_value) {
    return Ref<MultiplayerPeer>(
        Object::cast_to<MultiplayerPeer>(gd::live_object(p_value))
    );
}

Ref<NetwServerInfo> info_of(const Variant &p_value) {
    return Ref<NetwServerInfo>(
        Object::cast_to<NetwServerInfo>(gd::live_object(p_value))
    );
}

} // namespace

ConnectCore::~ConnectCore() {
    dispose();
}

void ConnectCore::bind_session(NetwMultiplayer *p_session) {
    session = p_session;
    if (session != nullptr) {
        rows.set_local_app_id(session->session_get_app_id());
    }
}

void ConnectCore::dispose() {
    while (!creations.is_empty()) {
        discard_creation(creations[creations.size() - 1]);
    }
    starts.clear();
    clear_waits(this, session);
    clear_probe_queue(this);
    drop_browsers();
    drop_live();
    if (prober != nullptr) {
        prober->close();
        memdelete(prober);
        prober = nullptr;
    }
    registrations.clear();
}

ProbeHooks ConnectCore::probe_hooks() const {
    return session != nullptr ? session->discovery_probe_hooks() : ProbeHooks();
}

void ConnectCore::adopt_live(Transport *p_transport) {
    if (live == p_transport) {
        return;
    }
    drop_live();
    live = p_transport;
}

void ConnectCore::drop_live() {
    if (live == nullptr) {
        return;
    }
    live->close();
    delete live;
    live = nullptr;
}

std::optional<JoinRequest> request_of(
    const StringName &p_username,
    const Array &p_args
) {
    if (String(p_username).is_empty()) {
        return std::nullopt;
    }
    JoinRequest request;
    request.username = p_username;
    request.arg_values = p_args.duplicate(false);
    return request;
}

Transport *ConnectCore::transport_of_target(
    const RID &p_target,
    String &r_address,
    Dictionary &r_metadata
) const {
    const TargetRow *held = rows.row(p_target);
    if (held == nullptr) {
        return nullptr;
    }
    const TransportSlot *slot = transport_slot(held->transport);
    if (slot == nullptr) {
        return nullptr;
    }
    r_address = held->address;
    r_metadata = held->metadata;
    return make_transport(*slot);
}

Ref<NetwPromise> ConnectCore::probe_row(const RID &p_target) {
    Ref<NetwPromise> out;
    out.instantiate();
    String address;
    Dictionary metadata;
    Transport *made = transport_of_target(p_target, address, metadata);
    if (made == nullptr) {
        out->reject(
            ERR_UNCONFIGURED,
            String("no transport is installed for this target.")
        );
        return out;
    }
    made->bind_session(session);

    Ref<NetwPromise> done;
    done.instantiate();
    Waiter *waiting = make_waiter(this, WAIT_PROBE);
    waiting->out = out;
    waiting->target = p_target;
    waiting->scratch = made;
    waiting->creation = mint_ticket_for(made, PEER_MODE_CLIENT, address);
    watch_promise(waiting, done);

    const Ref<MultiplayerPeer> asking = made->make_probe_peer(address);
    if (asking.is_valid()) {
        ProbeWait *queued = new ProbeWait();
        queued->core = this;
        queued->peer = asking;
        queued->done = done;
        probe_queue.push_back(queued);
        serve_probes(this, prober, probe_hooks());
    } else {
        made->arm(waiting->creation, done);
        made->probe(address);
    }
    return out;
}

RID ConnectCore::target_add(
    const RID &p_transport,
    const String &p_address,
    const String &p_display_name
) {
    const RID standing = rows.find(p_transport, p_address);
    const RID minted = rows.add(p_transport, p_address, p_display_name);
    if (!standing.is_valid() && minted.is_valid() && session != nullptr) {
        session->endpoint_emit_added(minted);
    }
    return minted;
}

void ConnectCore::target_remove(const RID &p_target) {
    if (rows.remove(p_target) && session != nullptr) {
        session->endpoint_emit_removed(p_target);
    }
}

bool ConnectCore::target_is_available(const RID &p_target) const {
    const TargetRow *held = rows.row(p_target);
    if (held == nullptr) {
        return false;
    }
    const TransportSlot *slot = transport_slot(held->transport);
    if (slot == nullptr) {
        return false;
    }
    Transport *shape = make_transport(*slot);
    if (shape == nullptr) {
        return false;
    }
    const bool reachable = shape->is_available();
    delete shape;
    return reachable;
}

void ConnectCore::probe_target(const RID &p_target) {
    if (rows.row(p_target) == nullptr) {
        return;
    }
    Waiter *waiting = make_waiter(this, WAIT_LIST_PROBE);
    waiting->target = p_target;
    watch_promise(waiting, probe_row(p_target));
}

RID ConnectCore::mint_ticket_for(
    Transport *p_made,
    int p_mode,
    const String &p_address
) {
    Creation seed;
    seed.session = session != nullptr ? gd::instance_id(session) : ObjectID();
    seed.mode = p_mode;
    seed.address = p_address;
    seed.peer_class = p_made != nullptr ? p_made->peer_class() : StringName();
    return mint_creation(seed);
}

RID ConnectCore::create_peer(
    const RID &p_transport,
    int p_mode,
    const String &p_address,
    const Dictionary &p_settings,
    const Callable &p_completed,
    const Callable &p_progress
) {
    NETW_ERR_COND_V(
        session == nullptr,
        RID(),
        sys::TRANSPORT,
        "a peer creation needs a live session, and this one is disposed"
    );
    NETW_ERR_COND_V(
        !p_completed.is_valid(),
        RID(),
        sys::TRANSPORT,
        "a peer creation is answered through its completion callable, and "
        "this one is not callable"
    );
    Creation seed;
    seed.session = gd::instance_id(session);
    seed.transport = p_transport;
    seed.mode = p_mode;
    seed.address = p_address;
    seed.settings = p_settings.duplicate(true);
    seed.completed = p_completed;
    seed.progress = p_progress;
    const RID ticket = mint_creation(seed);
    creations.push_back(ticket);
    starts.push_back(ticket);
    return ticket;
}

void ConnectCore::start_creation(const RID &p_ticket) {
    Creation *asked = creation_of(p_ticket);
    if (asked == nullptr || asked->published) {
        return;
    }
    const TransportSlot *slot = transport_slot(asked->transport);
    if (slot == nullptr) {
        settle_creation(
            p_ticket,
            Ref<MultiplayerPeer>(),
            ERR_DOES_NOT_EXIST,
            "this session holds no such transport."
        );
        return;
    }
    Transport *made = make_transport(*slot);
    if (made == nullptr) {
        settle_creation(
            p_ticket,
            Ref<MultiplayerPeer>(),
            ERR_UNAVAILABLE,
            "the transport could not be built."
        );
        return;
    }
    made->bind_session(session);
    asked->provider = made;
    asked->peer_class = made->peer_class();

    Ref<NetwPromise> done;
    done.instantiate();
    Waiter *waiting = make_waiter(this, WAIT_CREATE_PEER);
    waiting->target = p_ticket;
    watch_promise(waiting, done);
    made->arm(p_ticket, done);
    made->make_peer(asked->mode, asked->address, asked->settings);
}

void ConnectCore::forget_creation(const RID &p_ticket) {
    for (uint32_t at = creations.size(); at > 0; at--) {
        if (creations[at - 1] == p_ticket) {
            creations.remove_at(at - 1);
        }
    }
    for (uint32_t at = starts.size(); at > 0; at--) {
        if (starts[at - 1] == p_ticket) {
            starts.remove_at(at - 1);
        }
    }
}

void ConnectCore::discard_creation(const RID &p_ticket) {
    Creation *asked = creation_of(p_ticket);
    if (asked != nullptr && asked->provider != nullptr) {
        Transport *held = asked->provider;
        asked->provider = nullptr;
        held->close();
        delete held;
    }
    forget_creation(p_ticket);
    offer_close(p_ticket);
    free_creation(p_ticket);
}

void ConnectCore::settle_creation(
    const RID &p_ticket,
    const Ref<MultiplayerPeer> &p_peer,
    Error p_error,
    const String &p_detail
) {
    Creation *asked = creation_of(p_ticket);
    if (asked == nullptr || asked->published) {
        return;
    }
    asked->published = true;
    asked->dispatching = true;
    const Callable answering = asked->completed;
    forget_creation(p_ticket);

    if (p_peer.is_valid()) {
        PeerOffer offer;
        offer.ticket = p_ticket;
        offer.session = asked->session;
        offer.peer = p_peer;
        offer_open(offer);
    }

    if (answering.is_valid()) {
        const RID outer = answering_offer;
        answering_offer = p_ticket;
        Array carried;
        carried.push_back(p_peer);
        carried.push_back(int64_t(p_error));
        carried.push_back(p_detail);
        answering.callv(carried);
        answering_offer = outer;
    }

    const PeerOffer *standing = offer_of_peer(p_peer);
    const bool claimed = standing != nullptr && standing->claimed;
    Creation *held = creation_of(p_ticket);
    if (held != nullptr) {
        held->dispatching = false;
    }
    if (!claimed && p_peer.is_valid()) {
        p_peer->close();
    }
    discard_creation(p_ticket);
}

void ConnectCore::cancel_peer_creation(const RID &p_ticket) {
    Creation *asked = creation_of(p_ticket);
    if (asked == nullptr || asked->published || asked->dispatching) {
        return;
    }
    if (session == nullptr || asked->session != gd::instance_id(session)) {
        return;
    }
    if (asked->provider != nullptr) {
        asked->provider->cancel_peer_creation();
    }
    settle_creation(p_ticket, Ref<MultiplayerPeer>(), ERR_SKIP, String());
}

bool ConnectCore::claim_offer(const Ref<MultiplayerPeer> &p_peer) {
    PeerOffer *offered = offer_of_peer(p_peer);
    if (offered == nullptr) {
        offered = offer_of_ticket(answering_offer);
    }
    if (offered == nullptr || offered->claimed || session == nullptr
        || offered->session != gd::instance_id(session)) {
        return false;
    }
    offered->claimed = true;
    Creation *asked = creation_of(offered->ticket);
    if (asked != nullptr && asked->provider != nullptr) {
        Transport *taken = asked->provider;
        asked->provider = nullptr;
        taken->bind_session(session);
        taken->adopt(p_peer);
        adopt_live(taken);
    }
    return true;
}

String ConnectCore::join_address() const {
    return live != nullptr ? live->join_address() : String();
}

Dictionary ConnectCore::diagnostics(int64_t p_peer_id) const {
    return live != nullptr ? live->diagnostics(p_peer_id) : Dictionary();
}

bool ConnectCore::transport_facts(
    const TransportSlot &p_slot,
    TransportFacts &r_facts
) const {
    Transport *shape = make_transport(p_slot);
    if (shape == nullptr) {
        return false;
    }
    r_facts = facts_of(*shape);
    delete shape;
    return true;
}

void ConnectCore::on_peer_assigned(const Ref<MultiplayerPeer> &p_peer) {
    if (p_peer.is_null()
        || peer_class_of(p_peer) == StringName(OFFLINE_PEER_CLASS)) {
        drop_live();
        return;
    }
    if (claim_offer(p_peer)) {
        return;
    }
    Transport *found = registrations.make_for_peer(p_peer);
    if (found == nullptr) {
        found = TransportBook::shared().make_for_peer(p_peer);
    }
    if (found == nullptr) {
        found = make_transport(peer_class_of(p_peer));
    }
    if (found != nullptr) {
        found->bind_session(session);
        found->adopt(p_peer);
        adopt_live(found);
    }
}

void ConnectCore::on_poll(double p_delta) {
    if (p_delta > 0.0) {
        if (live != nullptr) {
            live->poll(p_delta);
        }
        const LocalVector<RID> outstanding(creations);
        for (uint32_t at = 0; at < outstanding.size(); at++) {
            Creation *asked = creation_of(outstanding[at]);
            if (asked != nullptr && asked->provider != nullptr) {
                asked->provider->poll(p_delta);
            }
        }
        for (uint32_t at = 0; at < browsers.size(); at++) {
            browsers[at]->poll(p_delta);
        }
        if (prober != nullptr && prober->is_open()) {
            prober->poll(p_delta);
        }
        serve_probes(this, prober, probe_hooks());
    }

    if (running_steps) {
        return;
    }
    running_steps = true;
    while (!starts.is_empty()) {
        String pending;
        const Error settled = session != nullptr
            ? session->config_readiness(pending)
            : Error(OK);
        if (settled == ERR_BUSY) {
            break;
        }
        const RID opening = starts[0];
        starts.remove_at(0);
        if (settled != OK) {
            settle_creation(
                opening,
                Ref<MultiplayerPeer>(),
                settled,
                "this session has no single configuration to bring up with."
            );
            continue;
        }
        start_creation(opening);
    }
    Waiter *step = take_ready(this);
    while (step != nullptr) {
        switch (step->kind) {
            case WAIT_PROBE: {
                if (!step->ok) {
                    step->out->reject(
                        static_cast<Error>(step->code),
                        step->detail
                    );
                    break;
                }
                const Ref<NetwServerInfo> answer = info_of(step->value);
                const int64_t verdict = rows.classify(answer, OK);
                if (verdict != int64_t(OK)) {
                    step->out->reject(
                        static_cast<Error>(verdict),
                        "the probe answered a foreign session."
                    );
                } else {
                    step->out->resolve(answer);
                }
            } break;
            case WAIT_LIST_PROBE: {
                const Ref<NetwServerInfo> answer
                    = step->ok ? info_of(step->value) : Ref<NetwServerInfo>();
                const int64_t code = step->ok ? int64_t(OK) : step->code;
                rows.probe_settled(step->target, code, answer);
                if (session != nullptr && rows.row(step->target) != nullptr) {
                    session->endpoint_emit_updated(step->target);
                }
            } break;
            case WAIT_CREATE_PEER: {
                if (step->ok) {
                    const Ref<MultiplayerPeer> made = peer_of(step->value);
                    if (made.is_valid()) {
                        settle_creation(step->target, made, OK, String());
                    } else {
                        settle_creation(
                            step->target,
                            Ref<MultiplayerPeer>(),
                            ERR_BUG,
                            "the transport delivered no peer."
                        );
                    }
                } else {
                    settle_creation(
                        step->target,
                        Ref<MultiplayerPeer>(),
                        Error(step->code),
                        step->detail
                    );
                }
            } break;
            default:
                break;
        }
        release_waiter(step, session);
        step = take_ready(this);
    }
    running_steps = false;
}

Transport *ConnectCore::make_transport(const StringName &p_peer_class) {
    Transport *made = registrations.make(p_peer_class);
    if (made == nullptr) {
        made = TransportBook::shared().make(p_peer_class);
    }
    if (made != nullptr) {
        return made;
    }
    if (session == nullptr) {
        return nullptr;
    }
    Object *found = session->discovery_find_directory(p_peer_class);
    return found != nullptr ? new DirectoryTransport(found) : nullptr;
}

Transport *ConnectCore::make_transport(const TransportSlot &p_slot) const {
    switch (p_slot.source_kind) {
        case TRANSPORT_SOURCE_DIRECTORY: {
            Object *named = gd::instance_from_id(p_slot.directory);
            return named != nullptr ? new DirectoryTransport(named) : nullptr;
        }
        case TRANSPORT_SOURCE_REGISTRATION:
            return make_from_script(p_slot.script);
        default:
            return TransportBook::shared().make(p_slot.peer_class);
    }
}

RID ConnectCore::register_transport(const Ref<Script> &p_type) {
    const ObjectID owner
        = session != nullptr ? gd::instance_id(session) : ObjectID();
    return registrations.register_script(p_type, owner);
}

Error ConnectCore::unregister_transport(const RID &p_transport) {
    const TransportSlot *slot = transport_slot(p_transport);
    if (slot == nullptr || slot->source_kind != TRANSPORT_SOURCE_REGISTRATION) {
        return ERR_DOES_NOT_EXIST;
    }
    const StringName named = slot->peer_class;
    const Error dropped = registrations.unregister(p_transport);
    if (dropped != OK) {
        return dropped;
    }
    for (uint32_t at = browsers.size(); at > 0; at--) {
        if (browsers[at - 1]->peer_class() != named) {
            continue;
        }
        browsers[at - 1]->close_query();
        delete browsers[at - 1];
        browsers.remove_at(at - 1);
    }
    const LocalVector<RID> outstanding(creations);
    for (uint32_t at = 0; at < outstanding.size(); at++) {
        const Creation *asked = creation_of(outstanding[at]);
        if (asked == nullptr || asked->published) {
            continue;
        }
        if (asked->transport != p_transport && asked->peer_class != named) {
            continue;
        }
        if (asked->provider != nullptr) {
            asked->provider->cancel_peer_creation();
        }
        settle_creation(
            outstanding[at],
            Ref<MultiplayerPeer>(),
            ERR_UNAVAILABLE,
            "the transport was unregistered before it delivered a peer."
        );
    }
    return OK;
}

Transport *ConnectCore::transport_of_directory(int64_t p_directory) const {
    DirectoryTransport *held = dynamic_cast<DirectoryTransport *>(live);
    if (held != nullptr
        && int64_t(uint64_t(held->directory_id())) == p_directory) {
        return held;
    }
    for (uint32_t at = 0; at < creations.size(); at++) {
        const Creation *asked = creation_of(creations[at]);
        if (asked == nullptr) {
            continue;
        }
        held = dynamic_cast<DirectoryTransport *>(asked->provider);
        if (held != nullptr
            && int64_t(uint64_t(held->directory_id())) == p_directory) {
            return held;
        }
    }
    for (uint32_t at = 0; at < browsers.size(); at++) {
        held = dynamic_cast<DirectoryTransport *>(browsers[at]);
        if (held != nullptr
            && int64_t(uint64_t(held->directory_id())) == p_directory) {
            return held;
        }
    }
    return nullptr;
}

void ConnectCore::on_directory_delivered(
    const Ref<MultiplayerPeer> &p_peer,
    int64_t p_directory
) {
    Transport *reached = transport_of_directory(p_directory);
    if (reached != nullptr) {
        reached->deliver(p_peer);
    }
}

void ConnectCore::on_directory_failed(
    int64_t p_error,
    const String &p_message,
    int64_t p_directory
) {
    Transport *reached = transport_of_directory(p_directory);
    if (reached != nullptr) {
        reached->fail(Error(p_error), p_message);
    }
}

void ConnectCore::on_directory_listed(
    const PackedStringArray &p_addresses,
    const PackedStringArray &p_names,
    const Array &p_infos,
    int64_t p_directory
) {
    Transport *reached = transport_of_directory(p_directory);
    if (reached != nullptr) {
        reached->publish_targets(p_addresses, p_names, p_infos);
    }
}

void ConnectCore::publish_targets(
    const StringName &p_peer_class,
    const PackedStringArray &p_addresses,
    const PackedStringArray &p_names,
    const Array &p_infos
) {
    if (session == nullptr) {
        return;
    }
    const RID transport = session->transport_find(p_peer_class);
    LocalVector<RID> dropped;
    LocalVector<RID> added;
    LocalVector<RID> updated;
    rows.publish_directory(
        p_peer_class,
        transport,
        p_addresses,
        p_names,
        p_infos,
        dropped,
        added,
        updated
    );
    for (uint32_t at = 0; at < dropped.size(); at++) {
        session->endpoint_emit_removed(dropped[at]);
    }
    for (uint32_t at = 0; at < added.size(); at++) {
        session->endpoint_emit_added(added[at]);
    }
    for (uint32_t at = 0; at < updated.size(); at++) {
        session->endpoint_emit_updated(updated[at]);
    }
}

void ConnectCore::open_browsers() {
    Array classes = registrations.peer_classes();
    const Array stock = TransportBook::shared().peer_classes();
    for (int64_t at = 0; at < stock.size(); at++) {
        if (!classes.has(stock[at])) {
            classes.push_back(stock[at]);
        }
    }
    if (session != nullptr) {
        const Array directed = session->discovery_directory_peer_classes();
        for (int64_t at = 0; at < directed.size(); at++) {
            if (!classes.has(directed[at])) {
                classes.push_back(directed[at]);
            }
        }
    }
    for (int64_t at = 0; at < classes.size(); at++) {
        const StringName named(classes[at]);
        bool held = false;
        for (uint32_t seen = 0; seen < browsers.size(); seen++) {
            held = held || browsers[seen]->peer_class() == named;
        }
        if (held) {
            continue;
        }
        Transport *made = make_transport(named);
        if (made == nullptr) {
            continue;
        }
        if (!made->can_browse()) {
            delete made;
            continue;
        }
        made->bind_session(session);
        browsers.push_back(made);
    }
    for (uint32_t at = 0; at < browsers.size(); at++) {
        browsers[at]->browse();
    }
}

void ConnectCore::drop_browsers() {
    for (uint32_t at = 0; at < browsers.size(); at++) {
        browsers[at]->close_query();
        delete browsers[at];
    }
    browsers.clear();
}

void ConnectCore::refresh() {
    open_browsers();
    rows.enqueue_caller_rows();
    RID target;
    while (rows.next_to_probe(target)) {
        Waiter *waiting = make_waiter(this, WAIT_LIST_PROBE);
        waiting->target = target;
        watch_promise(waiting, probe_row(target));
    }
}

} // namespace netw::connect
