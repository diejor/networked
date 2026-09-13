#include "support/netw_test.h"

#include "netw/api/schema_core.hpp"
#include "netw/predict/engine.hpp"
#include "netw/predict/frames.hpp"
#include "netw/predict/journal.hpp"
#include "netw/predict/relay_book.hpp"

using namespace godot;

namespace TestNetwPredictFrames {

using godot::PackedByteArray;
using godot::Ref;
using netw::SchemaCore;
using netw::predict::ACK_FRAME_BYTES_MAX;
using netw::predict::ACK_RECORD_MAX;
using netw::predict::AckEvidenceWire;
using netw::predict::AckFrame;
using netw::predict::CommandEvidenceWire;
using netw::predict::CommandFrame;
using netw::predict::TransitionWire;
using netw::table::SchemaRecord;
using netw::wire::CodeRow;
using netw::wire::WirePlan;
using netw::wire::WireRegistry;

constexpr int64_t OWNER_PEER = 7;

WirePlan input_plan() {
    SchemaRecord record;
    record.name = "PredictInput";
    SchemaCore::append_column(&record, "button", SchemaCore::U8, 1);
    SchemaCore::fix(&record);
    return WirePlan::compile(record);
}

CodeRow input_row(const WirePlan &plan, uint64_t value) {
    CodeRow row = CodeRow::for_plan(plan);
    row.write(plan.column(0), 0, value);
    return row;
}

CommandEvidenceWire command_evidence(int32_t seed, bool raw) {
    CommandEvidenceWire row;
    row.evidence_mask = uint8_t(
        netw::predict::EVIDENCE_WITNESS
        | (raw ? netw::predict::EVIDENCE_RAW : 0)
    );
    row.pre_fp = seed + 1;
    row.post_fp = seed + 2;
    row.e_digest = seed + 3;
    row.topo_fp = seed + 4;
    row.witness_fp = seed + 5;
    row.pre_pose_fp = seed + 6;
    row.pre_momentum_fp = seed + 7;
    row.pre_controller_fp = seed + 8;
    row.post_pose_fp = seed + 9;
    row.post_momentum_fp = seed + 10;
    row.post_controller_fp = seed + 11;
    row.raw_fp = seed + 12;
    return row;
}

AckEvidenceWire ack_evidence(int32_t seed, bool raw) {
    AckEvidenceWire row;
    row.evidence_mask = uint8_t(
        netw::predict::EVIDENCE_WITNESS
        | (raw ? netw::predict::EVIDENCE_RAW : 0)
    );
    row.pre_fp = seed + 1;
    row.c_hash = seed + 2;
    row.e_digest = seed + 3;
    row.post_fp = seed + 4;
    row.topo_fp = seed + 5;
    row.witness_fp = seed + 6;
    row.pre_pose_fp = seed + 7;
    row.pre_momentum_fp = seed + 8;
    row.pre_controller_fp = seed + 9;
    row.post_pose_fp = seed + 10;
    row.post_momentum_fp = seed + 11;
    row.post_controller_fp = seed + 12;
    row.raw_fp = seed + 13;
    row.flags = uint8_t(
        netw::predict::ACK_SUBSTITUTED | (5 << netw::predict::ACK_WITNESS_SHIFT)
    );
    return row;
}

TEST_CASE(
    "[Networked][PredictWire][Hosted] command binds each fresh transition "
    "to a row"
) {
    const WirePlan plan = input_plan();
    REQUIRE(plan.valid());
    CommandFrame sent;
    sent.epoch = 7;
    sent.ack_of_acks = -1;
    sent.transitions.push_back(TransitionWire{4, 100, true});
    sent.transitions.push_back(TransitionWire{5, 100, false});
    sent.transitions.push_back(TransitionWire{6, 102, true});
    sent.payloads.push_back(input_row(plan, 0x2a));
    sent.payloads.push_back(input_row(plan, 0x7f));
    sent.evidence.push_back(command_evidence(-20, false));
    sent.evidence.push_back(command_evidence(100, true));

    const PackedByteArray bytes = netw::predict::encode_command(sent, plan);
    REQUIRE_FALSE(bytes.is_empty());
    NETW_CHECK_EQ(bytes[0], 1);
    NETW_CHECK_EQ(bytes[1], 7);
    NETW_CHECK_EQ(bytes[2], 4);
    NETW_CHECK_EQ(bytes[3], 3);
    NETW_CHECK_EQ(bytes[4], 0x91);
    NETW_CHECK_EQ(bytes[5], 0x03);
    NETW_CHECK_EQ(bytes[6], 0);
    NETW_CHECK_EQ(bytes[7], 9);
    NETW_CHECK_EQ(bytes[8], 0x2a);
    NETW_CHECK_EQ(bytes[9], 0x7f);

    CommandFrame received;
    REQUIRE(netw::predict::decode_command(bytes, plan, received));
    NETW_CHECK_EQ(received.epoch, 7);
    NETW_CHECK_EQ(received.ack_of_acks, -1);
    NETW_CHECK_EQ(received.transitions.size(), 3);
    NETW_CHECK_EQ(received.payloads.size(), 2);
    NETW_CHECK_EQ(received.evidence.size(), 2);
    NETW_CHECK_EQ(received.transitions[2].index, 6);
    NETW_CHECK_EQ(received.transitions[2].label, 102);
    CHECK(received.transitions[2].fresh);
    NETW_CHECK_EQ(received.payloads[0].read(plan.column(0), 0), 0x2a);
    NETW_CHECK_EQ(received.payloads[1].read(plan.column(0), 0), 0x7f);
    NETW_CHECK_EQ(received.evidence[0].pre_fp, -19);
    NETW_CHECK_EQ(received.evidence[1].raw_fp, 112);
}

TEST_CASE(
    "[Networked][PredictWire][Hosted] command rejects a fresh row "
    "truncation"
) {
    const WirePlan plan = input_plan();
    CommandFrame sent;
    sent.transitions.push_back(TransitionWire{0, 1, true});
    sent.payloads.push_back(input_row(plan, 0x2a));
    PackedByteArray bytes = netw::predict::encode_command(sent, plan);
    REQUIRE_FALSE(bytes.is_empty());
    bytes.remove_at(5);

    CommandFrame unchanged;
    unchanged.epoch = 44;
    CHECK_FALSE(netw::predict::decode_command(bytes, plan, unchanged));
    NETW_CHECK_EQ(unchanged.epoch, 44);
}

TEST_CASE(
    "[Networked][PredictWire][Hosted] command carries a wide planned row"
) {
    SchemaRecord schema;
    schema.name = "WidePredictInput";
    SchemaCore::append_column(&schema, "axes", SchemaCore::VECTOR4, 1);
    REQUIRE(SchemaCore::fix(&schema) == godot::Error::OK);
    const WirePlan plan = WirePlan::compile(schema);
    REQUIRE(plan.valid());

    CodeRow row = CodeRow::for_plan(plan);
    REQUIRE(row.write_bits(0, 64, 0xfedcba9876543210ULL));
    REQUIRE(row.write_bits(64, 64, 0x0123456789abcdefULL));
    CommandFrame sent;
    sent.transitions.push_back(TransitionWire{0, 1, true});
    sent.payloads.push_back(row);
    const PackedByteArray bytes = netw::predict::encode_command(sent, plan);
    REQUIRE_FALSE(bytes.is_empty());

    CommandFrame received;
    REQUIRE(netw::predict::decode_command(bytes, plan, received));
    NETW_CHECK_EQ(received.payloads.size(), 1);
    NETW_CHECK_EQ(received.payloads[0].read_bits(0, 64), 0xfedcba9876543210ULL);
    NETW_CHECK_EQ(
        received.payloads[0].read_bits(64, 64),
        0x0123456789abcdefULL
    );
}

TEST_CASE(
    "[Networked][PredictWire][Hosted] ack keeps signed evidence and "
    "witness bits"
) {
    AckFrame sent;
    sent.epoch = 9;
    sent.base = 300;
    sent.records.push_back(ack_evidence(-20, false));
    sent.records.push_back(ack_evidence(100, true));

    const PackedByteArray bytes = netw::predict::encode_ack(sent);
    REQUIRE_FALSE(bytes.is_empty());
    NETW_CHECK_EQ(bytes[0], 9);
    NETW_CHECK_EQ(bytes[1], 0xac);
    NETW_CHECK_EQ(bytes[2], 0x02);
    NETW_CHECK_EQ(bytes[3], 2);

    AckFrame received;
    REQUIRE(netw::predict::decode_ack(bytes, received));
    NETW_CHECK_EQ(received.epoch, 9);
    NETW_CHECK_EQ(received.base, 300);
    NETW_CHECK_EQ(received.records.size(), 2);
    NETW_CHECK_EQ(received.records[0].pre_fp, -19);
    NETW_CHECK_EQ(received.records[1].raw_fp, 113);
    NETW_CHECK_EQ(
        received.records[0].flags & netw::predict::ACK_WITNESS_MASK,
        5 << netw::predict::ACK_WITNESS_SHIFT
    );
}

TEST_CASE(
    "[Networked][PredictWire][Hosted] journal exposes every retained "
    "evidence column"
) {
    netw::predict::Journal journal;
    netw::predict::JournalOpen opened;
    opened.label = 41;
    opened.kind = 2;
    opened.c_hash = 43;
    opened.pre_fp = 44;
    opened.pre_families = {45, 46, 47};
    opened.provenance.episode = 48;
    opened.provenance.write_id = 49;
    opened.provenance.op = netw::predict::Operator::REBASE_EXACT;
    opened.provenance.basis = 50;
    opened.raw_fp = 51;
    opened.evidence_mask = netw::predict::EVIDENCE_RAW;
    journal.open(40, opened);
    journal.mark_solve(
        40,
        52,
        53,
        uint8_t(netw::predict::EVIDENCE_RAW | netw::predict::EVIDENCE_WITNESS),
        5
    );
    journal.close(40, 54, {55, 56, 57});

    NETW_CHECK_EQ(journal.raw_fp_at(0), 51);
    NETW_CHECK_EQ(journal.witness_class_bits_at(0), 5);
    NETW_CHECK_EQ(journal.pre_families_at(0).pose, 45);
    NETW_CHECK_EQ(journal.pre_families_at(0).momentum, 46);
    NETW_CHECK_EQ(journal.pre_families_at(0).controller, 47);
    NETW_CHECK_EQ(journal.post_families_at(0).pose, 55);
    NETW_CHECK_EQ(journal.post_families_at(0).momentum, 56);
    NETW_CHECK_EQ(journal.post_families_at(0).controller, 57);
    NETW_CHECK_EQ(journal.episode_id_at(0), 48);
    NETW_CHECK_EQ(journal.write_id_at(0), 49);
    NETW_CHECK_EQ(
        journal.operator_at(0),
        int(netw::predict::Operator::REBASE_EXACT)
    );
    NETW_CHECK_EQ(journal.basis_at(0), 50);

    journal.clear(7);
    NETW_CHECK_EQ(journal.epoch(), 7);
    NETW_CHECK_EQ(journal.size(), 0);
}

TEST_CASE(
    "[Networked][PredictWire][Hosted] pool builds the authority ACK prefix "
    "from its journal"
) {
    netw::NetwPredictionEngine held_pool;
    netw::NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open();
    REQUIRE(pool->configure(
        slot,
        int(netw::Schedule::TICK),
        int(netw::Role::CONSUME),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT)
    ));

    for (int64_t transition = 0; transition < 3; ++transition) {
        pool->record_input(slot, transition, 100 + transition);
        const netw::predict::DriveRecord drive = pool->replay_drive(
            slot,
            godot::Dictionary(),
            transition,
            20 + transition,
            int(netw::DriveKind::FRESH),
            transition,
            transition,
            1.0 / 60.0,
            1,
            200 + transition,
            210 + transition,
            220 + transition,
            230 + transition
        );
        REQUIRE(drive.ran);
        pool->close_drive(
            slot,
            transition,
            300 + transition,
            310 + transition,
            320 + transition,
            330 + transition
        );
    }

    const PackedByteArray bytes = pool->build_ack_frame(slot, 9, 2, 0);
    REQUIRE_FALSE(bytes.is_empty());
    AckFrame received;
    REQUIRE(netw::predict::decode_ack(bytes, received));
    NETW_CHECK_EQ(received.epoch, 9);
    NETW_CHECK_EQ(received.base, 1);
    NETW_CHECK_EQ(received.records.size(), 2);
    NETW_CHECK_EQ(received.records[0].c_hash, 101);
    NETW_CHECK_EQ(received.records[0].pre_fp, 201);
    NETW_CHECK_EQ(received.records[0].post_fp, 301);
    NETW_CHECK_EQ(received.records[1].post_controller_fp, 332);

    pool->mark_attribution(slot, 1, int(netw::predict::Attribution::PRE_STATE));
    const netw::predict::JournalRow row = pool->journal_row(slot, 1);
    REQUIRE(row.present);
    NETW_CHECK_EQ(row.transition, 1);
    NETW_CHECK_EQ(row.label, 21);
    NETW_CHECK_EQ(row.c_hash, 101);
    NETW_CHECK_EQ(row.pre_fp, 201);
    NETW_CHECK_EQ(row.pre_families.controller, 231);
    NETW_CHECK_EQ(row.post_fp, 301);
    NETW_CHECK_EQ(row.post_families.controller, 331);
    NETW_CHECK_EQ(
        int(row.attribution),
        int(netw::predict::Attribution::PRE_STATE)
    );

    const PackedByteArray unconfirmed = pool->build_ack_frame(slot, -1, 2, 0);
    REQUIRE_FALSE(unconfirmed.is_empty());
    AckFrame sentinel;
    REQUIRE(netw::predict::decode_ack(unconfirmed, sentinel));
    NETW_CHECK_EQ(sentinel.epoch, UINT8_MAX);
}

TEST_CASE(
    "[Networked][PredictWire][Hosted] transition lookup keeps the shared "
    "oldest-first index after wrap"
) {
    netw::NetwPredictionEngine held_pool;
    netw::NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open();
    REQUIRE(pool->configure(
        slot,
        int(netw::Schedule::TICK),
        int(netw::Role::CONSUME),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT)
    ));

    const int count = netw::predict::JOURNAL_CAPACITY_DEFAULT + 2;
    for (int transition = 0; transition < count; ++transition) {
        pool->record_input(slot, transition, transition + 100);
        REQUIRE(pool->replay_drive(
                        slot,
                        godot::Dictionary(),
                        transition,
                        transition,
                        int(netw::DriveKind::FRESH),
                        transition,
                        transition,
                        1.0 / 60.0,
                        1,
                        transition,
                        0,
                        0,
                        0
        )
                    .ran);
        pool->close_drive(slot, transition, transition, 0, 0, 0);
    }

    NETW_CHECK_EQ(pool->journal_size(slot), count - 2);
    NETW_CHECK_EQ(pool->journal_slot_of(slot, 2), 0);
    NETW_CHECK_EQ(pool->journal_slot_of(slot, count - 1), count - 3);
    NETW_CHECK_EQ(pool->journal_transition_at(slot, 0), 2);
    NETW_CHECK_EQ(
        pool->journal_row(slot, 2).transition,
        pool->journal_transition_at(slot, 0)
    );
}

TEST_CASE(
    "[Networked][PredictWire][Hosted] tape reset restarts FRAME numbering"
) {
    netw::NetwPredictionEngine held_pool;
    netw::NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = pool->open();
    REQUIRE(pool->configure(
        slot,
        int(netw::Schedule::FRAME),
        int(netw::Role::PREDICT),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT)
    ));

    pool->record_input(slot, 1, 11);
    REQUIRE(pool->open_drive(
                    slot,
                    godot::Dictionary(),
                    1,
                    1,
                    1.0 / 60.0,
                    1,
                    true,
                    10,
                    0,
                    0,
                    0
    )
                .ran);
    pool->tape_reset(slot, 7);

    pool->record_input(slot, 1, 12);
    const netw::predict::DriveRecord restarted = pool->open_drive(
        slot,
        godot::Dictionary(),
        1,
        2,
        1.0 / 60.0,
        1,
        true,
        20,
        0,
        0,
        0
    );
    REQUIRE(restarted.ran);
    NETW_CHECK_EQ(restarted.transition, 0);
    NETW_CHECK_EQ(pool->journal_epoch(slot), 7);
    NETW_CHECK_EQ(pool->journal_size(slot), 1);
}

TEST_CASE(
    "[Networked][PredictWire][Hosted] ack takes the oldest MTU-safe prefix"
) {
    AckFrame sent;
    sent.base = 0x7fffffff;
    for (int at = 0; at < 64; ++at) {
        sent.records.push_back(ack_evidence(at, true));
    }
    const PackedByteArray bytes = netw::predict::encode_ack(sent);
    REQUIRE_FALSE(bytes.is_empty());
    CHECK(bytes.size() <= ACK_FRAME_BYTES_MAX);

    AckFrame received;
    REQUIRE(netw::predict::decode_ack(bytes, received));
    NETW_CHECK_EQ(received.records.size(), ACK_RECORD_MAX);
    NETW_CHECK_EQ(received.records[0].pre_fp, 1);
    NETW_CHECK_EQ(received.records[ACK_RECORD_MAX - 1].pre_fp, 21);
}

TEST_CASE(
    "[Networked][PredictWire][Hosted] decoders reject truncation and "
    "residue"
) {
    AckFrame sent;
    sent.records.push_back(ack_evidence(0, true));
    PackedByteArray bytes = netw::predict::encode_ack(sent);
    REQUIRE_FALSE(bytes.is_empty());

    PackedByteArray truncated = bytes;
    truncated.resize(truncated.size() - 1);
    AckFrame unchanged;
    unchanged.epoch = 44;
    CHECK_FALSE(netw::predict::decode_ack(truncated, unchanged));
    NETW_CHECK_EQ(unchanged.epoch, 44);

    bytes.push_back(0);
    CHECK_FALSE(netw::predict::decode_ack(bytes, unchanged));
    NETW_CHECK_EQ(unchanged.epoch, 44);
}

TEST_CASE(
    "[Networked][PredictWire][Hosted] relay request is one strict boolean "
    "byte"
) {
    const PackedByteArray subscribed
        = netw::predict::encode_relay_request(true);
    REQUIRE_FALSE(subscribed.is_empty());
    NETW_CHECK_EQ(subscribed.size(), 1);
    NETW_CHECK_EQ(subscribed[0], 1);

    bool received = false;
    REQUIRE(netw::predict::decode_relay_request(subscribed, received));
    CHECK(received);

    PackedByteArray residue = subscribed;
    residue.push_back(0);
    CHECK_FALSE(netw::predict::decode_relay_request(residue, received));
}

TEST_CASE(
    "[Networked][PredictWire][Hosted] the relay book admits and drops in "
    "admission order, and a slot nobody subscribes to holds no row at all"
) {
    netw::predict::RelayBook book;

    book.set_subscribed(4, 30, true);
    book.set_subscribed(4, 10, true);
    book.set_subscribed(4, 30, true);
    book.set_subscribed(5, 10, true);

    CHECK(book.subscribed(4, 10));
    CHECK_FALSE(book.subscribed(5, 30));
    NETW_CHECK_EQ(book.peer_count(4), 2);
    NETW_CHECK_EQ(book.peers(4)[0], 30);
    NETW_CHECK_EQ(book.peers(4)[1], 10);

    book.set_subscribed(4, 30, false);
    NETW_CHECK_EQ(book.peer_count(4), 1);
    book.set_subscribed(4, 10, false);
    NETW_CHECK_EQ(book.slot_count(), 1);

    book.release(5);
    NETW_CHECK_EQ(book.slot_count(), 0);
    NETW_CHECK_EQ(book.peers(5).size(), 0);
}

constexpr godot::Error LIVE = godot::Error::OK;

bool lane_named(
    const WireRegistry &p_registry,
    uint8_t p_id,
    const char *p_name
) {
    const netw::wire::ChannelDecl *decl = p_registry.find_channel(p_id);
    return decl != nullptr && decl->name == godot::StringName(p_name);
}

netw::predict::FrameOrigin from(int64_t p_sender, bool p_server) {
    netw::predict::FrameOrigin origin;
    origin.sender = p_sender;
    origin.controller = OWNER_PEER;
    origin.receiver_is_server = p_server;
    return origin;
}

TEST_CASE(
    "[Networked][PredictWire][Hosted] the lane ids are the registry "
    "declarations"
) {
    const WireRegistry registry = WireRegistry::create_default();

    CHECK(
        lane_named(registry, netw::predict::CHANNEL_COMMAND, "PREDICT_COMMAND")
    );
    CHECK(lane_named(registry, netw::predict::CHANNEL_ACK, "PREDICT_ACK"));
    CHECK(lane_named(registry, netw::predict::CHANNEL_RELAY, "PREDICT_RELAY"));
    CHECK(lane_named(
        registry,
        netw::predict::CHANNEL_RELAY_REQUEST,
        "PREDICT_RELAY_REQUEST"
    ));
}

TEST_CASE(
    "[Networked][PredictWire][Hosted] admission takes the authorship rule "
    "from the declared direction"
) {
    const WireRegistry registry = WireRegistry::create_default();
    using netw::predict::admit_frame;

    SUBCASE("a command is the controller's, and only at the server") {
        NETW_CHECK_EQ(
            admit_frame(
                registry,
                netw::predict::CHANNEL_COMMAND,
                from(OWNER_PEER, true),
                false,
                LIVE
            ),
            godot::Error::OK
        );
        NETW_CHECK_EQ(
            admit_frame(
                registry,
                netw::predict::CHANNEL_COMMAND,
                from(OWNER_PEER + 1, true),
                false,
                LIVE
            ),
            godot::Error::ERR_UNAUTHORIZED
        );
        NETW_CHECK_EQ(
            admit_frame(
                registry,
                netw::predict::CHANNEL_COMMAND,
                from(OWNER_PEER, false),
                false,
                LIVE
            ),
            godot::Error::ERR_UNAUTHORIZED
        );
    }

    SUBCASE("an ack and a relay are the server's alone") {
        for (const uint8_t channel :
             {netw::predict::CHANNEL_ACK, netw::predict::CHANNEL_RELAY}) {
            NETW_CHECK_EQ(
                admit_frame(
                    registry,
                    channel,
                    from(netw::predict::SERVER_PEER, false),
                    false,
                    LIVE
                ),
                godot::Error::OK
            );
            NETW_CHECK_EQ(
                admit_frame(
                    registry,
                    channel,
                    from(OWNER_PEER, false),
                    false,
                    LIVE
                ),
                godot::Error::ERR_UNAUTHORIZED
            );
        }
    }

    SUBCASE("a relay request is answered only by a server") {
        NETW_CHECK_EQ(
            admit_frame(
                registry,
                netw::predict::CHANNEL_RELAY_REQUEST,
                from(OWNER_PEER, true),
                false,
                LIVE
            ),
            godot::Error::OK
        );
        NETW_CHECK_EQ(
            admit_frame(
                registry,
                netw::predict::CHANNEL_RELAY_REQUEST,
                from(OWNER_PEER, false),
                false,
                LIVE
            ),
            godot::Error::ERR_UNAUTHORIZED
        );
    }
}

TEST_CASE(
    "[Networked][PredictWire][Hosted] the route's verdict outranks the "
    "frame's, and a lane it does not own is refused"
) {
    const WireRegistry registry = WireRegistry::create_default();
    using netw::predict::admit_frame;

    NETW_CHECK_EQ(
        admit_frame(
            registry,
            netw::predict::CHANNEL_COMMAND,
            from(OWNER_PEER + 1, true),
            false,
            godot::Error::ERR_DOES_NOT_EXIST
        ),
        godot::Error::ERR_DOES_NOT_EXIST
    );
    NETW_CHECK_EQ(
        admit_frame(
            registry,
            netw::predict::CHANNEL_COMMAND,
            from(OWNER_PEER, true),
            true,
            LIVE
        ),
        godot::Error::ERR_INVALID_DATA
    );
    CHECK_FALSE(netw::predict::is_lane(1));
    NETW_CHECK_EQ(
        admit_frame(
            registry,
            1,
            from(netw::predict::SERVER_PEER, true),
            false,
            LIVE
        ),
        godot::Error::ERR_INVALID_DATA
    );
}

TEST_CASE(
    "[Networked][PredictWire][Hosted] a run with no fresh transition carries "
    "no payload"
) {
    const WirePlan plan = input_plan();
    CommandFrame sent;
    sent.transitions.push_back(TransitionWire{9, 40, false});
    sent.transitions.push_back(TransitionWire{10, 40, false});

    CommandFrame received;
    REQUIRE(
        netw::predict::decode_command(
            netw::predict::encode_command(sent, plan),
            plan,
            received
        )
    );
    NETW_CHECK_EQ(received.transitions.size(), 2);
    NETW_CHECK_EQ(received.payloads.size(), 0);
}

TEST_CASE(
    "[Networked][PredictWire][Hosted] a gapped transition run has no encoding"
) {
    const WirePlan plan = input_plan();
    CommandFrame gapped;
    gapped.transitions.push_back(TransitionWire{4, 10, true});
    gapped.transitions.push_back(TransitionWire{6, 12, true});
    gapped.payloads.push_back(input_row(plan, 1));
    gapped.payloads.push_back(input_row(plan, 2));

    CHECK(netw::predict::encode_command(gapped, plan).is_empty());
}

TEST_CASE("[Networked][PredictWire][Hosted] the evidence section is optional") {
    const WirePlan plan = input_plan();
    CommandFrame bare;
    bare.transitions.push_back(TransitionWire{0, 5, true});
    bare.transitions.push_back(TransitionWire{1, 5, false});
    bare.payloads.push_back(input_row(plan, 7));

    CommandFrame received;
    REQUIRE(
        netw::predict::decode_command(
            netw::predict::encode_command(bare, plan),
            plan,
            received
        )
    );
    NETW_CHECK_EQ(received.evidence.size(), 0);
    NETW_CHECK_EQ(received.transitions.size(), 2);
}

TEST_CASE(
    "[Networked][PredictWire][Hosted] a frame needs a planned input schema"
) {
    SchemaRecord unsealed;
    unsealed.name = "Unsealed";
    SchemaCore::append_column(&unsealed, "motion", SchemaCore::VECTOR2, 1);
    CHECK_FALSE(WirePlan::compile(unsealed).valid());

    SchemaRecord self_describing;
    self_describing.name = "SelfDescribing";
    SchemaCore::append_column(
        &self_describing,
        "anything",
        SchemaCore::VARIANT,
        1
    );
    SchemaCore::fix(&self_describing);
    CHECK_FALSE(WirePlan::compile(self_describing).valid());

    CommandFrame frame;
    frame.transitions.push_back(TransitionWire{0, 5, true});
    frame.payloads.push_back(input_row(input_plan(), 3));
    CHECK(
        netw::predict::encode_command(frame, WirePlan::compile(self_describing))
            .is_empty()
    );
}

TEST_CASE(
    "[Networked][PredictWire][Hosted] an extreme digest survives the ack "
    "crossing unchanged"
) {
    AckFrame sent;
    AckEvidenceWire record = ack_evidence(0, false);
    record.e_digest = -2147483647 - 1;
    sent.records.push_back(record);

    AckFrame received;
    REQUIRE(
        netw::predict::decode_ack(netw::predict::encode_ack(sent), received)
    );
    NETW_CHECK_EQ(received.records.size(), 1);
    NETW_CHECK_EQ(received.records[0].e_digest, -2147483647 - 1);
}

TEST_CASE(
    "[Networked][PredictWire][Hosted] substitution is declared per transition"
) {
    AckFrame sent;
    AckEvidenceWire plain = ack_evidence(0, false);
    plain.flags = 0;
    AckEvidenceWire substituted = ack_evidence(10, false);
    substituted.flags = netw::predict::ACK_SUBSTITUTED;
    sent.records.push_back(plain);
    sent.records.push_back(substituted);

    AckFrame received;
    REQUIRE(
        netw::predict::decode_ack(netw::predict::encode_ack(sent), received)
    );
    REQUIRE(received.records.size() == 2);
    CHECK((received.records[0].flags & netw::predict::ACK_SUBSTITUTED) == 0);
    CHECK((received.records[1].flags & netw::predict::ACK_SUBSTITUTED) != 0);
}

} // namespace TestNetwPredictFrames
