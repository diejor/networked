class_name Networked
extends Object

const SETTING_PATH = "networked/config"
const DEFAULT_PATH = "res://networked_config.tres"

static var config: NetworkedConfig:
	get = get_config,
	set = warn_immutable

static func warn_immutable(value: NetworkedConfig) -> void:
	push_warning("trying to set a property that is immutable.")

static func get_config() -> NetworkedConfig:
	var setting: String = "networked/config"
	assert(ProjectSettings.has_setting(setting))
	var config_path: String = ProjectSettings.get_setting(setting)
	assert(ResourceLoader.exists(config_path), "Create a `%s` and \
configure `ProjectSettings/%s` to point to this resource." % ['NetworkedConfig', setting])
	var config: NetworkedConfig = load(config_path)
	return config
