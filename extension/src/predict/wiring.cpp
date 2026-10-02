#include "netw/predict/wiring.hpp"

#include "netw/predict/compare.hpp"

#include "netw/api/schema_core.hpp"
#include "netw/colors.hpp"
#include "netw/profile.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/stream.hpp"
#include "netw/wire/value_row.hpp"

using namespace godot;

namespace netw::predict {

void FieldCodec::append(
    const StringName &p_key,
    const Ref<NetwQuantize> &p_quantizer,
    int p_type,
    int p_column_type
) {
    keys.push_back(p_key);
    quantizers.push_back(p_quantizer);
    types.push_back(p_type);
    table::SchemaColumn column;
    column.key = p_key;
    column.type = p_column_type >= 0 ? p_column_type
                                     : SchemaCore::type_from_variant(p_type);
    column.quantizer = p_quantizer;
    schema.columns.push_back(column);
    schema.sealed = false;
}

void FieldCodec::seal() {
    if (!schema.sealed) {
        SchemaCore::fix(&schema);
    }
    plan = wire::WirePlan::compile(schema);
}

bool FieldCodec::holds(int p_field, const Variant &p_value) const {
    if (!plan.valid() || p_field < 0 || p_field >= int(plan.column_count())) {
        return false;
    }
    const wire::ColumnPlan &slot = plan.column(uint32_t(p_field));
    if (slot.variable) {
        return false;
    }
    const table::SchemaColumn &column = *schema.at(p_field);
    const Variant::Type expected
        = Variant::Type(SchemaCore::element_type(column.type));
    if (p_value.get_type() != expected) {
        return false;
    }
    return column.quantizer.is_null()
        || column.quantizer->supports_type(expected);
}

void FieldCodec::clear() {
    keys.clear();
    quantizers.clear();
    types.clear();
    schema = table::SchemaRecord();
    plan = wire::WirePlan();
}

PackedByteArray canonical_bytes(
    const FieldCodec &p_codec,
    const Dictionary &p_payload
) {
    NETW_ZONE_LEAF(
        "Predict canonical bytes",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    wire::CodeRow row = wire::CodeRow::for_plan(p_codec.plan);
    wire::WriteStream stream;
    uint64_t count = 0;
    for (int at = 0; at < p_codec.count(); ++at) {
        count += p_payload.has(p_codec.keys[at]) ? 1 : 0;
    }
    if (count == 0 || !stream.varuint(count, 2)) {
        return PackedByteArray();
    }
    for (int at = 0; at < p_codec.count(); ++at) {
        if (!p_payload.has(p_codec.keys[at])) {
            continue;
        }
        const Variant value = p_payload[p_codec.keys[at]];
        bool coded = p_codec.holds(at, value)
            && wire::encode_scalar_column(
                *p_codec.schema.at(at),
                p_codec.plan.column(uint32_t(at)),
                uint32_t(at),
                value,
                row
            );
        if (!stream.bool1(coded)) {
            return PackedByteArray();
        }
        if (!coded) {
            Variant carried = value;
            if (!wire::carry_value(stream, carried)) {
                return PackedByteArray();
            }
            continue;
        }
        const wire::ColumnPlan &slot = p_codec.plan.column(uint32_t(at));
        for (int element = 0; element < slot.stride; ++element) {
            uint64_t code = row.read(slot, element);
            if (!stream.bits(code, slot.width)) {
                return PackedByteArray();
            }
        }
    }
    if (!stream.align_verify()) {
        return PackedByteArray();
    }
    return stream.to_bytes();
}

namespace {

const Color ZERO_COLOR = Color(0.0, 0.0, 0.0, 0.0);

bool zero_of(int p_type, Variant &r_zero) {
    switch (Variant::Type(p_type)) {
        case Variant::BOOL:
            r_zero = false;
            return true;
        case Variant::INT:
            r_zero = int64_t(0);
            return true;
        case Variant::FLOAT:
            r_zero = 0.0;
            return true;
        case Variant::STRING:
            r_zero = String();
            return true;
        case Variant::STRING_NAME:
            r_zero = StringName();
            return true;
        case Variant::VECTOR2:
            r_zero = Vector2();
            return true;
        case Variant::VECTOR2I:
            r_zero = Vector2i();
            return true;
        case Variant::VECTOR3:
            r_zero = Vector3();
            return true;
        case Variant::VECTOR3I:
            r_zero = Vector3i();
            return true;
        case Variant::VECTOR4:
            r_zero = Vector4();
            return true;
        case Variant::VECTOR4I:
            r_zero = Vector4i();
            return true;
        case Variant::COLOR:
            r_zero = ZERO_COLOR;
            return true;
        case Variant::ARRAY:
            r_zero = Array();
            return true;
        case Variant::DICTIONARY:
            r_zero = Dictionary();
            return true;
        default:
            return false;
    }
}

} // namespace


void zero_state_row(const FieldCodec &p_codec, StateRow &r_row) {
    r_row.resize(int(p_codec.keys.size()));
    for (uint32_t at = 0; at < p_codec.keys.size(); ++at) {
        Variant zero;
        if (zero_of(p_codec.types[at], zero)) {
            r_row.set(int(at), zero);
        }
    }
}

bool codes_agree(
    const FieldCodec &p_codec,
    const wire::WirePlan &p_plan,
    const table::SchemaRecord &p_schema,
    const Array &p_keys
) {
    const uint32_t count = p_codec.keys.size();
    if (!p_codec.plan.valid() || !p_plan.valid()
        || p_codec.plan.column_count() != count
        || p_plan.column_count() != count
        || p_schema.columns.size() != count || uint32_t(p_keys.size()) != count
        || p_codec.plan.row_bits() != p_plan.row_bits()) {
        return false;
    }
    for (uint32_t at = 0; at < count; ++at) {
        const table::SchemaColumn &own = *p_codec.schema.at(int(at));
        const table::SchemaColumn &wire_column = p_schema.columns[at];
        const wire::ColumnPlan &mine = p_codec.plan.column(at);
        const wire::ColumnPlan &theirs = p_plan.column(at);
        if (StringName(p_keys[int(at)]) != p_codec.keys[at]
            || wire_column.key != own.key || wire_column.type != own.type
            || wire_column.quantizer != own.quantizer || mine.variable
            || theirs.variable || mine.width != theirs.width
            || mine.stride != theirs.stride || mine.offset != theirs.offset) {
            return false;
        }
    }
    return true;
}

Dictionary canonicalize(
    const FieldCodec &p_codec,
    const Dictionary &p_payload
) {
    NETW_ZONE_LEAF(
        "Predict canonicalize",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    Dictionary out = p_payload.duplicate();
    wire::CodeRow row = wire::CodeRow::for_plan(p_codec.plan);
    PackedInt64Array codes;
    for (int at = 0; at < p_codec.count(); ++at) {
        const StringName &key = p_codec.keys[at];
        if (!p_payload.has(key)) {
            continue;
        }
        const Variant value = p_payload[key];
        if (!p_codec.holds(at, value)) {
            continue;
        }
        const table::SchemaColumn &column = *p_codec.schema.at(at);
        const wire::ColumnPlan &slot = p_codec.plan.column(uint32_t(at));
        Variant decoded;
        if (wire::encode_scalar_column(column, slot, uint32_t(at), value, row)
            && wire::decode_scalar_column(
                column,
                slot,
                uint32_t(at),
                row,
                codes,
                decoded
            )) {
            out[key] = decoded;
        }
    }
    return out;
}

void FieldTable::append(const StringName &p_key) {
    if (index.has(p_key)) {
        return;
    }
    index[p_key] = int(names.size());
    names.push_back(p_key);
}

int FieldTable::index_of(const StringName &p_key) const {
    const HashMap<StringName, int>::ConstIterator found = index.find(p_key);
    return found != index.end() ? found->value : -1;
}

const StringName &FieldTable::name_at(int p_slot) const {
    static const StringName NONE;
    if (p_slot < 0 || p_slot >= int(names.size())) {
        return NONE;
    }
    return names[p_slot];
}

void FieldTable::clear() {
    names.clear();
    index.clear();
}

void Wiring::resize(int p_count) {
    const uint32_t count = uint32_t(p_count < 0 ? 0 : p_count);
    projection.resize(count);
    state_family.resize(count);
    converge_rate.resize(count);
    epsilon.resize(count);
    teleport.resize(count);
    pose.resize(count);
    withheld.resize(count);
    trigger_exclude.resize(count);
    vote_exclude.resize(count);
    angle.resize(count);
    causal.resize(count);
    property_class.resize(count);
    carry_channel.resize(count);
    for (uint32_t at = 0; at < count; ++at) {
        projection[at] = -1;
        state_family[at] = int(StateFamily::CONTROLLER);
        converge_rate[at] = -1.0;
        epsilon[at] = -1.0;
        teleport[at] = -1.0;
        pose[at] = 0;
        withheld[at] = 0;
        trigger_exclude[at] = 0;
        vote_exclude[at] = 0;
        angle[at] = 0;
        causal[at] = 0;
        property_class[at] = int(PropertyClass::CAUSAL);
        carry_channel[at] = godot::StringName();
    }
}

Wiring compile(const LocalVector<FieldDecl> &p_declaration) {
    Wiring out;
    for (uint32_t at = 0; at < p_declaration.size(); ++at) {
        const int before = out.fields.count();
        out.fields.append(p_declaration[at].key);
        if (out.fields.count() > before) {
            out.codec.append(
                p_declaration[at].key,
                p_declaration[at].quantizer,
                p_declaration[at].type,
                p_declaration[at].column_type
            );
        }
    }
    out.codec.seal();
    out.resize(out.fields.count());

    for (uint32_t at = 0; at < p_declaration.size(); ++at) {
        const FieldDecl &decl = p_declaration[at];
        const int slot = out.fields.index_of(decl.key);
        if (slot < 0) {
            continue;
        }
        const bool causal = decl.property_class == int(PropertyClass::CAUSAL);
        out.causal[slot] = causal ? 1 : 0;
        out.vote_exclude[slot] = causal ? 0 : 1;
        out.angle[slot] = decl.angle ? 1 : 0;
        out.property_class[slot] = decl.property_class;
        out.carry_channel[slot] = decl.carry_channel;
    }

    for (uint32_t at = 0; at < p_declaration.size(); ++at) {
        const FieldDecl &decl = p_declaration[at];
        const int slot = out.fields.index_of(decl.key);
        const int channel = out.fields.index_of(decl.carry_channel);
        if (slot < 0 || decl.carry_channel == StringName() || channel < 0) {
            continue;
        }
        out.state_family[slot] = int(StateFamily::POSE);
        out.state_family[channel] = int(StateFamily::MOMENTUM);
        out.projection[slot] = channel;
        out.pose[slot] = 1;
    }

    for (uint32_t at = 0; at < p_declaration.size(); ++at) {
        const FieldDecl &decl = p_declaration[at];
        const int slot = out.fields.index_of(decl.key);
        if (slot < 0) {
            continue;
        }
        if (decl.converge_stiffness > 0.0) {
            out.converge_rate[slot] = decl.converge_stiffness;
        }
        if (decl.teleport_only) {
            out.withheld[slot] = 1;
        }
        if (decl.reconcile_only) {
            out.trigger_exclude[slot] = 1;
            out.vote_exclude[slot] = 1;
        }
        if (decl.epsilon_override >= 0.0) {
            out.epsilon[slot] = decl.epsilon_override;
        }
        if (decl.teleport_at >= 0.0) {
            out.teleport[slot] = decl.teleport_at;
            out.pose[slot] = 1;
        }
    }
    return out;
}

} // namespace netw::predict
