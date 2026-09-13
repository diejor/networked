#pragma once

#include "godot/script.hpp"
#include "godot/variant.hpp"
#include "netw/scene_decl.hpp"

namespace netw::script::registry {

enum MemberBook {
    MEMBER_RPC,
    MEMBER_PROPERTY,
    MEMBER_SIGNAL,
    MEMBER_SPAWN,
};

enum ScriptBook {
    SCRIPT_DESPAWN,
    SCRIPT_PERSISTENCE,
};

void declare_member(
    const godot::Ref<godot::Script> &p_script,
    MemberBook p_book,
    const godot::StringName &p_member,
    const godot::Variant &p_config
);

godot::Variant member_config(
    const godot::Ref<godot::Script> &p_script,
    MemberBook p_book,
    const godot::StringName &p_member
);

godot::Dictionary member_configs(
    const godot::Ref<godot::Script> &p_script,
    MemberBook p_book
);

void declare_script_config(
    const godot::Ref<godot::Script> &p_script,
    ScriptBook p_book,
    const godot::Variant &p_config
);

godot::Variant script_config(
    const godot::Ref<godot::Script> &p_script,
    ScriptBook p_book
);

godot::Variant own_script_config(
    const godot::Ref<godot::Script> &p_script,
    ScriptBook p_book
);

void declare_scene(
    const godot::Ref<godot::Script> &p_script,
    const SceneDecl &p_decl
);

SceneDecl scene_decl(const godot::Ref<godot::Script> &p_script);

} // namespace netw::script::registry
