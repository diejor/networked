#include "support/netw_test.h"

#include "netw/auth_protocol.hpp"

namespace TestNetwAuthProtocol {

using namespace godot;
namespace auth = netw::auth;

PackedByteArray bytes_of(const int *p_values, int p_count) {
    PackedByteArray out;
    for (int at = 0; at < p_count; at++) {
        out.push_back(uint8_t(p_values[at]));
    }
    return out;
}

TEST_CASE(
    "[Networked][Session][Hosted] A1 a packet is named by its magic prefix, "
    "and one that carries neither, or is shorter than a header, is unknown"
) {
    NETW_CHECK_EQ(
        int(auth::classify(auth::encode_client_hello(PackedByteArray(), 0, 0))),
        int(auth::Kind::HELLO)
    );
    NETW_CHECK_EQ(
        int(auth::classify(auth::encode_probe_request(0))),
        int(auth::Kind::PROBE)
    );
    const int foreign[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
    NETW_CHECK_EQ(
        int(auth::classify(bytes_of(foreign, 6))),
        int(auth::Kind::UNKNOWN)
    );
    const int truncated[] = {0x4E, 0x48};
    NETW_CHECK_EQ(
        int(auth::classify(bytes_of(truncated, 2))),
        int(auth::Kind::UNKNOWN)
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] A2 a hello round-trips its build tag, its "
    "flags and the provider's own bytes, and an empty payload stays empty"
) {
    const int provider[] = {0xDE, 0xAD, 0xBE, 0xEF};
    const PackedByteArray payload = bytes_of(provider, 4);

    auth::Hello decoded = auth::decode_client_hello(
        auth::encode_client_hello(payload, 0, 0x42),
        0
    );
    CHECK(decoded.ok());
    NETW_CHECK_EQ(decoded.version, int(auth::PROTOCOL_VERSION));
    NETW_CHECK_EQ(int64_t(decoded.app_tag), int64_t(0));
    NETW_CHECK_EQ(decoded.flags, 0x42);
    CHECK(decoded.provider_payload == payload);

    decoded = auth::decode_client_hello(
        auth::encode_client_hello(payload, 0xABCDEF12, 0),
        0xABCDEF12
    );
    CHECK(decoded.ok());
    NETW_CHECK_EQ(int64_t(decoded.app_tag), int64_t(0xABCDEF12));
    CHECK(decoded.provider_payload == payload);

    decoded = auth::decode_client_hello(
        auth::encode_client_hello(PackedByteArray(), 0, 0),
        0
    );
    CHECK(decoded.ok());
    NETW_CHECK_EQ(int64_t(decoded.provider_payload.size()), int64_t(0));
}

TEST_CASE(
    "[Networked][Session][Hosted] A3 a probe request round-trips its flags "
    "and a probe reply round-trips its status and its payload"
) {
    const auth::ProbeRequest asked
        = auth::decode_probe_request(auth::encode_probe_request(0x07));
    CHECK(asked.ok);
    NETW_CHECK_EQ(asked.version, int(auth::PROTOCOL_VERSION));
    NETW_CHECK_EQ(asked.flags, 0x07);

    const int body[] = {0xCA, 0xFE};
    const PackedByteArray payload = bytes_of(body, 2);
    const auth::ProbeReply answered = auth::decode_probe_reply(
        auth::encode_probe_reply(int(auth::ProbeStatus::OK), payload)
    );
    CHECK(answered.ok);
    NETW_CHECK_EQ(answered.status, int(auth::ProbeStatus::OK));
    CHECK(answered.payload == payload);
}

TEST_CASE(
    "[Networked][Session][Hosted] A4 a foreign build, a foreign framing and "
    "the wrong packet are each refused by name rather than misread"
) {
    auth::Hello decoded = auth::decode_client_hello(
        auth::encode_client_hello(PackedByteArray(), 0x11111111, 0),
        0x22222222
    );
    CHECK_FALSE(decoded.ok());
    NETW_CHECK_EQ(int(decoded.refusal), int(auth::Refusal::APP));
    NETW_CHECK_EQ(int64_t(decoded.app_tag), int64_t(0x11111111));

    decoded = auth::decode_client_hello(auth::encode_probe_request(0), 0);
    CHECK_FALSE(decoded.ok());
    NETW_CHECK_EQ(int(decoded.refusal), int(auth::Refusal::FRAMING));

    CHECK_FALSE(
        auth::decode_probe_request(
            auth::encode_client_hello(PackedByteArray(), 0, 0)
        )
            .ok
    );

    PackedByteArray stale = auth::magic_hello();
    stale.push_back(0xFF);
    while (stale.size() < auth::HELLO_HEADER_LEN) {
        stale.push_back(0x00);
    }
    decoded = auth::decode_client_hello(stale, 0);
    CHECK_FALSE(decoded.ok());
    NETW_CHECK_EQ(int(decoded.refusal), int(auth::Refusal::VERSION));
}

} // namespace TestNetwAuthProtocol
