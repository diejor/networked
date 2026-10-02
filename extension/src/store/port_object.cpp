#include "netw/store/port.hpp"

using namespace godot;

namespace netw::store {

Variant object_value(Object *p_node, const StringName &p_name) {
    return p_node != nullptr ? p_node->get(p_name) : Variant();
}

int push_rank(const Column &p_column) {
    switch (p_column.body) {
        case BodyKey::LINEAR:
        case BodyKey::ANGULAR:
            return 1;
        case BodyKey::SLEEPING:
            return 2;
        default:
            return 0;
    }
}

} // namespace netw::store
