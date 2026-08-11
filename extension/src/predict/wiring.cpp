#include "netw/predict/wiring.hpp"

#include "netw/bit_buffer.hpp"
#include "netw/codec.hpp"
#include "netw/colors.hpp"
#include "netw/profile.hpp"

namespace netw {

namespace predict {

namespace {

// The flag byte NetwScriptModel.write_values leads every value with. A native
// canonicalization that spelled the layout differently would produce bytes no
// peer decoding the frame could match.
constexpr int64_t FLAG_RAW = 0;
constexpr int64_t FLAG_QUANTIZED = 1;

bool quantizes(
    const Ref<NetwQuantize> &p_quantizer,
    int p_type,
    const Variant &p_value
) {
    return p_quantizer.is_valid() && p_quantizer->supports_type(p_type)
        && p_quantizer->supports_type(int(p_value.get_type()));
}

struct Selection {
    LocalVector<int> fields;
    Ref<NetwBitBufferWriter> writer;
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
    out.writer.instantiate();
    out.writer->put_aligned_u8(int64_t(out.fields.size()));
    for (const int at : out.fields) {
        const Variant value = p_payload[p_codec.keys[at]];
        const Ref<NetwQuantize> &quantizer = p_codec.quantizers[at];
        const bool quantized
            = quantizes(quantizer, p_codec.types[at], value);
        out.writer->put_aligned_u8(quantized ? FLAG_QUANTIZED : FLAG_RAW);
        NetwCodec::encode_value(
            out.writer,
            value,
            quantized ? quantizer : Ref<NetwQuantize>()
        );
    }
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
    const Selection written = write_selected(p_codec, p_payload);
    if (written.writer.is_null()) {
        return PackedByteArray();
    }
    return written.writer->to_bytes();
}

Dictionary canonicalize(
    const FieldCodec &p_codec,
    const Dictionary &p_payload
) {
    NETW_ZONE_NC("predict canonicalize", colors::PREDICTION);
    const Selection written = write_selected(p_codec, p_payload);
    Dictionary out = p_payload.duplicate();
    if (written.writer.is_null()) {
        return out;
    }
    const Ref<NetwBitBufferReader> reader
        = NetwBitBufferReader::create(written.writer->to_bytes());
    const int64_t count = reader->get_aligned_u8();
    for (int64_t at = 0; at < count; ++at) {
        const int64_t flag = reader->get_aligned_u8();
        const int field = written.fields[uint32_t(at)];
        const bool quantized = flag == FLAG_QUANTIZED;
        out[p_codec.keys[field]] = NetwCodec::decode_value(
            reader,
            quantized ? p_codec.types[field] : int(Variant::NIL),
            quantized ? p_codec.quantizers[field] : Ref<NetwQuantize>()
        );
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
        // The class is the declaration that a value is not an antecedent, so
        // it is also the declaration that its disagreement is not evidence of
        // a fork. Both halves of the vote's exclusion are written from their
        // own source, so the set the vote reads is never one another reader
        // depends on.
        out.vote_exclude[slot] = causal ? 0 : 1;
        out.angle[slot] = decl.angle ? 1 : 0;
    }

    for (uint32_t at = 0; at < p_declaration.size(); ++at) {
        const FieldDecl &decl = p_declaration[at];
        const int slot = out.fields.index_of(decl.key);
        const int channel = out.fields.index_of(decl.carry_channel);
        // A channel this set does not carry cannot be read at the same tick as
        // the field it advances, so the pair is not one the engine can honour.
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
        // A declared tier distance is also an enrolment: the field is saying
        // what its own error means, which is only answerable if the tier
        // measurement reads it.
        if (decl.teleport_at >= 0.0) {
            out.teleport[slot] = decl.teleport_at;
            out.pose[slot] = 1;
        }
    }
    return out;
}

} // namespace predict


} // namespace netw
