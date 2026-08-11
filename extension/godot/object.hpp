#pragma once

#include "godot/variant.hpp"

#if defined(NETW_MODULE)
#include "core/object/object.h"

namespace godot {
using ::Object;
} // namespace godot
#elif defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/object.hpp>
#else
#error "Define NETW_MODULE or NETW_GDEXTENSION."
#endif

namespace netw::gd {

// The engine fills a PropertyInfo list through an out-parameter, godot-cpp
// returns an array of dictionaries. Both carry the same keys.
inline godot::Array property_list(const godot::Object *object) {
#if defined(NETW_MODULE)
    List<PropertyInfo> infos;
    object->get_property_list(&infos);
    godot::Array result;
    for (const PropertyInfo &info : infos) {
        result.push_back(godot::Dictionary(info));
    }
    return result;
#else
    return object->get_property_list();
#endif
}

/* Whether `object` answers for `name` as a property.
 *
 * The engine's `Object::get` reports it through a `bool *` out-parameter and
 * godot-cpp's does not, so the one spelling both tiers share is the property
 * list. It is the same question GDScript's `name in object` asks.
 */
inline bool has_property(
    const godot::Object *object,
    const godot::StringName &name
) {
    if (object == nullptr) {
        return false;
    }
#if defined(NETW_MODULE)
    bool valid = false;
    object->get(name, &valid);
    return valid;
#else
    const godot::Array infos = property_list(object);
    for (int at = 0; at < infos.size(); ++at) {
        const godot::Dictionary info = infos[at];
        if (godot::StringName(info["name"]) == name) {
            return true;
        }
    }
    return false;
#endif
}

} // namespace netw::gd
