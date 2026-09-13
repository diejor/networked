#include "netw/script/registry.hpp"

namespace netw::script::registry {

using namespace godot;

namespace {

StringName member_book_key(MemberBook p_book) {
    switch (p_book) {
        case MEMBER_RPC:
            return StringName("netw_rpc_configs");
        case MEMBER_PROPERTY:
            return StringName("netw_property_configs");
        case MEMBER_SIGNAL:
            return StringName("netw_signal_configs");
        case MEMBER_SPAWN:
            return StringName("netw_spawn_configs");
    }
    return StringName();
}

StringName script_book_key(ScriptBook p_book) {
    switch (p_book) {
        case SCRIPT_DESPAWN:
            return StringName("netw_despawn_config");
        case SCRIPT_PERSISTENCE:
            return StringName("netw_persistence_config");
    }
    return StringName();
}

StringName scene_label_key() {
    return StringName("netw_scene_label");
}

StringName scene_isolation_key() {
    return StringName("netw_scene_isolation");
}

bool holds(const Ref<Script> &p_script, const StringName &p_key) {
    return p_script.is_valid() && p_script->has_meta(p_key);
}

} // namespace

void declare_member(
    const Ref<Script> &p_script,
    MemberBook p_book,
    const StringName &p_member,
    const Variant &p_config
) {
    if (p_script.is_null() || p_member == StringName()) {
        return;
    }
    const StringName key = member_book_key(p_book);
    Dictionary book = p_script->has_meta(key)
        ? Dictionary(p_script->get_meta(key))
        : Dictionary();
    book[p_member] = p_config;
    p_script->set_meta(key, book);
}

Variant member_config(
    const Ref<Script> &p_script,
    MemberBook p_book,
    const StringName &p_member
) {
    const StringName key = member_book_key(p_book);
    if (!holds(p_script, key)) {
        return Variant();
    }
    const Dictionary book = p_script->get_meta(key);
    return book.has(p_member) ? book[p_member] : Variant();
}

Dictionary member_configs(const Ref<Script> &p_script, MemberBook p_book) {
    const StringName key = member_book_key(p_book);
    if (!holds(p_script, key)) {
        return Dictionary();
    }
    return Dictionary(p_script->get_meta(key));
}

void declare_script_config(
    const Ref<Script> &p_script,
    ScriptBook p_book,
    const Variant &p_config
) {
    if (p_script.is_null()) {
        return;
    }
    p_script->set_meta(script_book_key(p_book), p_config);
}

Variant script_config(const Ref<Script> &p_script, ScriptBook p_book) {
    const StringName key = script_book_key(p_book);
    Ref<Script> walked = p_script;
    while (walked.is_valid()) {
        if (walked->has_meta(key)) {
            return walked->get_meta(key);
        }
        walked = walked->get_base_script();
    }
    return Variant();
}

Variant own_script_config(const Ref<Script> &p_script, ScriptBook p_book) {
    const StringName key = script_book_key(p_book);
    if (!holds(p_script, key)) {
        return Variant();
    }
    return p_script->get_meta(key);
}

void declare_scene(const Ref<Script> &p_script, const SceneDecl &p_decl) {
    if (p_script.is_null()) {
        return;
    }
    p_script->set_meta(scene_label_key(), p_decl.label);
    p_script->set_meta(scene_isolation_key(), p_decl.isolation);
}

SceneDecl scene_decl(const Ref<Script> &p_script) {
    SceneDecl decl;
    if (!holds(p_script, scene_label_key())) {
        return decl;
    }
    decl.declared = true;
    decl.label = p_script->get_meta(scene_label_key());
    decl.isolation = p_script->get_meta(scene_isolation_key());
    return decl;
}

} // namespace netw::script::registry
