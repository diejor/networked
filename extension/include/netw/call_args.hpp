#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/api/quantize.hpp"
#include "netw/wire/stream.hpp"

namespace netw::call_args {

enum RawType {
    RAW_FALLBACK = 0,
    RAW_BOOL = 1,
    RAW_INT = 2,
    RAW_VECTOR2 = 3,
    RAW_FLOAT = 4,
    RAW_VECTOR3 = 5,
};

enum SlotKind {
    KIND_RAW = 0,
    KIND_QUANTIZED = 1,
    KIND_NODE_REF = 2,
};

constexpr int FALLBACK_CAP = 65535;
constexpr int COUNT_BYTES = 2;

struct NodeRef {
    int64_t route = 0;
    int64_t comp = 0;
    godot::String path;
};

struct Slot {
    godot::Variant value;
    bool addresses_node = false;
    NodeRef node;
};

Slot of_value(const godot::Variant &value);

Slot of_node(int64_t route, int64_t comp, const godot::String &path);

bool value_write(
    wire::WriteStream &stream,
    const godot::Variant &value,
    const godot::Ref<NetwQuantize> &quantizer
);

bool value_read(
    wire::ReadStream &stream,
    godot::Variant::Type type,
    const godot::Ref<NetwQuantize> &quantizer,
    godot::Variant &r_value
);

bool write(
    wire::WriteStream &stream,
    const godot::LocalVector<Slot> &slots,
    const godot::Array &quantizers,
    const godot::Array &types
);

bool read(
    wire::ReadStream &stream,
    const godot::Array &quantizers,
    const godot::Array &types,
    godot::LocalVector<Slot> &r_slots
);

bool values_write(
    wire::WriteStream &stream,
    const godot::Array &values,
    const godot::Array &quantizers,
    const godot::Array &types
);

bool values_read(
    wire::ReadStream &stream,
    const godot::Array &quantizers,
    const godot::Array &types,
    godot::Array &r_values
);

godot::Ref<NetwQuantize> quantizer_at(
    const godot::Array &quantizers,
    int index
);

int type_at(const godot::Array &types, int index);

} // namespace netw::call_args
