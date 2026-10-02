#pragma once

#if defined(NETW_GDEXTENSION)

#include <godot_cpp/core/gdextension_interface_loader.hpp>

#include "netw/store/validate.hpp"

namespace netw_test {

class AllocProbe {
    static GDExtensionInterfaceMemAlloc2 &kept_alloc() {
        static GDExtensionInterfaceMemAlloc2 kept = nullptr;
        return kept;
    }

    static GDExtensionInterfaceMemRealloc2 &kept_realloc() {
        static GDExtensionInterfaceMemRealloc2 kept = nullptr;
        return kept;
    }

    static void *counted_alloc(size_t p_bytes, GDExtensionBool p_pad) {
        netw::store::allocations_seen() += 1;
        return kept_alloc()(p_bytes, p_pad);
    }

    static void *counted_realloc(
        void *p_memory,
        size_t p_bytes,
        GDExtensionBool p_pad
    ) {
        netw::store::allocations_seen() += 1;
        return kept_realloc()(p_memory, p_bytes, p_pad);
    }

    static GDExtensionInterfaceDictionaryOperatorIndex &kept_index() {
        static GDExtensionInterfaceDictionaryOperatorIndex kept = nullptr;
        return kept;
    }

    static GDExtensionVariantPtr counted_index(
        GDExtensionTypePtr p_self,
        GDExtensionConstVariantPtr p_key
    ) {
        netw::store::dictionary_writes_seen() += 1;
        return kept_index()(p_self, p_key);
    }

    bool armed = false;

public:
    void arm() {
        if (armed) {
            return;
        }
        armed = true;
        kept_alloc() = godot::gdextension_interface::mem_alloc2;
        kept_realloc() = godot::gdextension_interface::mem_realloc2;
        kept_index() = godot::gdextension_interface::dictionary_operator_index;
        godot::gdextension_interface::mem_alloc2 = &counted_alloc;
        godot::gdextension_interface::mem_realloc2 = &counted_realloc;
        godot::gdextension_interface::dictionary_operator_index
            = &counted_index;
    }

    void disarm() {
        if (!armed) {
            return;
        }
        armed = false;
        godot::gdextension_interface::mem_alloc2 = kept_alloc();
        godot::gdextension_interface::mem_realloc2 = kept_realloc();
        godot::gdextension_interface::dictionary_operator_index = kept_index();
    }

    ~AllocProbe() {
        disarm();
    }
};

} // namespace netw_test

#endif
