#include "godot/class_db.hpp"

#include "godot/object.hpp"

#if defined(NETW_GDEXTENSION)
#include <godot_cpp/classes/class_db_singleton.hpp>
#endif

using namespace godot;

namespace netw::gd {

Variant instantiate_class(const StringName &p_class) {
#if defined(NETW_MODULE)
    Object *made = ::ClassDB::instantiate(p_class);
    if (made == nullptr) {
        return Variant();
    }
    RefCounted *counted = Object::cast_to<RefCounted>(made);
    if (counted != nullptr) {
        return Variant(Ref<RefCounted>(counted));
    }
    return Variant(made);
#else
    ClassDBSingleton *registry = ClassDBSingleton::get_singleton();
    return registry == nullptr ? Variant() : registry->instantiate(p_class);
#endif
}

} // namespace netw::gd
