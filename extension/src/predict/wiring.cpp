#include "netw/predict/wiring.hpp"

#include "netw/call_args.hpp"
#include "netw/colors.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw::predict {

namespace {

bool quantizes(
    const Ref<NetwQuantize> &p_quantizer,
    int p_type,
    const Variant &p_value
) {
    return p_quantizer.is_valid()
        && p_quantizer->supports_type(static_cast<Variant::Type>(p_type))
        && p_quantizer->supports_type(p_value.get_type());
}

struct Selection {
    LocalVector<int> fields;
    PackedByteArray bytes;
    bool written = false;
};

Selection write_selected(
    const FieldCodec &p_codec,
    const Dictionary &p_payload
) {
    Selection out;
    for (int at = 0; at < p_codec.count(); ++at) {
        if (p_payload.has(p_codec.keys[at])) {
            out.fields.push_back(at);
        }
    }
    if (out.fields.is_empty()) {
        return out;
    }
    wire::WriteStream stream;
    uint64_t count = uint64_t(out.fields.size());
    if (!stream.varuint(count, 2)) {
        return out;
    }
    for (const int at : out.fields) {
        const Variant value = p_payload[p_codec.keys[at]];
        const Ref<NetwQuantize> &quantizer = p_codec.quantizers[at];
        bool quantized = quantizes(quantizer, p_codec.types[at], value);
        if (!stream.bool1(quantized)
            || !call_args::value_write(
                stream,
                value,
                quantized ? quantizer : Ref<NetwQuantize>()
            )) {
            return out;
        }
    }
    if (!stream.align_verify()) {
        return out;
    }
    out.bytes = stream.to_bytes();
    out.written = true;
    return out;
}

} // namespace

void FieldCodec::append(
    const StringName &p_key,
    const Ref<NetwQuantize> &p_quantizer,
    int p_type
) {
    keys.push_back(p_key);
    quantizers.push_back(p_quantizer);
    types.push_back(p_type);
}

void FieldCodec::clear() {
    keys.clear();
    quantizers.clear();
    types.clear();
}

PackedByteArray canonical_bytes(
    const FieldCodec &p_codec,
    const Dictionary &p_payload
) {
    NETW_ZONE_NC("predict canonical bytes", colors::PREDICTION);
    return write_selected(p_codec, p_payload).bytes;
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

Dictionary zero_row(const FieldCodec &p_codec) {
    Dictionary out;
    for (uint32_t at = 0; at < p_codec.keys.size(); ++at) {
        Variant zero;
        if (zero_of(p_codec.types[at], zero)) {
            out[p_codec.keys[at]] = zero;
        }
    }
    return out;
}

Dictionary canonicalize(
    const FieldCodec &p_codec,
    const Dictionary &p_payload
) {
    NETW_ZONE_NC("predict canonicalize", colors::PREDICTION);
    const Selection written = write_selected(p_codec, p_payload);
    Dictionary out = p_payload.duplicate();
    if (!written.written) {
        return out;
    }
    wire::ReadStream reader(written.bytes);
    uint64_t count = 0;
    if (!reader.varuint(count, 2)) {
        return out;
    }
    for (uint64_t at = 0; at < count; ++at) {
        bool quantized = false;
        const int field = written.fields[uint32_t(at)];
        Variant value;
        if (!reader.bool1(quantized)
            || !call_args::value_read(
                reader,
                static_cast<Variant::Type>(
                    quantized ? p_codec.types[field] : int(Variant::NIL)
                ),
                quantized ? p_codec.quantizers[field] : Ref<NetwQuantize>(),
                value
            )) {
            return p_payload.duplicate();
        }
        out[p_codec.keys[field]] = value;
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
                p_declaration[at].type
            );
        }
    }
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
