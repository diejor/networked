#include "godot/callable.hpp"
#include "godot/crypto.hpp"
#include "godot/time.hpp"
#include "godot/utility.hpp"
#include "netw/api/admission_config.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/nodes/lobby_directory.hpp"
#include "netw/auth_protocol.hpp"
#include "netw/connect/directory_transport.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace {

Ref<NetwPromise> result_of(const Variant &p_value, Variant &r_continuation) {
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

void add_transport_account(
    LobbyDirectory *p_directory,
    const Ref<NetwJoinRequest> &p_request
) {
    const int64_t peer = p_request->get_peer_id();
    const String account
        = p_directory != nullptr ? p_directory->account_of(peer) : String();
    if (!account.is_empty()) {
        p_request->add_vouched_account(account, p_directory->member_name(peer));
    }
    p_request->start_from_first_account();
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

void NetwMultiplayer::session_arm_deadline(
    Awaited &r_awaited,
    uint64_t p_declaration
) {
    r_awaited.declaration = p_declaration;
    const double timeout = get_auth_timeout();
    r_awaited.deadline
        = timeout > 0.0 ? now_msec() + int64_t(timeout * 1000.0) : 0;
}

void NetwMultiplayer::session_await(
    int64_t p_peer,
    bool p_proof,
    const Variant &p_returned
) {
    Awaited *waiting = session_awaited(p_peer, p_proof);
    if (waiting == nullptr || waiting->result.is_valid()) {
        return;
    }
    const Ref<NetwPromise> result
        = result_of(p_returned, waiting->continuation);
    waiting->result = result;
    const uint64_t promise = uint64_t(gd::instance_id(result.ptr()));
    result->when_settled(
        p_proof ? callable_mp(this, &NetwMultiplayer::session_proof_settled)
                      .bind(p_peer, promise)
                : callable_mp(this, &NetwMultiplayer::session_admission_settled)
                      .bind(p_peer, promise)
    );
}

String NetwMultiplayer::session_mint_challenge() {
    const PackedByteArray bytes
        = gd::secure_random_bytes(auth::CHALLENGE_BYTES);
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

LobbyDirectory *NetwMultiplayer::session_serving_directory() const {
    const connect::DirectoryTransport *transport
        = dynamic_cast<const connect::DirectoryTransport *>(
            connect_core.live_transport()
        );
    return transport != nullptr ? Object::cast_to<LobbyDirectory>(
                                      gd::object_of(transport->directory_id())
                                  )
                                : nullptr;
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
    String identity
        = transport != nullptr ? transport->server_identity(p_link) : String();
    if (identity.is_empty()) {
        LobbyDirectory *directory = session_serving_directory();
        identity = directory != nullptr ? directory->account_of(SERVER_PEER)
                                        : String();
    }
    r_bound = !identity.is_empty();
    if (!r_bound && !challenge_unbound_logged) {
        challenge_unbound_logged = true;
        NETW_INFO(
            sys::SESSION,
            "Another server of this game could relay the admission "
            "challenge, because this transport does not identify the "
            "server. NetwJoinRequest.is_bound is false for accounts that "
            "answer it."
        );
    }
    return auth::bind_challenge(p_challenge, identity);
}

bool NetwMultiplayer::session_requires_proof() {
    const session_decl::Resolved declared
        = declaration_book().resolve(this, session_decl::KIND_ADMISSION);
    if (declared.state == session_decl::ABSENT) {
        return false;
    }
    if (declared.state != session_decl::READY) {
        return true;
    }
    const Ref<NetwAdmissionConfig> config = declared.payload;
    return config.is_valid() && config->provider_count() > 0;
}

bool NetwMultiplayer::session_has_proof(int64_t p_peer) const {
    const LinkHandshake *link = link_handshakes.getptr(p_peer);
    return link != nullptr && link->proven;
}

void NetwMultiplayer::session_request_proof(
    int64_t p_peer,
    const String &p_challenge
) {
    const session_decl::Resolved declared
        = declaration_book().resolve(this, session_decl::KIND_ADMISSION);
    const Ref<NetwAdmissionConfig> config = declared.payload;
    const bool provable = declared.state == session_decl::READY
        && config.is_valid() && config->provider_count() > 0;
    if (!provable || p_challenge.is_empty()) {
        session_report_join_failure(
            provable ? ERR_UNAVAILABLE : ERR_UNAUTHORIZED,
            provable ? "An admission challenge could not be made."
                     : "The server asks for account proofs. Call "
                       "NetwAdmissionConfig.accept with the same providers "
                       "on both peers."
        );
        if (p_peer != get_unique_id()) {
            disconnect_peer(p_peer);
        }
        return;
    }
    LinkHandshake &opening = link_handshakes[p_peer];
    if (opening.proven || opening.proof.result.is_valid()) {
        return;
    }
    opening.challenge = p_challenge;
    bool bound = false;
    opening.bound_challenge
        = session_bind_challenge(p_challenge, p_peer, bound);
    opening.proofs = Array();
    session_arm_deadline(opening.proof, declared.generation);
    session_prove_next(p_peer);
}

void NetwMultiplayer::session_prove_next(int64_t p_peer) {
    LinkHandshake *link = link_handshakes.getptr(p_peer);
    if (link == nullptr) {
        return;
    }
    const session_decl::Resolved declared
        = declaration_book().resolve(this, session_decl::KIND_ADMISSION);
    const Ref<NetwAdmissionConfig> config = declared.payload;
    if (declared.state != session_decl::READY || config.is_null()
        || declared.generation != link->proof.declaration) {
        session_proof_failed(p_peer, ERR_UNAVAILABLE);
        return;
    }
    const int64_t next = link->proofs.size();
    if (next < config->provider_count()) {
        const String challenge = link->bound_challenge;
        link->proof.result.unref();
        link->proof.continuation = Variant();
        const Variant returned
            = config->provider_at(next)->prove(challenge, get_session());
        session_await(p_peer, true, returned);
        return;
    }
    link->proven = true;
    link->proof.deadline = 0;
    link->proof.continuation = Variant();
    const std::optional<JoinRequest> local_join = link->local_join;
    link->local_join.reset();
    if (local_join.has_value()) {
        session_submit_request(*local_join);
    } else {
        auth_complete_link(p_peer);
    }
}

void NetwMultiplayer::session_proof_failed(int64_t p_peer, Error p_error) {
    session_report_join_failure(
        p_error,
        "The admission proof could not be made."
    );
    if (p_peer != get_unique_id()) {
        disconnect_peer(p_peer);
    } else {
        link_handshakes.erase(p_peer);
    }
}

void NetwMultiplayer::session_proof_settled(
    int64_t p_peer,
    uint64_t p_promise
) {
    Awaited *waiting = session_awaited_settle(p_peer, true, p_promise);
    if (waiting == nullptr) {
        return;
    }
    const Ref<NetwPromise> settled = waiting->result;
    if (settled->get_is_failed()) {
        session_proof_failed(p_peer, settled->get_code());
        return;
    }
    link_handshakes[p_peer].proofs.push_back(settled->get_result());
    session_prove_next(p_peer);
}

void NetwMultiplayer::session_start_admission(const JoinRequest &p_request) {
    const int64_t peer = p_request.peer_id;
    if (pending_admissions.has(peer)) {
        return;
    }
    const session_decl::Resolved declared
        = declaration_book().resolve(this, session_decl::KIND_ADMISSION);
    if (declared.state != session_decl::READY
        && declared.state != session_decl::ABSENT) {
        declaration_book().report_unresolved(
            this,
            session_decl::KIND_ADMISSION,
            declared.state,
            "a join"
        );
        session_fail_join(
            peer,
            ERR_UNAUTHORIZED,
            "The admission handler is unavailable."
        );
        return;
    }
    const LinkHandshake *link = link_handshakes.getptr(peer);
    bool bound = false;
    const String challenge = link != nullptr && !link->challenge.is_empty()
        ? session_bind_challenge(link->challenge, peer, bound)
        : String();
    const Ref<NetwAdmissionConfig> config = declared.payload;
    const int64_t accepted = config.is_valid() ? config->provider_count() : 0;
    Array proofs;
    if (accepted > 0) {
        if (challenge.is_empty() || p_request.proof_bytes.is_empty()) {
            session_fail_join(
                peer,
                ERR_UNAUTHORIZED,
                "The required admission proof is missing."
            );
            return;
        }
        const Variant decoded = gd::bytes_to_var(p_request.proof_bytes);
        if (decoded.get_type() == Variant::ARRAY) {
            proofs = decoded;
        }
        if (proofs.size() != accepted) {
            session_fail_join(
                peer,
                ERR_UNAUTHORIZED,
                vformat(
                    "This server accepts %d accounts and the joining peer "
                    "proved %d. Call NetwAdmissionConfig.accept with the same "
                    "providers in the same order on both peers.",
                    accepted,
                    int64_t(proofs.size())
                )
            );
            return;
        }
    }
    Ref<NetwJoinRequest> request;
    request.instantiate();
    request->bind_to(
        peer,
        p_request.username,
        p_request.arg_values,
        challenge,
        bound
    );
    request->bind_session(this);
    if (declared.state == session_decl::ABSENT) {
        add_transport_account(session_serving_directory(), request);
        session_finish_join(p_request, request);
        return;
    }
    PendingAdmission opened;
    opened.submitted = p_request;
    opened.request = request;
    opened.proofs = proofs;
    pending_admissions.insert(peer, opened);
    session_arm_deadline(pending_admissions[peer].verdict, declared.generation);
    session_verify_next(peer);
}

void NetwMultiplayer::session_verify_next(int64_t p_peer) {
    PendingAdmission *pending = pending_admissions.getptr(p_peer);
    if (pending == nullptr) {
        return;
    }
    const session_decl::Resolved declared
        = declaration_book().resolve(this, session_decl::KIND_ADMISSION);
    const Ref<NetwAdmissionConfig> config = declared.payload;
    if (declared.state != session_decl::READY || config.is_null()
        || declared.generation != pending->verdict.declaration) {
        pending_admissions.erase(p_peer);
        session_fail_join(
            p_peer,
            ERR_UNAVAILABLE,
            "The admission handler changed during the join."
        );
        return;
    }
    const Ref<NetwJoinRequest> request = pending->request;
    const int64_t next = pending->verified;
    if (next < config->provider_count()) {
        const Variant proof
            = next < pending->proofs.size() ? pending->proofs[next] : Variant();
        pending->verified = next + 1;
        pending->verdict.result.unref();
        pending->verdict.continuation = Variant();
        const Variant returned
            = config->provider_at(next)->verify(request, proof);
        session_await(p_peer, false, returned);
        return;
    }
    add_transport_account(session_serving_directory(), request);
    session_ask_admission(p_peer);
}

void NetwMultiplayer::session_ask_admission(int64_t p_peer) {
    PendingAdmission *pending = pending_admissions.getptr(p_peer);
    if (pending == nullptr) {
        return;
    }
    const session_decl::Resolved declared
        = declaration_book().resolve(this, session_decl::KIND_ADMISSION);
    const Ref<NetwJoinRequest> request = pending->request;
    pending->asked = true;
    pending->verdict.result.unref();
    pending->verdict.continuation = Variant();
    bool called = false;
    const Variant returned
        = gd::call_checked(declared.callable, Variant(request), called);
    session_await(
        p_peer,
        false,
        called ? returned
               : Variant(
                     NetwPromise::rejected(
                         ERR_INVALID_DATA,
                         "The admission handler could not be called. It must "
                         "take one NetwJoinRequest."
                     )
                 )
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
    const bool provider = !answered.asked;
    if (!provider) {
        pending_admissions.erase(p_peer);
    }
    const Ref<NetwPromise> result = answered.verdict.result;
    const session_decl::Resolved declared
        = declaration_book().resolve(this, session_decl::KIND_ADMISSION);
    if (declared.state != session_decl::READY
        || declared.generation != answered.verdict.declaration) {
        pending_admissions.erase(p_peer);
        session_fail_join(
            p_peer,
            ERR_UNAVAILABLE,
            "The admission handler changed during the join."
        );
        return;
    }
    if (result->get_is_failed()) {
        pending_admissions.erase(p_peer);
        session_fail_join(p_peer, result->get_code(), result->get_detail());
        return;
    }
    const Variant verdict = result->get_result();
    if (verdict.get_type() != Variant::INT) {
        NETW_ERROR(
            sys::SESSION,
            "%s returned %s. Return OK or an Error.",
            String(
                provider ? "NetwAccountProvider._verify"
                         : "The admission handler"
            ),
            String(Variant::get_type_name(verdict.get_type()))
        );
        pending_admissions.erase(p_peer);
        session_fail_join(
            p_peer,
            ERR_INVALID_DATA,
            provider ? "An account provider did not return an Error."
                     : "The admission handler did not return an Error."
        );
        return;
    }
    const Error refusal = Error(int64_t(verdict));
    if (refusal != OK) {
        pending_admissions.erase(p_peer);
        session_fail_join(
            p_peer,
            refusal,
            vformat(
                provider ? "An account provider refused the join (%s)"
                         : "This server did not admit the join (%s)",
                String(gd::error_name(refusal))
            )
        );
        return;
    }
    if (provider) {
        session_verify_next(p_peer);
        return;
    }
    session_finish_join(answered.submitted, answered.request);
}

void NetwMultiplayer::session_poll_admissions(int64_t p_now_msec) {
    player_expire_kept(p_now_msec);
    LocalVector<int64_t> expired;
    for (const KeyValue<int64_t, PendingAdmission> &entry :
         pending_admissions) {
        const int64_t deadline = entry.value.verdict.deadline;
        if (deadline > 0 && deadline <= p_now_msec) {
            expired.push_back(entry.key);
        }
    }
    for (const int64_t peer : expired) {
        pending_admissions.erase(peer);
        session_fail_join(peer, ERR_TIMEOUT, "The admission timed out.");
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
        session_report_join_failure(
            ERR_TIMEOUT,
            "The admission proof timed out."
        );
        if (peer != get_unique_id()) {
            disconnect_peer(peer);
        }
    }
}

} // namespace netw
