#include "support/netw_test.h"

#include <cstdint>
#include <string>

#include "godot/variant.hpp"
#include "netw/api/quantize.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/repl/snapshot_frame.hpp"
#include "netw/store/world.hpp"

using namespace godot;

namespace TestNetwStoreReceive {

using netw::SchemaCore;
using netw::repl::SnapshotHeader;
using netw::store::Family;
using netw::store::Landing;
using netw::store::RowAddress;
using netw::store::Table;
using netw::store::World;
using netw::table::SchemaColumn;
using netw::table::SchemaRecord;
using netw::wire::CodeRow;

constexpr int SENDER = 5;
constexpr uint64_t TOKEN = 77;
constexpr int64_t BASE_TICK = 100;

SchemaRecord pose_schema() {
    SchemaRecord record;
    SchemaColumn where;
    where.key = StringName("where");
    where.type = SchemaCore::VECTOR3;
    Ref<netw::NetwQuantizeScalar> grid;
    grid.instantiate();
    grid->set_bit_count(12);
    grid->set_min_limit(-8.0);
    grid->set_max_limit(8.0);
    where.quantizer = grid;
    record.columns.push_back(where);
    SchemaColumn awake;
    awake.key = StringName("awake");
    awake.type = SchemaCore::BOOL;
    record.columns.push_back(awake);
    SchemaCore::fix(&record);
    return record;
}

struct Pose {
    Vector3 where;
    bool awake = false;
};

struct Stand {
    World world;
    uint32_t table = 0;
    RowAddress at;

    Stand() {
        table = world.table_for(1, Family::VOLATILE, pose_schema());
        at = world.add_row(table, 7, 0, 7, 0);
    }

    Table &held() {
        return world.table(table);
    }

    CodeRow row_of(const Pose &p_pose) {
        CodeRow row = CodeRow::for_plan(held().plan);
        held().encode(0, p_pose.where, row);
        held().encode(1, p_pose.awake, row);
        return row;
    }

    PackedByteArray frame(
        const Pose &p_pose,
        uint64_t p_revision,
        int64_t p_tick,
        const Pose *p_base = nullptr,
        uint64_t p_distance = 0
    ) {
        const CodeRow row = row_of(p_pose);
        SnapshotHeader header;
        header.token = TOKEN;
        header.revision = p_revision;
        header.tick = p_tick;
        header.distance = p_base == nullptr ? 0 : p_distance;
        if (p_base == nullptr) {
            return netw::repl::write_snapshot_row(
                header,
                BASE_TICK,
                held().plan,
                row,
                nullptr
            );
        }
        const CodeRow base = row_of(*p_base);
        header.mask = CodeRow::changed_mask(held().plan, base, row);
        return netw::repl::write_snapshot_row(
            header,
            BASE_TICK,
            held().plan,
            row,
            &base
        );
    }

    Landing take(const PackedByteArray &p_frame) {
        return world.receive(at, SENDER, p_frame, BASE_TICK, false);
    }

    std::string codes() {
        std::string out;
        const uint64_t *words = held().codes_of(at.row);
        for (uint32_t word = 0; word < held().words; ++word) {
            out += std::to_string(words[word]) + ",";
        }
        return out;
    }

    bool holds(const Pose &p_pose) {
        const CodeRow expected = row_of(p_pose);
        const uint64_t *words = held().codes_of(at.row);
        for (uint32_t word = 0; word < held().words; ++word) {
            if (words[word] != expected.word_data()[word]) {
                return false;
            }
        }
        return true;
    }
};

TEST_CASE(
    "[Networked][Store][Hosted] R1 a refused frame leaves the row's codes, "
    "values and history exactly as the last accepted frame left them"
) {
    Stand stand;
    const Pose first{Vector3(1.0, 2.0, 3.0), true};
    const Pose other{Vector3(-4.0, 0.5, 2.0), false};
    NETW_REQUIRE_EQ(
        int(stand.take(stand.frame(first, 2, 101))),
        int(Landing::ACCEPTED)
    );
    const std::string codes = stand.codes();
    const Variant where = stand.held().read_value(stand.at.row, 0);
    const Variant awake = stand.held().read_value(stand.at.row, 1);
    const uint32_t samples = stand.held().history_count(stand.at.row);
    const int64_t newest = stand.held().history_tick(stand.at.row, 0);

    NETW_CHECK_EQ(
        int(stand.take(stand.frame(other, 1, 102))),
        int(Landing::STALE)
    );
    NETW_CHECK_EQ(
        int(stand.take(stand.frame(other, 2, 102))),
        int(Landing::DUPLICATE)
    );
    NETW_CHECK_EQ(
        int(stand.take(stand.frame(other, 6, 102, &first, 3))),
        int(Landing::BASELINE_UNKNOWN)
    );
    PackedByteArray torn = stand.frame(other, 7, 102);
    torn.resize(torn.size() - 1);
    NETW_CHECK_EQ(int(stand.take(torn)), int(Landing::MALFORMED));

    CHECK(stand.codes() == codes);
    CHECK(bool(stand.held().read_value(stand.at.row, 0) == where));
    CHECK(bool(stand.held().read_value(stand.at.row, 1) == awake));
    NETW_CHECK_EQ(stand.held().history_count(stand.at.row), samples);
    NETW_CHECK_EQ(stand.held().history_tick(stand.at.row, 0), newest);
    NETW_CHECK_EQ(stand.held().received_ticks[stand.at.row], int64_t(101));
}

TEST_CASE(
    "[Networked][Store][Hosted] R2 a delta decodes against the revision it "
    "names, and a receiver's values are the dequantized codes"
) {
    Stand stand;
    const Pose first{Vector3(1.0, 1.0, 1.0), false};
    const Pose second{Vector3(1.0, 1.0, 1.0), true};
    const Pose third{Vector3(-3.0, 2.5, 0.25), false};
    NETW_REQUIRE_EQ(
        int(stand.take(stand.frame(first, 1, 101))),
        int(Landing::ACCEPTED)
    );
    NETW_REQUIRE_EQ(
        int(stand.take(stand.frame(second, 2, 102, &first, 1))),
        int(Landing::ACCEPTED)
    );
    NETW_REQUIRE_EQ(
        int(stand.take(stand.frame(third, 3, 103, &first, 2))),
        int(Landing::ACCEPTED)
    );
    CHECK(stand.holds(third));
    const Vector3 where = stand.held().read_value(stand.at.row, 0);
    NETW_CHECK_CLOSE(where.x, -3.0, 0.005);
    NETW_CHECK_CLOSE(where.y, 2.5, 0.005);
    NETW_CHECK_CLOSE(where.z, 0.25, 0.005);
    CHECK_FALSE(bool(stand.held().read_value(stand.at.row, 1)));
    CodeRow requantized = CodeRow::for_plan(stand.held().plan);
    stand.held().encode(0, where, requantized);
    stand.held().encode(1, false, requantized);
    CHECK(stand.holds(third));
    CHECK(requantized.equals(stand.row_of(third)));
}

TEST_CASE(
    "[Networked][Store][Hosted] R4 a received row appends its history at the "
    "frame's tick only when its codes change, a change after an unchanged row "
    "first holds the old value one tick earlier, and history answers the "
    "newest sample at or before a tick"
) {
    Stand stand;
    const Pose first{Vector3(1.0, 0.0, 0.0), false};
    const Pose second{Vector3(2.0, 0.0, 0.0), false};
    NETW_REQUIRE_EQ(
        int(stand.take(stand.frame(first, 1, 110))),
        int(Landing::ACCEPTED)
    );
    NETW_REQUIRE_EQ(
        int(stand.take(stand.frame(first, 2, 111))),
        int(Landing::ACCEPTED)
    );
    NETW_CHECK_EQ(stand.held().history_count(stand.at.row), uint32_t(1));
    NETW_CHECK_EQ(stand.held().received_ticks[stand.at.row], int64_t(111));
    NETW_REQUIRE_EQ(
        int(stand.take(stand.frame(second, 3, 113))),
        int(Landing::ACCEPTED)
    );
    NETW_CHECK_EQ(stand.held().history_count(stand.at.row), uint32_t(3));
    NETW_CHECK_EQ(stand.held().history_tick(stand.at.row, 1), int64_t(112));
    NETW_CHECK_EQ(stand.held().history_at(stand.at.row, 111), 2);
    NETW_CHECK_EQ(stand.held().history_at(stand.at.row, 109), -1);
    NETW_CHECK_EQ(stand.held().history_at(stand.at.row, 112), 1);
    NETW_CHECK_EQ(stand.held().history_at(stand.at.row, 113), 0);
    const Vector3 older = stand.held().history_value(stand.at.row, 1, 0);
    NETW_CHECK_CLOSE(older.x, 1.0, 0.005);

    for (int step = 0; step < 12; ++step) {
        const Pose moving{Vector3(-1.0 - 0.25 * step, 0.0, 0.0), step % 2 == 0};
        stand.take(stand.frame(moving, uint64_t(4 + step), 114 + step));
    }
    NETW_CHECK_EQ(
        stand.held().history_count(stand.at.row),
        Table::HISTORY_DEPTH
    );
    NETW_CHECK_EQ(stand.held().history_tick(stand.at.row, 0), int64_t(125));
}

TEST_CASE(
    "[Networked][Store][Hosted] R5 a row that moves on a removal keeps its "
    "accepted images and its history, so its next delta still decodes"
) {
    Stand stand;
    const RowAddress moved = stand.world.add_row(stand.table, 8, 0, 8, 0);
    const Pose first{Vector3(3.0, 3.0, 3.0), true};
    const Pose second{Vector3(3.0, -3.0, 3.0), true};
    NETW_REQUIRE_EQ(
        int(stand.world.receive(
            moved,
            SENDER,
            stand.frame(first, 1, 101),
            BASE_TICK,
            false
        )),
        int(Landing::ACCEPTED)
    );
    stand.world.remove_row(stand.at);
    const RowAddress now = stand.world.row_of(8, 0);
    REQUIRE(now.valid());
    NETW_CHECK_EQ(now.row, uint32_t(0));
    NETW_CHECK_EQ(stand.held().history_count(now.row), uint32_t(1));
    NETW_CHECK_EQ(
        int(stand.world.receive(
            now,
            SENDER,
            stand.frame(second, 2, 102, &first, 1),
            BASE_TICK,
            false
        )),
        int(Landing::ACCEPTED)
    );
    stand.at = now;
    CHECK(stand.holds(second));
}

} // namespace TestNetwStoreReceive
