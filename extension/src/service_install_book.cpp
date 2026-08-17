#include "netw/service_install_book.hpp"

#include "godot/class_db.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace {

String nearest_named_class(Object *p_config) {
    if (p_config == nullptr) {
        return String("<null>");
    }
    Ref<Script> script = p_config->get_script();
    while (script.is_valid()) {
        const StringName global = script->get_global_name();
        if (global != StringName()) {
            return String(global);
        }
        script = script->get_base_script();
    }
    return String(p_config->get_class());
}

} // namespace

const NetwServiceInstallBook::Row *NetwServiceInstallBook::row_of(
    Object *p_config
) const {
    if (p_config == nullptr) {
        return nullptr;
    }
    Ref<Script> script = p_config->get_script();
    while (script.is_valid()) {
        const HashMap<StringName, Row>::ConstIterator found
            = rows.find(script->get_global_name());
        if (found) {
            return &found->value;
        }
        script = script->get_base_script();
    }
    return nullptr;
}

bool NetwServiceInstallBook::register_service(
    const StringName &p_config_class,
    const Callable &p_installer,
    const Callable &p_uninstaller
) {
    const HashMap<StringName, Row>::Iterator found = rows.find(p_config_class);
    if (found) {
        found->value.installer = p_installer;
        found->value.uninstaller = p_uninstaller;
        return true;
    }
    Row row;
    row.installer = p_installer;
    row.uninstaller = p_uninstaller;
    rows.insert(p_config_class, row);
    return false;
}

bool NetwServiceInstallBook::unregister_service(
    const StringName &p_config_class
) {
    return rows.erase(p_config_class);
}

bool NetwServiceInstallBook::has_service(const StringName &p_config_class
) const {
    return rows.has(p_config_class);
}

StringName NetwServiceInstallBook::class_of(Object *p_config) const {
    if (p_config == nullptr) {
        return StringName();
    }
    Ref<Script> script = p_config->get_script();
    while (script.is_valid()) {
        const StringName global = script->get_global_name();
        if (rows.has(global)) {
            return global;
        }
        script = script->get_base_script();
    }
    return StringName();
}

Error NetwServiceInstallBook::install(Object *p_config, Object *p_object) {
    const Row *row = row_of(p_config);
    if (row == nullptr) {
        NETW_WARN(
            sys::SESSION,
            "no service is registered for %s, so it installs nothing",
            nearest_named_class(p_config)
        );
        return ERR_INVALID_PARAMETER;
    }
    if (!row->installer.is_valid()) {
        NETW_WARN(
            sys::SESSION,
            "the service registered for %s has no live installer",
            nearest_named_class(p_config)
        );
        return ERR_UNCONFIGURED;
    }
    Array arguments;
    arguments.push_back(p_config);
    arguments.push_back(p_object);
    return Error(int(row->installer.callv(arguments)));
}

Error NetwServiceInstallBook::uninstall(Object *p_config, Object *p_object) {
    const Row *row = row_of(p_config);
    if (row == nullptr) {
        NETW_WARN(
            sys::SESSION,
            "no service is registered for %s, so it uninstalls nothing",
            nearest_named_class(p_config)
        );
        return ERR_INVALID_PARAMETER;
    }
    if (!row->uninstaller.is_valid()) {
        NETW_WARN(
            sys::SESSION,
            "the service registered for %s has no live uninstaller",
            nearest_named_class(p_config)
        );
        return ERR_UNCONFIGURED;
    }
    Array arguments;
    arguments.push_back(p_config);
    arguments.push_back(p_object);
    return Error(int(row->uninstaller.callv(arguments)));
}

int NetwServiceInstallBook::size() const {
    return int(rows.size());
}

void NetwServiceInstallBook::clear() {
    rows.clear();
}

void NetwServiceInstallBook::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD(
            "register_service",
            "config_class",
            "installer",
            "uninstaller"
        ),
        &NetwServiceInstallBook::register_service
    );
    ClassDB::bind_method(
        D_METHOD("unregister_service", "config_class"),
        &NetwServiceInstallBook::unregister_service
    );
    ClassDB::bind_method(
        D_METHOD("has_service", "config_class"),
        &NetwServiceInstallBook::has_service
    );
    ClassDB::bind_method(
        D_METHOD("class_of", "config"),
        &NetwServiceInstallBook::class_of
    );
    ClassDB::bind_method(
        D_METHOD("install", "config", "object"),
        &NetwServiceInstallBook::install
    );
    ClassDB::bind_method(
        D_METHOD("uninstall", "config", "object"),
        &NetwServiceInstallBook::uninstall
    );
    ClassDB::bind_method(D_METHOD("size"), &NetwServiceInstallBook::size);
    ClassDB::bind_method(D_METHOD("clear"), &NetwServiceInstallBook::clear);
}

} // namespace netw
