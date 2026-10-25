#pragma once

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"

#if defined(NETW_MODULE)
#include "core/crypto/crypto.h"
#include "core/crypto/hashing_context.h"
namespace godot {
using ::Crypto;
using ::CryptoKey;
using ::HashingContext;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/crypto.hpp>
#include <godot_cpp/classes/crypto_key.hpp>
#include <godot_cpp/classes/hashing_context.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

namespace netw::gd {

inline godot::Ref<godot::Crypto> new_crypto() {
    godot::Ref<godot::Crypto> crypto;
#if defined(NETW_MODULE)
    crypto = godot::Ref<godot::Crypto>(godot::Crypto::create());
#else
    crypto.instantiate();
#endif
    return crypto;
}

inline godot::Ref<godot::CryptoKey> new_crypto_key() {
    godot::Ref<godot::CryptoKey> key;
#if defined(NETW_MODULE)
    key = godot::Ref<godot::CryptoKey>(godot::CryptoKey::create());
#else
    key.instantiate();
#endif
    return key;
}

inline godot::PackedByteArray secure_random_bytes(int p_count) {
    const godot::Ref<godot::Crypto> crypto = new_crypto();
    return crypto.is_valid() ? crypto->generate_random_bytes(p_count)
                             : godot::PackedByteArray();
}

inline godot::PackedByteArray rs256_sign(
    const godot::PackedByteArray &p_message_hash,
    const godot::Ref<godot::CryptoKey> &p_key
) {
    const godot::Ref<godot::Crypto> crypto = new_crypto();
    if (crypto.is_null() || p_key.is_null()) {
        return godot::PackedByteArray();
    }
    return crypto->sign(
        godot::HashingContext::HASH_SHA256, p_message_hash, p_key
    );
}

inline bool rs256_verify(
    const godot::PackedByteArray &p_message_hash,
    const godot::PackedByteArray &p_signature,
    const godot::Ref<godot::CryptoKey> &p_key
) {
    const godot::Ref<godot::Crypto> crypto = new_crypto();
    if (crypto.is_null() || p_key.is_null() || p_signature.is_empty()) {
        return false;
    }
    return crypto->verify(
        godot::HashingContext::HASH_SHA256, p_message_hash, p_signature,
        p_key
    );
}

inline godot::Ref<godot::CryptoKey> new_rsa_key(int p_bits) {
    const godot::Ref<godot::Crypto> crypto = new_crypto();
    return crypto.is_valid() ? crypto->generate_rsa(p_bits)
                             : godot::Ref<godot::CryptoKey>();
}

} // namespace netw::gd
