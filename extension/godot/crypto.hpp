#pragma once

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

#if defined(NETW_MODULE)
#include "core/crypto/crypto.h"
namespace godot {
using ::Crypto;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/crypto.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

namespace netw::gd {

inline godot::PackedByteArray secure_random_bytes(int p_count) {
    godot::Ref<godot::Crypto> crypto;
#if defined(NETW_MODULE)
    crypto = godot::Ref<godot::Crypto>(godot::Crypto::create());
#else
    crypto.instantiate();
#endif
    return crypto.is_valid() ? crypto->generate_random_bytes(p_count)
                             : godot::PackedByteArray();
}

} // namespace netw::gd
