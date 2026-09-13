#include "netw/wire/describe.hpp"

using namespace godot;

namespace netw::wire::detail {

const char *spec_kind_name(SpecKind kind) {
    switch (kind) {
        case SpecKind::BITS:
            return "bits";
        case SpecKind::INT_RANGE:
            return "int_range";
        case SpecKind::VARUINT:
            return "varuint";
        case SpecKind::SVARINT:
            return "svarint";
        case SpecKind::BOOL1:
            return "bool1";
        case SpecKind::BYTES_CAPPED:
            return "bytes_capped";
        case SpecKind::STRING:
            return "string";
    }
    return "unknown";
}

Dictionary dump_field(const char *name, const Spec &spec) {
    Dictionary out;
    out["name"] = String(name);
    out["kind"] = String(spec_kind_name(spec.kind));
    switch (spec.kind) {
        case SpecKind::BITS:
            out["width"] = spec.low;
            break;
        case SpecKind::INT_RANGE:
            out["low"] = spec.low;
            out["high"] = spec.high;
            out["width"]
                = int64_t(bits_required(uint64_t(spec.high - spec.low)));
            break;
        case SpecKind::VARUINT:
        case SpecKind::SVARINT:
            out["max_bytes"] = spec.low;
            break;
        case SpecKind::BOOL1:
            out["width"] = int64_t(1);
            break;
        case SpecKind::BYTES_CAPPED:
        case SpecKind::STRING:
            out["cap"] = spec.low;
            break;
    }
    return out;
}

} // namespace netw::wire::detail
