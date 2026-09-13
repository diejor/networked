#include "netw/api/scene_config.hpp"

#include "godot/class_db.hpp"
#include "godot/node.hpp"
#include "netw/api/context.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/log.hpp"
#include "netw/scene_core.hpp"
#include "netw/script/model.hpp"

using namespace godot;

namespace netw {

void NetwSceneConfig::declare_against(
    Node *p_node,
    const Ref<Script> &p_script
) {
    node_id = gd::instance_id(p_node);
    script_id = gd::instance_id(p_script.ptr());
}

SceneDecl NetwSceneConfig::held() const {
    return netw::script::model::get_scene_decl(
        Ref<Script>(Object::cast_to<Script>(gd::object_of(script_id)))
    );
}

void NetwSceneConfig::write(const SceneDecl &p_decl) {
    const Ref<Script> script
        = Ref<Script>(Object::cast_to<Script>(gd::object_of(script_id)));
    if (script.is_null()) {
        return;
    }
    netw::script::model::declare_scene(script, p_decl);
}

Ref<NetwSceneConfig> NetwSceneConfig::labeled(const StringName &p_stem) {
    NETW_ERR_COND_V(
        p_stem == StringName(),
        Ref<NetwSceneConfig>(this),
        sys::SCENE,
        "NetwSceneConfig.labeled: an empty stem names no scene"
    );
    SceneDecl decl = held();
    decl.label = p_stem;
    write(decl);
    return Ref<NetwSceneConfig>(this);
}

Ref<NetwSceneConfig> NetwSceneConfig::isolated() {
    SceneDecl decl = held();
    decl.isolation = NetwSceneCore::ISOLATION_OWN_WORLD;
    write(decl);
    return Ref<NetwSceneConfig>(this);
}

void NetwSceneConfig::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("labeled", "stem"),
        &NetwSceneConfig::labeled
    );
    ClassDB::bind_method(D_METHOD("isolated"), &NetwSceneConfig::isolated);
    ClassDB::bind_method(D_METHOD("get_label"), &NetwSceneConfig::get_label);
    ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "label"), "", "get_label");
    ClassDB::bind_method(
        D_METHOD("get_isolation"),
        &NetwSceneConfig::get_isolation
    );
    ADD_PROPERTY(PropertyInfo(Variant::INT, "isolation"), "", "get_isolation");
}

} // namespace netw
