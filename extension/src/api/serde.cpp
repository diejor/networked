#include "netw/api/serde.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

void Serde::_bind_methods() {
    ClassDB::bind_method(D_METHOD("serialize"), &Serde::serialize);
    ClassDB::bind_method(D_METHOD("deserialize", "bytes"), &Serde::deserialize);

    GDVIRTUAL_BIND(_serialize);
    GDVIRTUAL_BIND(_deserialize, "bytes");
}

PackedByteArray Serde::serialize() {
    PackedByteArray bytes;
    if (GDVIRTUAL_CALL(_serialize, bytes)) {
        return bytes;
    }
    return PackedByteArray();
}

void Serde::deserialize(const PackedByteArray &bytes) {
    GDVIRTUAL_CALL(_deserialize, bytes);
}

} // namespace netw
