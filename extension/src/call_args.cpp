#include "netw/call_args.hpp"

#include <cstring>

#include "godot/utility.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/wire/frame.hpp"

using namespace godot;

namespace netw::call_args {

namespace {

int raw_type_of(const Variant &p_value) {
    switch (p_value.get_type()) {
        case Variant::BOOL:
            return RAW_BOOL;
        case Variant::INT:
            return RAW_INT;
        case Variant::VECTOR2:
            return RAW_VECTOR2;
        case Variant::FLOAT:
            return RAW_FLOAT;
        case Variant::VECTOR3:
            return RAW_VECTOR3;
        default:
            return RAW_FALLBACK;
    }
}

bool f32_write(wire::WriteStream &p_stream, double p_value) {
    const float narrowed = float(p_value);
    uint32_t staged = 0;
    std::memcpy(&staged, &narrowed, sizeof(float));
    uint64_t held = uint64_t(staged);
    return p_stream.bits(held, 32);
}

bool f32_read(wire::ReadStream &p_stream, double &r_value) {
    uint64_t staged = 0;
    if (!p_stream.bits(staged, 32)) {
        return false;
    }
    const uint32_t bits = uint32_t(staged);
    float held = 0.0f;
    std::memcpy(&held, &bits, sizeof(float));
    r_value = double(held);
    return true;
}

bool node_write(wire::WriteStream &p_stream, const NodeRef &p_node) {
    uint64_t route = uint64_t(MAX(p_node.route, int64_t(0)));
    uint64_t comp = uint64_t(p_node.comp) & 0xFF;
    if (!p_stream.varuint(route, 5) || !p_stream.bits(comp, 8)) {
        return false;
    }
    if (comp != wire::FRAME_COMP_PATH) {
        return true;
    }
    String path = p_node.path;
    return wire::string_field(p_stream, path);
}

bool node_read(wire::ReadStream &p_stream, NodeRef &r_node) {
    uint64_t route = 0;
    uint64_t comp = 0;
    if (!p_stream.varuint(route, 5) || !p_stream.bits(comp, 8)) {
        return false;
    }
    r_node.route = int64_t(route);
    r_node.comp = int64_t(comp);
    if (comp != wire::FRAME_COMP_PATH) {
        return true;
    }
    return wire::string_field(p_stream, r_node.path);
}

bool quantized_write(
    wire::WriteStream &p_stream,
    const Variant &p_value,
    const Ref<NetwQuantize> &p_quantizer
) {
    return p_quantizer->write(p_stream, p_value);
}

bool quantized_read(
    wire::ReadStream &p_stream,
    Variant::Type p_type,
    const Ref<NetwQuantize> &p_quantizer,
    Variant &r_value
) {
    return p_quantizer->read(p_stream, p_type, r_value);
}

bool slot_is_quantized(
    const Slot &p_slot,
    const Ref<NetwQuantize> &p_quantizer,
    Variant::Type p_declared
) {
    return p_quantizer.is_valid() && p_quantizer->supports_type(p_declared)
        && p_quantizer->supports_type(p_slot.value.get_type());
}

} // namespace

Slot of_value(const Variant &p_value) {
    Slot out;
    out.value = p_value;
    return out;
}

Slot of_node(int64_t p_route, int64_t p_comp, const String &p_path) {
    Slot out;
    out.addresses_node = true;
    out.node.route = p_route;
    out.node.comp = p_comp;
    out.node.path = p_path;
    return out;
}

Ref<NetwQuantize> quantizer_at(const Array &p_quantizers, int p_index) {
    if (p_index >= p_quantizers.size()
        || p_quantizers[p_index].get_type() == Variant::NIL) {
        return Ref<NetwQuantize>();
    }
    return p_quantizers[p_index];
}

int type_at(const Array &p_types, int p_index) {
    if (p_index >= p_types.size()
        || p_types[p_index].get_type() != Variant::INT) {
        return int(Variant::NIL);
    }
    return int(p_types[p_index]);
}

bool value_write(
    wire::WriteStream &p_stream,
    const Variant &p_value,
    const Ref<NetwQuantize> &p_quantizer
) {
    if (p_quantizer.is_valid()) {
        return quantized_write(p_stream, p_value, p_quantizer);
    }
    int64_t type = raw_type_of(p_value);
    if (!p_stream.int_range(type, RAW_FALLBACK, RAW_VECTOR3)) {
        return false;
    }
    switch (type) {
        case RAW_BOOL: {
            bool held = bool(p_value);
            return p_stream.bool1(held);
        }
        case RAW_INT: {
            uint64_t held = uint64_t(int64_t(p_value));
            return p_stream.bits(held, 64);
        }
        case RAW_VECTOR2: {
            const Vector2 held = p_value;
            return f32_write(p_stream, held.x) && f32_write(p_stream, held.y);
        }
        case RAW_FLOAT:
            return f32_write(p_stream, double(p_value));
        case RAW_VECTOR3: {
            const Vector3 held = p_value;
            return f32_write(p_stream, held.x) && f32_write(p_stream, held.y)
                && f32_write(p_stream, held.z);
        }
        default: {
            PackedByteArray bytes = gd::var_to_bytes(p_value);
            return p_stream.bytes_capped(bytes, FALLBACK_CAP);
        }
    }
}

bool value_read(
    wire::ReadStream &p_stream,
    Variant::Type p_type,
    const Ref<NetwQuantize> &p_quantizer,
    Variant &r_value
) {
    if (p_quantizer.is_valid()) {
        return quantized_read(p_stream, p_type, p_quantizer, r_value);
    }
    int64_t type = 0;
    if (!p_stream.int_range(type, RAW_FALLBACK, RAW_VECTOR3)) {
        return false;
    }
    switch (type) {
        case RAW_BOOL: {
            bool held = false;
            if (!p_stream.bool1(held)) {
                return false;
            }
            r_value = held;
            return true;
        }
        case RAW_INT: {
            uint64_t held = 0;
            if (!p_stream.bits(held, 64)) {
                return false;
            }
            r_value = int64_t(held);
            return true;
        }
        case RAW_VECTOR2: {
            double x = 0.0;
            double y = 0.0;
            if (!f32_read(p_stream, x) || !f32_read(p_stream, y)) {
                return false;
            }
            r_value = Vector2(x, y);
            return true;
        }
        case RAW_FLOAT: {
            double held = 0.0;
            if (!f32_read(p_stream, held)) {
                return false;
            }
            r_value = held;
            return true;
        }
        case RAW_VECTOR3: {
            double x = 0.0;
            double y = 0.0;
            double z = 0.0;
            if (!f32_read(p_stream, x) || !f32_read(p_stream, y)
                || !f32_read(p_stream, z)) {
                return false;
            }
            r_value = Vector3(x, y, z);
            return true;
        }
        default: {
            PackedByteArray bytes;
            if (!p_stream.bytes_capped(bytes, FALLBACK_CAP)) {
                return false;
            }
            r_value = gd::bytes_to_var(bytes);
            return true;
        }
    }
}

bool write(
    wire::WriteStream &p_stream,
    const LocalVector<Slot> &p_slots,
    const Array &p_quantizers,
    const Array &p_types
) {
    NETW_ZONE_NC("call args write", colors::CODEC);
    NETW_ZONE_VALUE(int64_t(p_slots.size()));
    uint64_t count = uint64_t(p_slots.size());
    if (!p_stream.varuint(count, COUNT_BYTES)) {
        return false;
    }
    for (uint32_t at = 0; at < p_slots.size(); ++at) {
        const Slot &slot = p_slots[at];
        const Ref<NetwQuantize> quantizer = quantizer_at(p_quantizers, int(at));
        const Variant::Type declared
            = static_cast<Variant::Type>(type_at(p_types, int(at)));
        const bool quantized = !slot.addresses_node
            && slot_is_quantized(slot, quantizer, declared);
        int64_t kind = slot.addresses_node
            ? KIND_NODE_REF
            : (quantized ? KIND_QUANTIZED : KIND_RAW);
        if (!p_stream.int_range(kind, KIND_RAW, KIND_NODE_REF)) {
            return false;
        }
        if (slot.addresses_node) {
            if (!node_write(p_stream, slot.node)) {
                return false;
            }
            continue;
        }
        const bool placed = value_write(
            p_stream,
            slot.value,
            quantized ? quantizer : Ref<NetwQuantize>()
        );
        if (!placed) {
            return false;
        }
    }
    return true;
}

bool read(
    wire::ReadStream &p_stream,
    const Array &p_quantizers,
    const Array &p_types,
    LocalVector<Slot> &r_slots
) {
    NETW_ZONE_NC("call args read", colors::CODEC);
    uint64_t count = 0;
    if (!p_stream.varuint(count, COUNT_BYTES)) {
        return false;
    }
    NETW_ZONE_VALUE(int64_t(count));
    LocalVector<Slot> staged;
    staged.reserve(uint32_t(count));
    for (uint64_t at = 0; at < count; ++at) {
        int64_t kind = 0;
        if (!p_stream.int_range(kind, KIND_RAW, KIND_NODE_REF)) {
            return false;
        }
        if (kind == KIND_NODE_REF) {
            Slot slot;
            slot.addresses_node = true;
            if (!node_read(p_stream, slot.node)) {
                return false;
            }
            staged.push_back(slot);
            continue;
        }
        Variant value;
        const bool taken = value_read(
            p_stream,
            static_cast<Variant::Type>(type_at(p_types, int(at))),
            kind == KIND_QUANTIZED ? quantizer_at(p_quantizers, int(at))
                                   : Ref<NetwQuantize>(),
            value
        );
        if (!taken) {
            return false;
        }
        staged.push_back(of_value(value));
    }
    r_slots = staged;
    return true;
}

bool values_write(
    wire::WriteStream &p_stream,
    const Array &p_values,
    const Array &p_quantizers,
    const Array &p_types
) {
    LocalVector<Slot> slots;
    slots.reserve(uint32_t(p_values.size()));
    for (int at = 0; at < p_values.size(); ++at) {
        const Variant value = p_values[at];
        if (value.get_type() == Variant::OBJECT) {
            NETW_ERROR(
                sys::CODEC,
                "a value row carries no Object, so value %d is dropped. A "
                "node argument is addressed by the call argument path, not "
                "by this carrier.",
                int(at)
            );
            slots.push_back(of_value(Variant()));
            continue;
        }
        slots.push_back(of_value(value));
    }
    return write(p_stream, slots, p_quantizers, p_types);
}

bool values_read(
    wire::ReadStream &p_stream,
    const Array &p_quantizers,
    const Array &p_types,
    Array &r_values
) {
    LocalVector<Slot> slots;
    if (!read(p_stream, p_quantizers, p_types, slots)) {
        return false;
    }
    Array out;
    for (uint32_t at = 0; at < slots.size(); ++at) {
        out.push_back(slots[at].value);
    }
    r_values = out;
    return true;
}

} // namespace netw::call_args
