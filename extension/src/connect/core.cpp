#include "netw/connect/core.hpp"

#include <cmath>

#include "godot/callable.hpp"
#include "godot/local_vector.hpp"
#include "godot/multiplayer.hpp"
#include "godot/net_peers.hpp"
#include "godot/object.hpp"
#include "godot/resource.hpp"
#include "godot/time.hpp"
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
#include "netw/session_core.hpp"

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
    int64_t began_msec = 0;
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

void overwrite_declared(Dictionary &r_settings, const Dictionary &p_preset) {
    const Array declared = r_settings.keys();
    for (int64_t at = 0; at < declared.size(); at++) {
        if (p_preset.has(declared[at])) {
            r_settings[declared[at]] = p_preset[declared[at]];
        }
    }
}

Waiter *make_waiter(ConnectCore *p_core, int p_kind) {
    Waiter *made = new Waiter();
    made->core = p_core;
    made->kind = p_kind;
    made->began_msec = int64_t(Time::get_singleton()->get_ticks_msec());
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

void clear_waits_of_kind(
    ConnectCore *p_core,
    NetwMultiplayer *p_session,
    int p_kind
) {
    uint32_t at = 0;
    while (at < waiters.size()) {
        Waiter *held = waiters[at];
        if (held->core == p_core && held->kind == p_kind) {
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

void assign_created(
    const Ref<MultiplayerPeer> &p_peer,
    int64_t p_error,
    const String &,
    uint64_t p_session
) {
    NetwMultiplayer *session
        = Object::cast_to<NetwMultiplayer>(gd::object_of(ObjectID(p_session)));
    if (session == nullptr || p_error != OK || p_peer.is_null()
        || !session->connect_plane().answers_attempt()) {
        return;
    }
    session->NETW_API_VIRTUAL(set_multiplayer_peer)(p_peer);
}

void heard_through_failed(int64_t, const String &) {
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
    browsing = false;
    const Ref<NetwPromise> pending = attempt.promise;
    attempt = Attempt();
    if (pending.is_valid()) {
        pending->reject(ERR_SKIP, "The session closed.");
    }
    defaults.clear();
    announced_address = String();
    while (!creations.is_empty()) {
        discard_creation(creations[creations.size() - 1]);
    }
    starts.clear();
    clear_waits(this, session);
    clear_probe_queue(this);
    drop_browsers();
    release_live();
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
    release_live();
    live = p_transport;
    announce_join_address();
}

void ConnectCore::drop_live() {
    release_live();
    announce_join_address();
}

void ConnectCore::release_live() {
    joined_target = RID();
    joined_address = String();
    advertised_address = String();
    advertised_players = 0;
    if (live == nullptr) {
        return;
    }
    live->close();
    delete live;
    live = nullptr;
}

void ConnectCore::announce_join_address() {
    const String now = join_address();
    if (now == announced_address) {
        return;
    }
    announced_address = now;
    if (session != nullptr) {
        session->connection_emit_join_address_changed(now);
    }
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
    const String address = target_address(p_transport, p_address);
    const RID standing = rows.find(p_transport, address);
    const RID minted = rows.add(p_transport, address, p_display_name);
    if (!standing.is_valid() && minted.is_valid() && session != nullptr) {
        session->endpoint_emit_added(minted);
    }
    return minted;
}

RID ConnectCore::target_find(
    const RID &p_transport,
    const String &p_address
) const {
    return rows.find(p_transport, target_address(p_transport, p_address));
}

String ConnectCore::target_address(
    const RID &p_transport,
    const String &p_address
) const {
    if (!p_address.is_empty()) {
        return p_address;
    }
    const TransportSlot *slot = transport_slot(p_transport);
    Transport *shape = slot != nullptr ? make_transport(*slot) : nullptr;
    if (shape == nullptr) {
        return p_address;
    }
    const String reached = shape->default_address();
    delete shape;
    return reached;
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
    if (!rows.begin_probe(p_target)) {
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
    const Callable &p_completed
) {
    const RID ticket = open_creation(
        p_transport,
        p_mode,
        p_address,
        Dictionary(),
        p_settings,
        p_completed
    );
    if (ticket.is_valid() && attempt.state == ATTEMPT_OFFLINE) {
        begin_attempt(ticket, p_mode, Ref<NetwPromise>());
    }
    return ticket;
}

Dictionary ConnectCore::layered_settings(
    const RID &p_transport,
    const Dictionary &p_metadata,
    const Dictionary &p_settings
) const {
    Dictionary layered;
    const TransportSlot *slot = transport_slot(p_transport);
    const Dictionary *preset
        = slot != nullptr ? defaults.getptr(slot->peer_class) : nullptr;
    if (preset != nullptr) {
        layered.merge(*preset, true);
    }
    layered.merge(p_metadata, true);
    layered.merge(p_settings, true);
    return layered.duplicate(true);
}

Error ConnectCore::set_defaults(
    const RID &p_transport,
    const Dictionary &p_settings
) {
    const TransportSlot *slot = transport_slot(p_transport);
    if (slot == nullptr) {
        return ERR_DOES_NOT_EXIST;
    }
    defaults[slot->peer_class] = p_settings.duplicate(true);
    return OK;
}

Dictionary ConnectCore::defaults_of(const RID &p_transport) const {
    const TransportSlot *slot = transport_slot(p_transport);
    if (slot == nullptr) {
        return Dictionary();
    }
    const Dictionary *preset = defaults.getptr(slot->peer_class);
    return preset != nullptr ? preset->duplicate(true) : Dictionary();
}

Ref<NetwPromise> ConnectCore::join(
    const RID &p_transport,
    const String &p_address,
    const Dictionary &p_settings
) {
    return start_attempt(
        p_transport,
        PEER_MODE_CLIENT,
        p_address,
        Dictionary(),
        p_settings
    );
}

Ref<NetwPromise> ConnectCore::join_target(
    const RID &p_target,
    const Dictionary &p_settings
) {
    const TargetRow *held = rows.row(p_target);
    if (held == nullptr) {
        return NetwPromise::rejected(
            ERR_DOES_NOT_EXIST,
            "This endpoint is no longer listed."
        );
    }
    return start_attempt(
        held->transport,
        PEER_MODE_CLIENT,
        held->address,
        held->metadata,
        p_settings
    );
}

Ref<NetwPromise> ConnectCore::host(
    const RID &p_transport,
    const Dictionary &p_settings
) {
    return start_attempt(
        p_transport,
        PEER_MODE_HOST,
        String(),
        Dictionary(),
        p_settings
    );
}

Ref<NetwPromise> ConnectCore::start_attempt(
    const RID &p_transport,
    int p_mode,
    const String &p_address,
    const Dictionary &p_metadata,
    const Dictionary &p_settings
) {
    if (attempt.state == ATTEMPT_ONLINE) {
        return NetwPromise::rejected(
            ERR_ALREADY_IN_USE,
            "This session is already online. Leave it before joining or "
            "hosting again."
        );
    }
    cancel_attempt();
    Ref<NetwPromise> outcome;
    outcome.instantiate();
    const RID ticket = open_creation(
        p_transport,
        p_mode,
        p_address,
        p_metadata,
        p_settings,
        callable_mp_static(&assign_created)
            .bind(uint64_t(gd::instance_id(session)))
    );
    if (!ticket.is_valid()) {
        outcome->reject(ERR_UNCONFIGURED, "This session is closed.");
        return outcome;
    }
    begin_attempt(ticket, p_mode, outcome);
    return outcome;
}

bool ConnectCore::answers_attempt() const {
    return attempt.state == ATTEMPT_CREATING && attempt.ticket.is_valid()
        && attempt.ticket == answering_offer;
}

void ConnectCore::begin_attempt(
    const RID &p_ticket,
    int p_mode,
    const Ref<NetwPromise> &p_promise
) {
    attempt = Attempt();
    attempt.ticket = p_ticket;
    attempt.mode = p_mode;
    attempt.promise = p_promise;
    move_attempt(ATTEMPT_CREATING);
}

void ConnectCore::move_attempt(int p_state) {
    if (attempt.state == p_state) {
        return;
    }
    attempt.state = p_state;
    if (session != nullptr) {
        session->connection_attempt_moved(p_state);
    }
}

void ConnectCore::end_attempt(Error p_error, const String &p_reason) {
    const Attempt ended = attempt;
    attempt = Attempt();
    announce_end(ended, p_error, p_reason);
}

void ConnectCore::release_attempt(const Attempt &p_ended) {
    if (p_ended.state == ATTEMPT_CREATING) {
        cancel_peer_creation(p_ended.ticket);
    } else if (p_ended.state == ATTEMPT_CONNECTING && session != nullptr) {
        session->NETW_API_VIRTUAL(set_multiplayer_peer)(Ref<MultiplayerPeer>());
    }
}

void ConnectCore::announce_end(
    const Attempt &p_ended,
    Error p_error,
    const String &p_reason
) {
    const bool heard = session != nullptr && p_error != OK
        && p_error != ERR_SKIP
        && session->connection_emit_failed(p_error, p_reason);
    if (session != nullptr && p_ended.state != ATTEMPT_OFFLINE) {
        session->connection_attempt_moved(ATTEMPT_OFFLINE);
    }
    if (p_ended.promise.is_valid()) {
        if (heard) {
            p_ended.promise->catch_error(
                callable_mp_static(&heard_through_failed)
            );
        }
        p_ended.promise->reject(p_error, p_reason);
    }
}

void ConnectCore::cancel_attempt() {
    if (attempt.state != ATTEMPT_CREATING
        && attempt.state != ATTEMPT_CONNECTING) {
        return;
    }
    const Attempt ended = attempt;
    attempt = Attempt();
    release_attempt(ended);
    announce_end(ended, ERR_SKIP, "The attempt was cancelled.");
}

void ConnectCore::tick_attempt(double p_delta) {
    if (attempt.state != ATTEMPT_CREATING
        && attempt.state != ATTEMPT_CONNECTING) {
        return;
    }
    attempt.elapsed += p_delta;
    if (attempt.deadline <= 0.0) {
        return;
    }
    if (attempt.elapsed >= attempt.deadline) {
        const Attempt ended = attempt;
        attempt = Attempt();
        release_attempt(ended);
        announce_end(
            ended,
            ERR_TIMEOUT,
            vformat(
                "No answer after %d seconds.",
                int64_t(std::ceil(ended.deadline))
            )
        );
        return;
    }
    if (!attempt.reported && session != nullptr) {
        session->connection_emit_progressed(
            String(),
            attempt.elapsed / attempt.deadline
        );
    }
}

void ConnectCore::on_progress(
    const RID &p_ticket,
    const String &p_message,
    double p_ratio
) {
    if (!p_ticket.is_valid() || p_ticket != attempt.ticket
        || (attempt.state != ATTEMPT_CREATING
            && attempt.state != ATTEMPT_CONNECTING)) {
        return;
    }
    if (p_ratio >= 0.0) {
        attempt.reported = true;
    }
    if (session != nullptr) {
        session->connection_emit_progressed(p_message, p_ratio);
    }
}

void ConnectCore::on_session_edge(int p_old, int p_new) {
    if (p_new == SessionCore::STATE_CONNECTING) {
        if (attempt.state == ATTEMPT_OFFLINE) {
            attempt = Attempt();
        }
        if (attempt.state == ATTEMPT_OFFLINE
            || attempt.state == ATTEMPT_CREATING) {
            move_attempt(ATTEMPT_CONNECTING);
        }
        return;
    }
    if (p_new != SessionCore::STATE_OFFLINE) {
        return;
    }
    if (attempt.state == ATTEMPT_CONNECTING
        || attempt.state == ATTEMPT_ONLINE) {
        end_attempt(ERR_SKIP, "The peer was unassigned.");
    }
    announce_join_address();
}

void ConnectCore::on_server_answered() {
    attempt.deadline = 0.0;
    attempt.answered = true;
}

void ConnectCore::on_session_left() {
    drop_live();
}

void ConnectCore::on_session_online() {
    const Ref<NetwPromise> outcome = attempt.promise;
    attempt.ticket = RID();
    attempt.promise.unref();
    attempt.deadline = 0.0;
    TargetRow *joined = rows.row(joined_target);
    const bool acknowledged = joined != nullptr && joined->invite_pending;
    if (acknowledged) {
        joined->invite_pending = false;
        joined->invited_by = String();
    }
    move_attempt(ATTEMPT_ONLINE);
    if (acknowledged) {
        session->endpoint_emit_updated(joined_target);
    }
    announce_join_address();
    if (outcome.is_valid()) {
        outcome->resolve(int64_t(OK));
    }
}

void ConnectCore::on_transport_failed() {
    if (attempt.state != ATTEMPT_CONNECTING) {
        return;
    }
    if (attempt.answered) {
        end_attempt(
            ERR_CONNECTION_ERROR,
            "The server refused the join or ran out of time before admitting "
            "the player. Check the server's log, and raise "
            "NetwAdmissionConfig.timeout on the server if the player needs "
            "more time."
        );
        return;
    }
    end_attempt(ERR_CANT_CONNECT, "The server could not be reached.");
}

void ConnectCore::on_join_refused(Error p_error, const String &p_reason) {
    if (attempt.state == ATTEMPT_CONNECTING) {
        end_attempt(p_error, p_reason);
        return;
    }
    if (session != nullptr) {
        session->connection_emit_failed(p_error, p_reason);
    }
}

RID ConnectCore::open_creation(
    const RID &p_transport,
    int p_mode,
    const String &p_address,
    const Dictionary &p_metadata,
    const Dictionary &p_settings,
    const Callable &p_completed
) {
    NETW_ERR_COND_V(
        session == nullptr,
        RID(),
        sys::TRANSPORT,
        "NetwMultiplayer.transport_create_peer ran on a session that is "
        "already closed."
    );
    NETW_ERR_COND_V(
        !p_completed.is_valid(),
        RID(),
        sys::TRANSPORT,
        "NetwMultiplayer.transport_create_peer needs a valid completion "
        "Callable."
    );
    Creation seed;
    seed.session = gd::instance_id(session);
    seed.transport = p_transport;
    seed.mode = p_mode;
    seed.address = p_address;
    seed.settings = layered_settings(p_transport, p_metadata, p_settings);
    seed.completed = p_completed;
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
    if (attempt.ticket == p_ticket) {
        attempt.deadline = made->timeout_hint();
    }
    Dictionary built = asked->mode == PEER_MODE_HOST ? made->host_settings()
                                                     : made->client_settings();
    built.merge(asked->settings, true);
    asked->settings = built;

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
    if (attempt.ticket != p_ticket || attempt.state != ATTEMPT_CREATING) {
        return;
    }
    if (p_error != OK) {
        end_attempt(p_error, p_detail);
    } else if (attempt.promise.is_valid()) {
        end_attempt(ERR_CANT_CREATE, "The session refused the new peer.");
    } else {
        end_attempt(ERR_SKIP, String());
    }
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
        const String address
            = asked->mode == PEER_MODE_CLIENT ? asked->address : String();
        const RID target = asked->mode == PEER_MODE_CLIENT
            ? rows.find(asked->transport, asked->address)
            : RID();
        adopt_live(taken);
        joined_target = target;
        joined_address = address;
    }
    return true;
}

bool ConnectCore::live_online() const {
    return live != nullptr && session != nullptr && session->is_online();
}

String ConnectCore::endpoint_address() const {
    if (!live_online()) {
        return String();
    }
    return joined_address.is_empty() ? live->join_address() : joined_address;
}

String ConnectCore::join_address() const {
    if (!live_online()) {
        return String();
    }
    if (!advertised_address.is_empty()) {
        return advertised_address;
    }
    if (session->has_server_role()) {
        const String served = live->join_address();
        if (!served.is_empty()) {
            return served;
        }
    }
    return endpoint_address();
}

void ConnectCore::advertise(const String &p_address, int64_t p_max_players) {
    advertised_address = p_address;
    advertised_players = p_max_players;
    announce_join_address();
}

int64_t ConnectCore::advertised_max_players() const {
    return live_online() ? advertised_players : 0;
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
    const Dictionary *preset = defaults.getptr(p_slot.peer_class);
    if (preset != nullptr) {
        overwrite_declared(r_facts.host_settings, *preset);
        overwrite_declared(r_facts.client_settings, *preset);
    }
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
            browsers[at].transport->poll(p_delta);
        }
        if (prober != nullptr && prober->is_open()) {
            prober->poll(p_delta);
        }
        serve_probes(this, prober, probe_hooks());
        if (browsing) {
            tick_browsing(p_delta);
        }
        tick_attempt(p_delta);
    }

    if (running_steps) {
        return;
    }
    running_steps = true;
    while (!starts.is_empty()) {
        const Error settled = session != nullptr
            ? session->apply_pending_config(false, "the connection")
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
                "the Netw.configure_* settings of this session could not be "
                "applied."
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
                if (answer.is_valid() && answer->get_latency_ms() < 0) {
                    answer->set_latency_ms(
                        int64_t(Time::get_singleton()->get_ticks_msec())
                        - step->began_msec
                    );
                }
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
    Transport *made = make_creator(p_peer_class);
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
        Transport *held = browsers[at - 1].transport;
        if (held->peer_class() != named) {
            continue;
        }
        browsers.remove_at(at - 1);
        held->close_query();
        delete held;
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
        held = dynamic_cast<DirectoryTransport *>(browsers[at].transport);
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
        publish_targets(
            reached->peer_class(),
            ObjectID(uint64_t(p_directory)),
            p_addresses,
            p_names,
            p_infos
        );
    }
}

RID ConnectCore::invite(
    const RID &p_transport,
    const String &p_address,
    const String &p_from_name
) {
    const StringName named = session->transport_class_of(p_transport);
    if (named.is_empty() || p_address.is_empty()) {
        return RID();
    }
    ObjectID source;
    for (const ObjectID &directory : session->discovery_directories()) {
        if (DirectoryTransport::peer_class_of_directory(
                gd::object_of(directory)
            )
            == named) {
            source = directory;
            break;
        }
    }
    bool minted = false;
    const RID target
        = rows.invite(named, source, p_transport, p_address, minted);
    const bool already_there = live_online() && live->peer_class() == named
        && (p_address == endpoint_address() || p_address == join_address());
    if (already_there) {
        if (minted) {
            session->endpoint_emit_added(target);
        }
        return target;
    }
    TargetRow *held = rows.row(target);
    if (held != nullptr) {
        held->invite_pending = true;
        held->invited_by = p_from_name;
    }
    if (minted) {
        session->endpoint_emit_added(target);
    } else {
        session->endpoint_emit_updated(target);
    }
    session->connection_emit_invited(target);
    return target;
}

void ConnectCore::forget_directory(ObjectID p_directory) {
    for (int32_t at = int32_t(browsers.size()) - 1; at >= 0; at--) {
        DirectoryTransport *held = dynamic_cast<DirectoryTransport *>(
            browsers[uint32_t(at)].transport
        );
        if (held != nullptr && held->directory_id() == p_directory) {
            browsers.remove_at(uint32_t(at));
            delete held;
        }
    }
    LocalVector<RID> dropped;
    rows.forget_source(p_directory, dropped);
    for (uint32_t at = 0; session != nullptr && at < dropped.size(); at++) {
        session->endpoint_emit_removed(dropped[at]);
    }
}

void ConnectCore::publish_targets(
    const StringName &p_peer_class,
    ObjectID p_source,
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
        p_source,
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

Transport *ConnectCore::make_creator(const StringName &p_peer_class) const {
    Transport *made = registrations.make(p_peer_class);
    return made != nullptr ? made : TransportBook::shared().make(p_peer_class);
}

bool ConnectCore::open_browser(Transport *p_made) {
    if (!p_made->can_browse()) {
        delete p_made;
        return false;
    }
    p_made->bind_session(session);
    Browser opened;
    opened.transport = p_made;
    browsers.push_back(opened);
    return true;
}

void ConnectCore::open_browsers() {
    Array classes = registrations.peer_classes();
    const Array stock = TransportBook::shared().peer_classes();
    for (int64_t at = 0; at < stock.size(); at++) {
        if (!classes.has(stock[at])) {
            classes.push_back(stock[at]);
        }
    }
    for (int64_t at = 0; at < classes.size(); at++) {
        const StringName named(classes[at]);
        bool held = false;
        for (uint32_t seen = 0; seen < browsers.size(); seen++) {
            const Transport *browsing_with = browsers[seen].transport;
            held = held
                || (dynamic_cast<const DirectoryTransport *>(browsing_with)
                        == nullptr
                    && browsing_with->peer_class() == named);
        }
        Transport *made = held ? nullptr : make_creator(named);
        if (made != nullptr) {
            open_browser(made);
        }
    }
    LocalVector<ObjectID> directories;
    if (session != nullptr) {
        directories = session->discovery_directories();
    }
    for (uint32_t at = 0; at < directories.size(); at++) {
        bool held = false;
        for (uint32_t seen = 0; seen < browsers.size(); seen++) {
            const DirectoryTransport *listing
                = dynamic_cast<DirectoryTransport *>(browsers[seen].transport);
            held = held
                || (listing != nullptr
                    && listing->directory_id() == directories[at]);
        }
        if (!held) {
            open_browser(
                new DirectoryTransport(gd::object_of(directories[at]))
            );
        }
    }
}

void ConnectCore::drop_browsers() {
    while (!browsers.is_empty()) {
        Transport *held = browsers[browsers.size() - 1].transport;
        browsers.remove_at(browsers.size() - 1);
        held->close_query();
        delete held;
    }
}

void ConnectCore::watch_directory(Object *p_directory) {
    if (!browsing || !DirectoryTransport::is_directory(p_directory)) {
        return;
    }
    const ObjectID named = gd::instance_id(p_directory);
    for (uint32_t at = 0; at < browsers.size(); at++) {
        const DirectoryTransport *listing
            = dynamic_cast<DirectoryTransport *>(browsers[at].transport);
        if (listing != nullptr && listing->directory_id() == named) {
            return;
        }
    }
    open_browser(new DirectoryTransport(p_directory));
}

void ConnectCore::tick_browsing(double p_delta) {
    for (uint32_t at = 0; browsing && at < browsers.size(); at++) {
        Browser &held = browsers[at];
        const double interval = held.transport->list_interval();
        if (held.listed) {
            if (interval <= 0.0) {
                continue;
            }
            held.next_list -= p_delta;
            if (held.next_list > 0.0) {
                continue;
            }
        }
        held.listed = true;
        held.next_list = interval;
        held.transport->browse();
    }
    if (browsing) {
        rows.enqueue_due(p_delta, probe_interval);
        start_due_probes();
    }
}

void ConnectCore::start_due_probes() {
    RID target;
    while (browsing && rows.next_to_probe(target)) {
        Waiter *waiting = make_waiter(this, WAIT_LIST_PROBE);
        waiting->target = target;
        watch_promise(waiting, probe_row(target));
    }
}

void ConnectCore::drop_row_probes() {
    clear_waits_of_kind(this, session, WAIT_LIST_PROBE);
    clear_waits_of_kind(this, session, WAIT_PROBE);
    clear_probe_queue(this);
    if (prober != nullptr && prober->is_open()) {
        prober->close();
    }
    rows.drop_probes();
}

void ConnectCore::set_browsing(bool p_browsing) {
    if (browsing == p_browsing) {
        return;
    }
    browsing = p_browsing;
    if (!browsing) {
        drop_browsers();
        drop_row_probes();
        return;
    }
    open_browsers();
    rows.arm_caller_rows();
    rows.enqueue_due(0.0, probe_interval);
    start_due_probes();
}

void ConnectCore::set_probe_interval(double p_seconds) {
    probe_interval = p_seconds > 0.0 ? p_seconds : 0.0;
}

} // namespace netw::connect
