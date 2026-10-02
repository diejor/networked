#include "support/netw_test.h"
#include "support/predict_rows.h"

#include "godot/variant.hpp"
#include "netw/predict/engine.hpp"

using namespace godot;

namespace TestNetwPredictColumnCompareLaws {

using netw::NetwPredictionEngine;
using netw::predict::FieldDecl;
using netw::predict::StateRow;
using netw::predict::StateVerdict;

constexpr double LOOSE_FALLBACK = 1000.0;

struct Column {
    Variant::Type type = Variant::NIL;
    double tolerance = 0.0;
    Variant authority;
    Variant inside;
    Variant outside;
};

bool corrects(const Column &p_column, const Variant &p_predicted) {
    NetwPredictionEngine pool;
    const StringName key("value");
    LocalVector<FieldDecl> declared;
    FieldDecl field;
    field.key = key;
    field.type = int(p_column.type);
    field.epsilon_override = p_column.tolerance;
    declared.push_back(field);
    const int64_t slot = pool.open(declared);
    REQUIRE(pool.configure(
        slot,
        int(netw::Schedule::TICK),
        int(netw::Role::PREDICT),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT)
    ));
    pool.set_stream_reconstructed(slot, true);
    pool.record_input(slot, 1, 1);
    const netw::predict::DriveRecord drive = pool.open_drive(
        slot,
        Dictionary(),
        1,
        1,
        1.0 / 60.0,
        1,
        true,
        0,
        0,
        0,
        0
    );
    REQUIRE(drive.ran);
    pool.close_drive(slot, drive.transition, 0, 0, 0, 0);

    Dictionary predicted;
    predicted[key] = p_predicted;
    Dictionary authority;
    authority[key] = p_column.authority;
    const StateRow predicted_row
        = netw_test::state_row(&pool, slot, predicted);
    const StateRow authority_row
        = netw_test::state_row(&pool, slot, authority);
    const StateVerdict *verdict = pool.compare_rows(
        slot,
        drive.transition,
        predicted_row,
        authority_row,
        int(netw::predict::Domain::IN_DOMAIN),
        LOOSE_FALLBACK
    );
    REQUIRE(verdict != nullptr);
    REQUIRE(verdict->compared);
    return verdict->corrected;
}

void judge(const Column &p_column) {
    CHECK_FALSE(corrects(p_column, p_column.authority));
    CHECK_FALSE(corrects(p_column, p_column.inside));
    CHECK(corrects(p_column, p_column.outside));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Compare] CT1 a float column compares within "
    "the Wiring's tolerance, so a value inside it is equal and one outside is "
    "not"
) {
    Column column;
    column.type = Variant::FLOAT;
    column.tolerance = 0.5;
    column.authority = 1.0;
    column.inside = 1.3;
    column.outside = 2.0;
    judge(column);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Compare] CT2 a vector column compares by "
    "distance within the Wiring's tolerance"
) {
    Column flat;
    flat.type = Variant::VECTOR2;
    flat.tolerance = 0.5;
    flat.authority = Vector2(1.0, 1.0);
    flat.inside = Vector2(1.3, 1.1);
    flat.outside = Vector2(3.0, 1.0);
    judge(flat);

    Column solid;
    solid.type = Variant::VECTOR3;
    solid.tolerance = 0.5;
    solid.authority = Vector3(1.0, 2.0, 3.0);
    solid.inside = Vector3(1.2, 2.2, 3.0);
    solid.outside = Vector3(1.0, 2.0, 4.0);
    judge(solid);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Compare] CT3 a quaternion column compares "
    "by angle within the Wiring's tolerance"
) {
    Column column;
    column.type = Variant::QUATERNION;
    column.tolerance = 0.1;
    column.authority = Quaternion();
    column.inside = Quaternion(Vector3(0.0, 1.0, 0.0), 0.05);
    column.outside = Quaternion(Vector3(0.0, 1.0, 0.0), 0.5);
    judge(column);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Compare] CT4 a bool column is equal only "
    "when it holds the same value"
) {
    Column column;
    column.type = Variant::BOOL;
    column.tolerance = 0.5;
    column.authority = true;
    column.inside = true;
    column.outside = false;
    judge(column);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Compare] CT5 an int column compares by "
    "difference within the Wiring's tolerance"
) {
    Column column;
    column.type = Variant::INT;
    column.tolerance = 2.0;
    column.authority = int64_t(10);
    column.inside = int64_t(11);
    column.outside = int64_t(20);
    judge(column);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Compare] CT6 a carried column is equal only "
    "when it holds the same value"
) {
    Column column;
    column.type = Variant::STRING;
    column.tolerance = 0.5;
    column.authority = String("red");
    column.inside = String("red");
    column.outside = String("blue");
    judge(column);
}

} // namespace TestNetwPredictColumnCompareLaws
