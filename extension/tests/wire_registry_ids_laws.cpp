#include "support/netw_test.h"

#include "netw/wire/registry.hpp"

namespace TestWireRegistryIds {

using namespace godot;
using netw::wire::builtin_channel;
using netw::wire::ChannelDecl;
using netw::wire::WireRegistry;

struct Declared {
    const char *name;
    int64_t id;
};

const Declared TRANSCRIBED[] = {
    {"ACTION", 2},
    {"CALL", 3},
    {"REPLY", 4},
    {"SIGNAL", 5},
    {"PROPERTY_SYNC", 6},
    {"INTEREST_AWARENESS", 8},
    {"CLOCK_HANDSHAKE", 9},
    {"CLOCK_HANDSHAKE_REPLY", 10},
    {"CLOCK_PING", 11},
    {"CLOCK_PONG", 12},
    {"LAGCOMP_DENY", 13},
    {"SPAWN", 14},
    {"DESPAWN", 15},
    {"REPARENT", 16},
    {"TABLE", 17},
    {"HIDE", 18},
    {"SYNC", 19},
    {"SYNC_DELTA", 20},
    {"CONTROL_REQUEST", 21},
    {"CONTROL_APPLY", 22},
    {"SESSION_JOIN", 23},
    {"SESSION_ACCEPT", 24},
    {"SESSION_ROSTER", 25},
    {"SESSION_PAUSE", 26},
    {"SESSION_UNPAUSE", 27},
    {"SESSION_KICKED", 28},
    {"SESSION_SCENE_REQUEST", 29},
    {"SESSION_SCENE_RESULT", 30},
    {"SESSION_SHUTDOWN", 31},
    {"SESSION_SCENE_RELEASED", 32},
    {"SESSION_KICK_REQUEST", 33},
    {"SESSION_LEAVE_REQUEST", 34},
    {"PREDICT_COMMAND", 35},
    {"PREDICT_ACK", 36},
    {"PREDICT_RELAY", 37},
    {"PREDICT_RELAY_REQUEST", 38},
    {"SYNC_ROW", 39},
    {"SYNC_ROW_DELTA", 40},
    {"SYNC_ROW_WINDOW", 41},
    {"SESSION_SCENE_SEAT", 42},
};

constexpr int TRANSCRIBED_COUNT
    = int(sizeof(TRANSCRIBED) / sizeof(TRANSCRIBED[0]));

TEST_CASE(
    "[Networked][Wire][Hosted][Registry] WR1 every channel carries the id "
    "WIRE.md transcribes for its name, because a sender and a receiver that "
    "disagree about one byte agree about nothing after it"
) {
    for (int at = 0; at < TRANSCRIBED_COUNT; ++at) {
        NETW_FORMAT_TEXT(named, TRANSCRIBED[at].name);
        CAPTURE(named);
        NETW_CHECK_EQ(
            builtin_channel(StringName(TRANSCRIBED[at].name)),
            TRANSCRIBED[at].id
        );
    }
}

TEST_CASE(
    "[Networked][Wire][Hosted][Registry] WR2 the registry declares exactly "
    "the transcribed table, so a channel added to the code and not to the "
    "prose cannot ship"
) {
    const WireRegistry registry = WireRegistry::create_default();
    NETW_CHECK_EQ(int(registry.active_count()), TRANSCRIBED_COUNT);
}

TEST_CASE(
    "[Networked][Wire][Hosted][Registry] WR3 ids 0, 1 and 7 are reserved and "
    "answer no declaration, because a frame naming one of them is a frame "
    "from a build this one cannot read"
) {
    const WireRegistry registry = WireRegistry::create_default();
    for (const uint8_t id : {uint8_t(0), uint8_t(1), uint8_t(7)}) {
        NETW_FORMAT_INT(id_text, int64_t(id));
        CAPTURE(id_text);
        const ChannelDecl *decl = registry.find_channel(id);
        const bool reserved = decl != nullptr && decl->is_reserved;
        CHECK(reserved);
    }
}

TEST_CASE(
    "[Networked][Wire][Hosted][Registry] WR4 no two declared channels share "
    "an id, which is what makes the table a table"
) {
    const WireRegistry registry = WireRegistry::create_default();
    for (int at = 0; at < TRANSCRIBED_COUNT; ++at) {
        for (int other = at + 1; other < TRANSCRIBED_COUNT; ++other) {
            const bool distinct = TRANSCRIBED[at].id != TRANSCRIBED[other].id;
            CHECK(distinct);
        }
        const ChannelDecl *decl
            = registry.find_channel(uint8_t(TRANSCRIBED[at].id));
        const bool named
            = decl != nullptr && decl->name == StringName(TRANSCRIBED[at].name);
        CHECK(named);
    }
}

} // namespace TestWireRegistryIds
