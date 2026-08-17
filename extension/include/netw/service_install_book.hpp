#pragma once

#include "godot/callable.hpp"
#include "godot/object.hpp"
#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"

namespace netw {

class NetwServiceInstallBook : public godot::RefCounted {
    GDCLASS(NetwServiceInstallBook, godot::RefCounted)

private:
    struct Row {
        godot::Callable installer;
        godot::Callable uninstaller;
    };

    godot::HashMap<godot::StringName, Row> rows;

    const Row *row_of(godot::Object *p_config) const;

protected:
    static void _bind_methods();

public:
    bool register_service(
        const godot::StringName &p_config_class,
        const godot::Callable &p_installer,
        const godot::Callable &p_uninstaller
    );

    bool unregister_service(const godot::StringName &p_config_class);

    bool has_service(const godot::StringName &p_config_class) const;

    godot::StringName class_of(godot::Object *p_config) const;

    godot::Error install(godot::Object *p_config, godot::Object *p_object);

    godot::Error uninstall(godot::Object *p_config, godot::Object *p_object);

    int size() const;

    void clear();
};

} // namespace netw
