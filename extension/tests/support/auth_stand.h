#pragma once

#include "godot/class_db.hpp"
#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/api/auth_flow.hpp"
#include "netw/api/auth_result.hpp"
#include "netw/api/netw_identity.hpp"
#include "netw/api/promise.hpp"

namespace netw_test {

class NetwTestAuthFlow : public netw::NetwAuthFlow {
    GDCLASS(NetwTestAuthFlow, netw::NetwAuthFlow)

    godot::PackedByteArray answer;
    godot::Ref<netw::AuthResult> verdict;
    godot::PackedByteArray verified_payload;
    godot::Array recorded;
    godot::Array minted_for;
    godot::Ref<netw::NetwPromise> prepared;
    godot::Ref<netw::NetwIdentity> host;
    int verify_calls = 0;
    int prepare_calls = 0;
    int credential_calls = 0;

protected:
    static void _bind_methods() {
        godot::ClassDB::bind_method(
            D_METHOD("record", "peer", "data"),
            &NetwTestAuthFlow::record
        );
        godot::ClassDB::bind_method(D_METHOD("mint"), &NetwTestAuthFlow::mint);
    }

public:
    void set_answer(const godot::PackedByteArray &p_answer) {
        answer = p_answer;
    }

    void set_verdict(const godot::Ref<netw::AuthResult> &p_verdict) {
        verdict = p_verdict;
    }

    godot::PackedByteArray credentials(const godot::StringName &) override {
        credential_calls += 1;
        return answer;
    }

    int credential_count() const {
        return credential_calls;
    }

    godot::Ref<netw::AuthResult> verify(
        int64_t,
        const godot::PackedByteArray &p_data
    ) override {
        verify_calls += 1;
        verified_payload = p_data;
        return verdict;
    }

    void set_prepared(const godot::Ref<netw::NetwPromise> &p_prepared) {
        prepared = p_prepared;
    }

    void set_host(const godot::Ref<netw::NetwIdentity> &p_host) {
        host = p_host;
    }

    godot::Ref<netw::NetwPromise> prepare(const godot::StringName &) override {
        prepare_calls += 1;
        return prepared;
    }

    godot::Ref<netw::NetwIdentity> host_identity() override {
        return host;
    }

    int prepare_count() const {
        return prepare_calls;
    }

    godot::Ref<NetwTestAuthFlow> mint() {
        minted_for.push_back(godot::Variant(minted_for.size()));
        return godot::Ref<NetwTestAuthFlow>(this);
    }

    int mint_count() const {
        return minted_for.size();
    }

    void record(int64_t p_peer, const godot::PackedByteArray &p_data) {
        (void)p_peer;
        recorded.push_back(p_data);
    }

    int record_count() const {
        return recorded.size();
    }

    int verify_count() const {
        return verify_calls;
    }

    godot::PackedByteArray last_verified_payload() const {
        return verified_payload;
    }
};

} // namespace netw_test
