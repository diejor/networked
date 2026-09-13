#pragma once

#include <cstdint>

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"
#include "netw/api/member_config.hpp"
#include "netw/api/property_set.hpp"

namespace netw::property_set_builder {

godot::Ref<NetwPropertySet> for_record(int64_t p_record);

godot::Ref<NetwPropertySet> from_property_config(
    const godot::StringName &p_property,
    const godot::Ref<NetwMemberConfig> &p_config
);

godot::Ref<NetwPropertySet> from_property_configs(
    const godot::Dictionary &p_configs,
    int64_t p_record
);

godot::Ref<NetwPropertySet> from_script(
    const godot::Ref<godot::Script> &p_script,
    int64_t p_record,
    godot::Object *p_api,
    godot::Node *p_node
);

} // namespace netw::property_set_builder
