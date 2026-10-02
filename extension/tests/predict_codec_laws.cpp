#include "support/netw_test.h"

#include <cstdint>

#include "godot/variant.hpp"
#include "netw/api/quantize.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/predict/wiring.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/value_row.hpp"

using namespace godot;

namespace TestNetwPredictCodec {

using netw::SchemaCore;
using netw::predict::FieldDecl;
using netw::predict::Wiring;
using netw::table::SchemaColumn;
using netw::table::SchemaRecord;
using netw::wire::CodeRow;

Ref<netw::NetwQuantizeScalar> grid() {
    Ref<netw::NetwQuantizeScalar> made;
    made.instantiate();
    made->set_bit_count(12);
    made->set_min_limit(-8.0);
    made->set_max_limit(8.0);
    return made;
}

FieldDecl decl(const StringName &p_key, Variant::Type p_type) {
    FieldDecl out;
    out.key = p_key;
    out.type = int(p_type);
    return out;
}

SchemaColumn column(const StringName &p_key, int p_type) {
    SchemaColumn out;
    out.key = p_key;
    out.type = p_type;
    return out;
}

TEST_CASE(
    "[Networked][Predict][Hosted] C1 a predicted row's canonical form is the "
    "store codec's encode then decode, so a float field round-trips as f64 "
    "and a quantized field lands on the store's bin"
) {
    const Ref<netw::NetwQuantizeScalar> bins = grid();
    LocalVector<FieldDecl> declared;
    declared.push_back(decl(StringName("speed"), Variant::FLOAT));
    FieldDecl where = decl(StringName("where"), Variant::VECTOR3);
    where.quantizer = bins;
    declared.push_back(where);
    declared.push_back(decl(StringName("label"), Variant::STRING));
    const Wiring wiring = netw::predict::compile(declared);

    const double speed = 0.1;
    const Vector3 at(1.2345, -3.3, 0.7);
    Dictionary payload;
    payload[StringName("speed")] = speed;
    payload[StringName("where")] = at;
    payload[StringName("label")] = String("left");
    const Dictionary canonical
        = netw::predict::canonicalize(wiring.codec, payload);

    SchemaRecord store;
    store.columns.push_back(column(StringName("speed"), SchemaCore::F64));
    SchemaColumn quantized = column(StringName("where"), SchemaCore::VECTOR3);
    quantized.quantizer = bins;
    store.columns.push_back(quantized);
    store.columns.push_back(column(StringName("label"), SchemaCore::STRING));
    SchemaCore::fix(&store);
    Array values;
    values.push_back(speed);
    values.push_back(at);
    values.push_back(String("left"));
    CodeRow coded;
    REQUIRE(netw::wire::encode_scalar_row(store, values, coded));
    Array decoded;
    REQUIRE(netw::wire::decode_scalar_row(store, coded, decoded));
    REQUIRE(decoded.size() == 3);

    const double held = canonical.get(StringName("speed"), 0.0);
    NETW_CHECK_CLOSE(held, speed, 0.0);
    NETW_CHECK_CLOSE(held, double(decoded[0]), 0.0);
    const Vector3 landed = canonical.get(StringName("where"), Vector3());
    const Vector3 binned = decoded[1];
    CHECK(bool(landed == binned));
    CHECK_FALSE(bool(landed == at));
    CHECK(bool(
        String(canonical.get(StringName("label"), String())) == String("left")
    ));
}

} // namespace TestNetwPredictCodec
