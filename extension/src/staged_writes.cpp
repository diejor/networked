#include "netw/staged_writes.hpp"

namespace netw {

using namespace godot;

bool StagedWrites::is_valid() const {
    return !keys.is_empty() && keys.size() == values.size();
}

Dictionary StagedWrites::header() const {
    Dictionary out;
    out[StringName("ordinal")] = ordinal;
    out[StringName("tick")] = tick;
    out[StringName("ack")] = ack;
    out[StringName("payload")] = row;
    out[StringName("whole")] = whole;
    out[StringName("samples")] = samples;
    return out;
}

} // namespace netw
