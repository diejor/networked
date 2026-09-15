#include "support/netw_test.h"

#include "support/composed_link.h"

#include <cstdint>

#include "godot/templates.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/wire/plan.hpp"

namespace TestWireLadderSchedule {

using netw::SchemaCore;
using netw::table::DeltaMode;
using netw::table::SchemaRecord;
using netw::wire::CodeRow;
using netw::wire::WirePlan;
using netw_test::ComposedLink;

constexpr uint64_t GENERATOR_SEED = 0x5CED017ull;
constexpr int SCHEDULE_COUNT = 64;
constexpr int EVENTS_PER_SCHEDULE = 40;
constexpr uint64_t CODE_CEILING = 65535;
constexpr int POSE_COLUMNS = 9;
constexpr int NARROW_COLUMNS = 2;

const int64_t EVERY_MAGNITUDE[] = {0,   1,    -1,   7,    8,     -8,
                                   15,  16,   -16,  127,  128,   -128,
                                   255, 256,  -256, 4096, -4096, 31000};

const int64_t INTEGRATED[] = {0,  1, -1, 2, -2, 3, -3, 4, -4, 5,
                              -5, 6, -6, 7, -7, 5, -5, 2, -2, 1};

const int64_t NUDGED[] = {9, -9, 20, -20, 60, -60, 120, -120};

const int64_t TELEPORTED[] = {600, -600, 4096, -4096, 20000};

WirePlan laddered_plan(int p_columns) {
    SchemaRecord record;
    record.name = godot::StringName("LadderSchedule");
    for (int at = 0; at < p_columns; ++at) {
        SchemaCore::append_column(
            &record,
            godot::StringName(godot::String::num_int64(at)),
            SchemaCore::I16,
            1
        );
        record.at(at)->delta = DeltaMode::LADDER;
    }
    SchemaCore::fix(&record);
    return WirePlan::compile(record);
}

struct Roller {
    uint64_t state = 0;

    uint64_t next() {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return state >> 17;
    }

    int below(int p_bound) {
        return int(next() % uint64_t(p_bound));
    }
};

int64_t every_magnitude_step(Roller &p_roll) {
    return EVERY_MAGNITUDE[p_roll.below(18)];
}

int64_t integrated_step(Roller &p_roll) {
    const int draw = p_roll.below(100);
    if (draw < 78) {
        return INTEGRATED[p_roll.below(20)];
    }
    if (draw < 96) {
        return NUDGED[p_roll.below(8)];
    }
    return TELEPORTED[p_roll.below(5)];
}

uint64_t moved(uint64_t p_code, int64_t p_step) {
    const int64_t at = int64_t(p_code) + p_step;
    if (at < 0) {
        return uint64_t(-at) % (CODE_CEILING + 1);
    }
    return uint64_t(at) % (CODE_CEILING + 1);
}

struct Run {
    int64_t diverged = 0;
    int64_t accepted_unstaged = 0;
    int64_t restated = 0;
    int64_t unconverged = 0;
    int64_t unquiet = 0;
    int64_t written_bytes = 0;
    int64_t absolute_bytes = 0;
    int64_t rows = 0;
    int64_t stepped_rows = 0;
    int64_t stepped_written = 0;
    int64_t stepped_absolute = 0;
};

enum class Arm : uint8_t {
    HEALTHY,
    LOSS,
    REORDER,
};

Run drive_schedules(int p_columns, bool p_integrated, Arm p_arm) {
    Run out;
    for (int schedule = 0; schedule < SCHEDULE_COUNT; ++schedule) {
        Roller roll;
        roll.state = GENERATOR_SEED + uint64_t(schedule);
        ComposedLink link;
        link.open(laddered_plan(p_columns));
        godot::Vector<uint64_t> codes;
        for (int at = 0; at < p_columns; ++at) {
            codes.push_back(uint64_t(1000 + at * 700));
        }
        link.publish(link.row_of(codes));
        link.deliver_all();
        link.apply_all_receipts();

        for (int event = 0; event < EVENTS_PER_SCHEDULE; ++event) {
            if (p_arm == Arm::HEALTHY) {
                for (int at = 0; at < p_columns; ++at) {
                    codes.write[at] = moved(
                        codes[at],
                        p_integrated ? integrated_step(roll)
                                     : every_magnitude_step(roll)
                    );
                }
                link.publish(link.row_of(codes));
                link.deliver_all();
                link.apply_all_receipts();
                continue;
            }
            const int choice = roll.below(20);
            if (choice < 9) {
                for (int at = 0; at < p_columns; ++at) {
                    codes.write[at] = moved(
                        codes[at],
                        p_integrated ? integrated_step(roll)
                                     : every_magnitude_step(roll)
                    );
                }
                link.publish(link.row_of(codes));
            } else if (choice < 13) {
                if (!link.flight.is_empty()) {
                    link.deliver_at(0);
                }
            } else if (choice < 15) {
                if (!link.flight.is_empty()) {
                    link.deliver_at(int32_t(roll.below(link.flight.size())));
                }
            } else if (choice == 15) {
                if (!link.flight.is_empty()) {
                    if (p_arm == Arm::LOSS) {
                        link.drop_at(int32_t(roll.below(link.flight.size())));
                    } else {
                        link.deliver_at(
                            int32_t(roll.below(link.flight.size()))
                        );
                    }
                }
            } else if (choice == 16) {
                if (!link.receipts.is_empty()) {
                    link.apply_receipt_at(0);
                }
            } else if (choice == 17) {
                if (!link.receipts.is_empty()) {
                    link.apply_receipt_at(
                        int32_t(roll.below(link.receipts.size()))
                    );
                }
            } else if (choice == 18) {
                if (p_arm == Arm::LOSS) {
                    link.drop_receipts();
                } else if (!link.receipts.is_empty()) {
                    link.apply_receipt_at(
                        int32_t(roll.below(link.receipts.size()))
                    );
                }
            } else {
                link.now_ms += 400;
                link.repair();
            }
        }

        const bool quiet = link.settle(80);
        const netw_test::RowImageLedger::Verdict verdict = link.ledger.audit();
        out.diverged += verdict.diverged;
        out.accepted_unstaged += verdict.accepted_unstaged;
        out.restated += verdict.restated_revisions;
        out.unquiet += quiet ? 0 : 1;
        bool converged = true;
        for (int at = 0; at < p_columns; ++at) {
            converged = converged && link.column(at) == codes[at];
        }
        out.unconverged += converged ? 0 : 1;
        out.written_bytes += link.written_bytes;
        out.absolute_bytes += link.absolute_bytes;
        out.rows += link.written_rows;
        out.stepped_rows += link.stepped_rows;
        out.stepped_written += link.stepped_written;
        out.stepped_absolute += link.stepped_absolute;
    }
    return out;
}

TEST_CASE(
    "[Networked][Wire][Hosted] LS1 every row a generated schedule accepts "
    "carries the exact image its sender staged, and the lane ends holding the "
    "value the source last published"
) {
    const Arm arms[] = {Arm::HEALTHY, Arm::LOSS, Arm::REORDER};
    for (int arm = 0; arm < 3; ++arm) {
        for (int which = 0; which < 2; ++which) {
            const Run run
                = drive_schedules(POSE_COLUMNS, which == 1, arms[arm]);
            NETW_REQUIRE_EQ(run.rows > 0, true);
            NETW_CHECK_EQ(run.diverged, 0);
            NETW_CHECK_EQ(run.accepted_unstaged, 0);
            NETW_CHECK_EQ(run.restated, 0);
            NETW_CHECK_EQ(run.unconverged, 0);
            NETW_CHECK_EQ(run.unquiet, 0);
        }
    }
}

TEST_CASE(
    "[Networked][Wire][Hosted] LS2 a pose a solver integrates costs a third "
    "fewer bytes laddered on a healthy link than the same rows with every "
    "column forced whole"
) {
    const Run run = drive_schedules(POSE_COLUMNS, true, Arm::HEALTHY);
    NETW_REQUIRE_EQ(run.rows > 0, true);
    const bool the_ladder_saved_bytes = run.written_bytes < run.absolute_bytes;
    CHECK(the_ladder_saved_bytes);
    NETW_CHECK_LE(run.stepped_written * 100, run.stepped_absolute * 70);
    NETW_CHECK_ORDER(run.stepped_rows * 2, run.rows, >);
}

TEST_CASE(
    "[Networked][Wire][Hosted] LS3 a step is measured from the confirmed "
    "snapshot, so loss and reordering shrink the saving without ever "
    "shrinking the accepted image"
) {
    const Run healthy = drive_schedules(POSE_COLUMNS, true, Arm::HEALTHY);
    const Run lost = drive_schedules(POSE_COLUMNS, true, Arm::LOSS);
    const Run reordered = drive_schedules(POSE_COLUMNS, true, Arm::REORDER);
    NETW_CHECK_EQ(lost.diverged + reordered.diverged, 0);
    const bool loss_still_saves
        = lost.stepped_written < lost.stepped_absolute;
    CHECK(loss_still_saves);
    const bool reorder_still_saves
        = reordered.stepped_written < reordered.stepped_absolute;
    CHECK(reorder_still_saves);
    const bool a_healthy_link_saves_most
        = healthy.stepped_written * lost.stepped_absolute
        < lost.stepped_written * healthy.stepped_absolute;
    CHECK(a_healthy_link_saves_most);
}

TEST_CASE(
    "[Networked][Wire][Hosted] LS4 a source that jumps over its whole range "
    "costs the ladder two bits an element and never a wrong image, because a "
    "step too wide for a bucket falls back to the whole code"
) {
    const Run run = drive_schedules(POSE_COLUMNS, false, Arm::HEALTHY);
    NETW_REQUIRE_EQ(run.rows > 0, true);
    NETW_CHECK_EQ(run.diverged, 0);
    NETW_CHECK_LE(run.stepped_written * 100, run.stepped_absolute * 120);
}

TEST_CASE(
    "[Networked][Wire][Hosted] LS5 a row of two columns saves a smaller share "
    "than a pose of nine, because a stream token and a row revision are a "
    "fixed cost the ladder never reaches"
) {
    const Run narrow = drive_schedules(NARROW_COLUMNS, true, Arm::HEALTHY);
    const Run pose = drive_schedules(POSE_COLUMNS, true, Arm::HEALTHY);
    NETW_REQUIRE_EQ(narrow.rows > 0 && pose.rows > 0, true);
    NETW_CHECK_EQ(narrow.diverged, 0);
    const bool a_narrow_row_still_saves
        = narrow.stepped_written < narrow.stepped_absolute;
    CHECK(a_narrow_row_still_saves);
    const bool a_pose_saves_a_larger_share
        = pose.stepped_written * narrow.stepped_absolute
        < narrow.stepped_written * pose.stepped_absolute;
    CHECK(a_pose_saves_a_larger_share);
}


} // namespace TestWireLadderSchedule
