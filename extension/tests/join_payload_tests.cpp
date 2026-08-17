// Laws for JoinPayload, the request a peer sends to enter a session.
//
// This is the untrusted side of the join. A server reads one of these from
// bytes a client wrote, before anything has decided whether that client may
// join at all, so the laws here are about what the read refuses and about what
// it does NOT carry: arg_values never rides the wire, only the encoded
// arg_bytes does, and the golden below is what pins that.

#include "support/netw_test.h"

#include "godot/utility.hpp"
#include "netw/join_payload.hpp"

namespace TestJoinPayload {

using namespace godot;
using netw::JoinPayload;
using netw::ResolvedJoin;

// username "valeria", arg_bytes [9,8,7], schema_hash 1234567, peer_id 42,
// is_debug true, as the GDScript form wrote it. StringName keys, and no
// arg_values entry at all.
constexpr const char *GOLDEN =
    "1b000000050000001500000008000000757365726e616d6515000000070000007661"
    "6c657269610015000000090000006172675f62797465730000001d00000003000000"
    "09080700150000000b000000736368656d615f68617368000200000087d612001500"
    "000007000000706565725f696400020000002a000000150000000800000069735f64"
    "65627567 0100000001000000";

Ref<JoinPayload> a_payload() {
    Ref<JoinPayload> payload;
    payload.instantiate();
    payload->set_username("valeria");
    PackedByteArray bytes;
    bytes.push_back(9);
    bytes.push_back(8);
    bytes.push_back(7);
    payload->set_arg_bytes(bytes);
    payload->set_schema_hash(1234567);
    payload->set_peer_id(42);
    payload->set_is_debug(true);
    return payload;
}

TEST_CASE(
    "[Networked][Session][Hosted] K1 the request is the bytes the GDScript "
    "form wrote, and the live args are not among them"
) {
    Ref<JoinPayload> payload = a_payload();
    Array live;
    live.push_back("never sent");
    payload->set_arg_values(live);

    const String written = netw::gd::hex_of(payload->serialize());

    CHECK(written == String(GOLDEN).replace(" ", ""));
}

TEST_CASE(
    "[Networked][Session][Hosted] K2 a request survives its own frame, and the "
    "live args do not come back because they never left"
) {
    Ref<JoinPayload> back;
    back.instantiate();

    NETW_CHECK_EQ(back->deserialize(a_payload()->serialize()), true);
    CHECK(back->get_username() == StringName("valeria"));
    NETW_CHECK_EQ(back->get_peer_id(), 42);
    NETW_CHECK_EQ(back->get_schema_hash(), 1234567);
    NETW_CHECK_EQ(back->get_arg_bytes().size(), 3);
    NETW_CHECK_EQ(back->get_is_debug(), true);
    NETW_CHECK_EQ(back->get_arg_values().size(), 0);
}

TEST_CASE(
    "[Networked][Session][Hosted] K3 an unreadable request is refused and "
    "leaves the payload exactly as it found it"
) {
    Ref<JoinPayload> payload = a_payload();

    PackedByteArray garbage;
    garbage.push_back(1);
    garbage.push_back(2);
    garbage.push_back(3);
    garbage.push_back(4);
    NETW_CHECK_EQ(payload->deserialize(garbage), false);

    Dictionary no_name;
    no_name[StringName("peer_id")] = 3;
    NETW_CHECK_EQ(payload->deserialize(netw::gd::var_to_bytes(no_name)), false);

    Dictionary worded_peer;
    worded_peer[StringName("username")] = StringName("mallory");
    worded_peer[StringName("peer_id")] = "three";
    NETW_CHECK_EQ(
        payload->deserialize(netw::gd::var_to_bytes(worded_peer)),
        false
    );

    // A refused read writes nothing, so a server reusing one payload cannot be
    // left holding half of a request it rejected.
    CHECK(payload->get_username() == StringName("valeria"));
    NETW_CHECK_EQ(payload->get_peer_id(), 42);
    NETW_CHECK_EQ(payload->get_schema_hash(), 1234567);
}

TEST_CASE(
    "[Networked][Session][Hosted] K4 a request with no name resolves to no "
    "join, because a nameless player is not one this session can seat"
) {
    Ref<JoinPayload> nameless;
    nameless.instantiate();
    nameless->set_peer_id(9);
    CHECK(nameless->resolve().is_null());

    const Ref<ResolvedJoin> resolved = a_payload()->resolve();
    REQUIRE(resolved.is_valid());
    CHECK(resolved->get_username() == StringName("valeria"));
    NETW_CHECK_EQ(resolved->get_peer_id(), 42);
    NETW_CHECK_EQ(resolved->get_is_debug(), true);
}

TEST_CASE(
    "[Networked][Session][Hosted] K5 the resolved join takes a copy of the "
    "live args, so a caller mutating them afterwards moves nothing"
) {
    Ref<JoinPayload> payload = a_payload();
    Array live;
    Array nested;
    nested.push_back(1);
    live.push_back(nested);
    payload->set_arg_values(live);

    const Ref<ResolvedJoin> resolved = payload->resolve();
    REQUIRE(resolved.is_valid());
    nested.push_back(2);

    REQUIRE(resolved->get_arg_values().size() == 1);
    NETW_CHECK_EQ(Array(resolved->get_arg_values()[0]).size(), 1);
}

struct ScalarRow {
    const char *username;
    int64_t peer_id;
    bool is_debug;
};

TEST_CASE(
    "[Networked][Session][Hosted] K6 every scalar a request carries survives "
    "its own frame, including the ones a default would hide"
) {
    const ScalarRow rows[] = {
        {"valeria", 7, false},
        {"jose", 0, true},
        {"carol", 42, false},
        {"", -1, true},
    };

    for (const ScalarRow &row : rows) {
        NETW_FORMAT_TEXT(username_text, row.username);
        CAPTURE(username_text);

        Ref<JoinPayload> sent;
        sent.instantiate();
        sent->set_username(StringName(String(row.username)));
        sent->set_peer_id(row.peer_id);
        sent->set_is_debug(row.is_debug);
        PackedByteArray bytes;
        bytes.push_back(3);
        bytes.push_back(1);
        bytes.push_back(4);
        sent->set_arg_bytes(bytes);
        sent->set_schema_hash(987654321);

        Ref<JoinPayload> back;
        back.instantiate();
        NETW_CHECK_EQ(back->deserialize(sent->serialize()), true);

        CHECK(back->get_username() == StringName(String(row.username)));
        NETW_CHECK_EQ(back->get_peer_id(), row.peer_id);
        NETW_CHECK_EQ(back->get_is_debug(), row.is_debug);
        CHECK(back->get_arg_bytes() == bytes);
        NETW_CHECK_EQ(back->get_schema_hash(), 987654321);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] K7 a request that names only the required "
    "fields reads the absent ones as their defaults rather than as nothing"
) {
    Dictionary minimal;
    minimal[StringName("username")] = StringName("valeria");
    minimal[StringName("peer_id")] = 7;

    Ref<JoinPayload> back = a_payload();
    NETW_CHECK_EQ(back->deserialize(netw::gd::var_to_bytes(minimal)), true);

    NETW_CHECK_EQ(back->get_is_debug(), false);
    NETW_CHECK_EQ(back->get_arg_bytes().size(), 0);
    NETW_CHECK_EQ(back->get_schema_hash(), 0);
}

TEST_CASE(
    "[Networked][Session][Hosted] K8 a request whose optional fields carry the "
    "wrong type reads them as their defaults rather than refusing the join"
) {
    Dictionary worded;
    worded[StringName("username")] = StringName("valeria");
    worded[StringName("peer_id")] = 7;
    worded[StringName("is_debug")] = "yes";
    worded[StringName("schema_hash")] = "big";
    worded[StringName("arg_bytes")] = "three";

    Ref<JoinPayload> back = a_payload();
    NETW_CHECK_EQ(back->deserialize(netw::gd::var_to_bytes(worded)), true);

    NETW_CHECK_EQ(back->get_is_debug(), false);
    NETW_CHECK_EQ(back->get_arg_bytes().size(), 0);
    NETW_CHECK_EQ(back->get_schema_hash(), 0);
}

} // namespace TestJoinPayload
