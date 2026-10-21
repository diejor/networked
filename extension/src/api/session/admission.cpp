#include "godot/callable.hpp"
#include "godot/crypto.hpp"
#include "godot/time.hpp"
#include "godot/utility.hpp"
#include "netw/api/admission_config.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/nodes/lobby_directory.hpp"
#include "netw/connect/directory_transport.hpp"
#include "netw/log.hpp"
#include "netw/auth_protocol.hpp"

using namespace godot;

namespace netw {

namespace {

Ref<NetwPromise> result_of(
    const Variant &p_value,
    Variant &r_continuation
) {
    Object *object = gd::live_object(p_value);
    const Ref<NetwPromise> promise(Object::cast_to<NetwPromise>(object));
    if (promise.is_valid()) {
        return promise;
    }
    if (object != nullptr && object->is_class("GDScriptFunctionState")) {
        Ref<NetwPromise> pending;
        pending.instantiate();
        r_continuation = p_value;
        const Error connected = object->connect(
            StringName("completed"),
            callable_mp(pending.ptr(), &NetwPromise::resolve),
            Object::CONNECT_ONE_SHOT
        );
        if (connected != OK) {
            pending->reject(connected, "The admission handler cannot resume.");
        }
        return pending;
    }
    return NetwPromise::resolved(p_value);
}

int64_t now_msec() {
    return int64_t(Time::get_singleton()->get_ticks_msec());
}

} // namespace

NetwMultiplayer::Awaited *NetwMultiplayer::session_awaited(
    int64_t p_peer,
    bool p_proof
) {
    if (p_proof) {
        LinkHandshake *link = link_handshakes.getptr(p_peer);
        return link != nullptr ? &link->proof : nullptr;
    }
    PendingAdmission *pending = pending_admissions.getptr(p_peer);
    return pending != nullptr ? &pending->verdict : nullptr;
}

NetwMultiplayer::Awaited *NetwMultiplayer::session_awaited_settle(
    int64_t p_peer,
    bool p_proof,
    uint64_t p_promise
) {
    Awaited *waiting = session_awaited(p_peer, p_proof);
    if (waiting == nullptr || waiting->result.is_null()
        || uint64_t(gd::instance_id(waiting->result.ptr())) != p_promise) {
        return nullptr;
    }
    return waiting;
}

void NetwMultiplayer::session_await(
    int64_t p_peer,
    bool p_proof,
    const Callable &p_handler,
    const Variant &p_argument,
    uint64_t p_declaration
) {
    Awaited *opening = session_awaited(p_peer, p_proof);
    opening->declaration = p_declaration;
    const double timeout = get_auth_timeout();
    opening->deadline
        = timeout > 0.0 ? now_msec() + int64_t(timeout * 1000.0) : 0;
    bool called = false;
    const Variant returned = gd::call_checked(p_handler, p_argument, called);
    Awaited *waiting = session_awaited(p_peer, p_proof);
    if (waiting == nullptr || waiting->result.is_valid()) {
        return;
    }
    const Ref<NetwPromise> result = called
        ? result_of(returned, waiting->continuation)
        : NetwPromise::rejected(
              ERR_INVALID_DATA,
              p_proof ? "The proof handler could not be called."
                      : "The admission handler could not be called. "
                        "It must take one NetwJoinRequest."
          );
    waiting->result = result;
    const uint64_t promise = uint64_t(gd::instance_id(result.ptr()));
    result->when_settled(
        p_proof
            ? callable_mp(this, &NetwMultiplayer::session_proof_settled)
                  .bind(p_peer, promise)
            : callable_mp(this, &NetwMultiplayer::session_admission_settled)
                  .bind(p_peer, promise)
    );
}

String NetwMultiplayer::session_mint_challenge() {
    const PackedByteArray bytes = gd::secure_random_bytes(auth::CHALLENGE_BYTES);
    NETW_ERR_COND_V(
        bytes.size() != auth::CHALLENGE_BYTES,
        String(),
        sys::SESSION,
        "An admission challenge could not be made. Enable Godot's "
        "cryptography support."
    );
    constexpr char digits[] = "0123456789abcdef";
    char challenge[auth::CHALLENGE_LENGTH + 1];
    for (int at = 0; at < auth::CHALLENGE_BYTES; ++at) {
        challenge[at * 2] = digits[bytes[at] >> 4];
        challenge[at * 2 + 1] = digits[bytes[at] & 15];
    }
    challenge[auth::CHALLENGE_LENGTH] = 0;
    return String::utf8(challenge);
}

String NetwMultiplayer::session_bind_challenge(
    const String &p_challenge,
    int64_t p_link,
    bool &r_bound
) {
    r_bound = true;
    if (p_link == get_unique_id()) {
        return p_challenge;
    }
    const connect::Transport *transport = connect_core.live_transport();
    String identity = transport != nullptr
        ? transport->server_identity(p_link)
        : String();
    if (identity.is_empty() && assigned_peer.is_valid()
        && assigned_peer->is_class("SteamMultiplayerPeer")) {
        const int64_t host = assigned_peer->call(
            "get_steam_id_for_peer_id", int64_t(SERVER_PEER)
        );
        identity = host != 0 ? String::num_int64(host) : String();
    }
    r_bound = !identity.is_empty();
    if (!r_bound && !challenge_unbound_logged) {
        challenge_unbound_logged = true;
        NETW_INFO(
            sys::SESSION,
            "Another server of this game could relay the admission "
            "challenge, because this transport does not identify the "
            "server. NetwJoinRequest.challenge_bound is false."
        );
    }
    return auth::bind_challenge(p_challenge, identity);
}

bool NetwMultiplayer::session_requires_proof() {
    const session_decl::Resolved declared = declaration_book().resolve(
        this, session_decl::KIND_ADMISSION
    );
    if (declared.state == session_decl::ABSENT) {
        return false;
    }
    if (declared.state != session_decl::READY) {
        return true;
    }
    const Ref<NetwAdmissionConfig> config = declared.payload;
    return config.is_valid() && !config->get_proof_handler().is_null();
}

bool NetwMultiplayer::session_has_proof(int64_t p_peer) const {
    const LinkHandshake *link = link_handshakes.getptr(p_peer);
    return link != nullptr && link->proof.result.is_valid()
        && link->proof.result->get_is_completed();
}

void NetwMultiplayer::session_request_proof(
    int64_t p_peer,
    const String &p_challenge
) {
    const session_decl::Resolved declared = declaration_book().resolve(
        this, session_decl::KIND_ADMISSION
    );
    const Ref<NetwAdmissionConfig> config = declared.payload;
    const bool provable = declared.state == session_decl::READY
        && config.is_valid() && config->get_proof_handler().is_valid();
    if (!provable || p_challenge.is_empty()) {
        session_report_join_failure(
            provable ? ERR_UNAVAILABLE : ERR_UNAUTHORIZED,
            provable ? "An admission challenge could not be made."
                     : "The server requires a proof. Set "
                       "NetwAdmissionConfig.proof on both peers."
        );
        if (p_peer != get_unique_id()) {
            disconnect_peer(p_peer);
        }
        return;
    }
    LinkHandshake &opening = link_handshakes[p_peer];
    if (opening.proof.result.is_valid()) {
        return;
    }
    opening.challenge = p_challenge;
    bool bound = false;
    session_await(
        p_peer, true, config->get_proof_handler(),
        Variant(session_bind_challenge(p_challenge, p_peer, bound)),
        declared.generation
    );
}

void NetwMultiplayer::session_proof_settled(
    int64_t p_peer,
    uint64_t p_promise
) {
    Awaited *waiting = session_awaited_settle(p_peer, true, p_promise);
    if (waiting == nullptr) {
        return;
    }
    const session_decl::Resolved declared = declaration_book().resolve(
        this, session_decl::KIND_ADMISSION
    );
    if (waiting->result->get_is_failed()
        || declared.generation != waiting->declaration
        || declared.state != session_decl::READY) {
        session_report_join_failure(
            waiting->result->get_is_failed() ? waiting->result->get_code()
                                             : ERR_UNAVAILABLE,
            "The admission proof could not be made."
        );
        if (p_peer != get_unique_id()) {
            disconnect_peer(p_peer);
        } else {
            link_handshakes.erase(p_peer);
        }
        return;
    }
    waiting->deadline = 0;
    waiting->continuation = Variant();
    LinkHandshake &ready = link_handshakes[p_peer];
    const std::optional<JoinRequest> local_join = ready.local_join;
    ready.local_join.reset();
    if (local_join.has_value()) {
        session_submit_request(*local_join);
    } else {
        auth_complete_link(p_peer);
    }
}

void NetwMultiplayer::session_start_admission(const JoinRequest &p_request) {
    const int64_t peer = p_request.peer_id;
    if (pending_admissions.has(peer)) {
        return;
    }
    const session_decl::Resolved declared = declaration_book().resolve(
        this, session_decl::KIND_ADMISSION
    );
    if (declared.state != session_decl::READY
        && declared.state != session_decl::ABSENT) {
        declaration_book().report_unresolved(
            this, session_decl::KIND_ADMISSION, declared.state, "a join"
        );
        session_fail_join(peer, ERR_UNAUTHORIZED,
            "The admission handler is unavailable.");
        return;
    }
    const LinkHandshake *link = link_handshakes.getptr(peer);
    bool bound = false;
    const String challenge = link != nullptr && !link->challenge.is_empty()
        ? session_bind_challenge(link->challenge, peer, bound)
        : String();
    if (session_requires_proof()
        && (challenge.is_empty() || p_request.proof_bytes.is_empty())) {
        session_fail_join(peer, ERR_UNAUTHORIZED,
            "The required admission proof is missing.");
        return;
    }
    PackedStringArray identities;
    String account_name;
    const connect::DirectoryTransport *transport
        = dynamic_cast<const connect::DirectoryTransport *>(
            connect_core.live_transport()
        );
    LobbyDirectory *directory = transport != nullptr
        ? Object::cast_to<LobbyDirectory>(gd::object_of(
              transport->directory_id()
          ))
        : nullptr;
    if (directory != nullptr) {
        const String account = directory->account_of(peer);
        if (!account.is_empty()) {
            identities.push_back(account);
            account_name = directory->member_name(peer);
        }
    }
    Ref<NetwJoinRequest> request;
    request.instantiate();
    request->bind_to(
        peer, p_request.username, p_request.arg_values, challenge, bound,
        p_request.proof_bytes.is_empty()
            ? Variant() : gd::bytes_to_var(p_request.proof_bytes),
        identities, account_name
    );
    if (declared.state == session_decl::ABSENT) {
        session_finish_join(p_request, request);
        return;
    }
    PendingAdmission opened;
    opened.submitted = p_request;
    opened.request = request;
    pending_admissions.insert(peer, opened);
    session_await(
        peer, false, declared.callable, Variant(request), declared.generation
    );
}

void NetwMultiplayer::session_admission_settled(
    int64_t p_peer,
    uint64_t p_promise
) {
    if (session_awaited_settle(p_peer, false, p_promise) == nullptr) {
        return;
    }
    const PendingAdmission answered = pending_admissions[p_peer];
    pending_admissions.erase(p_peer);
    const Ref<NetwPromise> result = answered.verdict.result;
    const session_decl::Resolved declared = declaration_book().resolve(
        this, session_decl::KIND_ADMISSION
    );
    if (declared.state != session_decl::READY
        || declared.generation != answered.verdict.declaration) {
        session_fail_join(p_peer, ERR_UNAVAILABLE,
            "The admission handler changed during the join.");
        return;
    }
    if (result->get_is_failed()) {
        session_fail_join(p_peer, result->get_code(), result->get_detail());
        return;
    }
    const Variant verdict = result->get_result();
    if (verdict.get_type() != Variant::INT) {
        NETW_ERROR(
            sys::SESSION,
            "The admission handler returned %s. Return OK or an Error.",
            String(Variant::get_type_name(verdict.get_type()))
        );
        session_fail_join(p_peer, ERR_INVALID_DATA,
            "The admission handler did not return an Error.");
        return;
    }
    const Error refusal = Error(int64_t(verdict));
    if (refusal != OK) {
        session_fail_join(p_peer, refusal, vformat(
            "This server did not admit the join (%s)",
            String(gd::error_name(refusal))
        ));
        return;
    }
    session_finish_join(answered.submitted, answered.request);
}

void NetwMultiplayer::session_poll_admissions(int64_t p_now_msec) {
    LocalVector<int64_t> expired;
    for (const KeyValue<int64_t, PendingAdmission> &entry : pending_admissions) {
        const int64_t deadline = entry.value.verdict.deadline;
        if (deadline > 0 && deadline <= p_now_msec) {
            expired.push_back(entry.key);
        }
    }
    for (const int64_t peer : expired) {
        pending_admissions.erase(peer);
        session_fail_join(peer, ERR_TIMEOUT, "The admission handler timed out.");
    }
    expired.clear();
    for (const KeyValue<int64_t, LinkHandshake> &entry : link_handshakes) {
        const int64_t deadline = entry.value.proof.deadline;
        if (deadline > 0 && deadline <= p_now_msec) {
            expired.push_back(entry.key);
        }
    }
    for (const int64_t peer : expired) {
        link_handshakes.erase(peer);
        session_report_join_failure(ERR_TIMEOUT,
            "The admission proof timed out.");
        if (peer != get_unique_id()) {
            disconnect_peer(peer);
        }
    }
}

} // namespace netw
