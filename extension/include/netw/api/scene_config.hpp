#pragma once

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"
#include "netw/scene_decl.hpp"

namespace netw {

class NetwSceneConfig : public godot::RefCounted {
    GDCLASS(NetwSceneConfig, godot::RefCounted)

private:
    godot::ObjectID script_id;
    godot::ObjectID node_id;

    SceneDecl held() const;
    void write(const SceneDecl &p_decl);

protected:
    static void _bind_methods();

public:
    void declare_against(
        godot::Node *p_node,
        const godot::Ref<godot::Script> &p_script
    );

    godot::StringName get_label() const {
        return held().label;
    }
    int get_isolation() const {
        return held().isolation;
    }

    godot::Ref<NetwSceneConfig> labeled(const godot::StringName &p_stem);
    godot::Ref<NetwSceneConfig> isolated();
};

} // namespace netw
