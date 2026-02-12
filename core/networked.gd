class_name Networked
extends Object

static func get_config() -> NetworkedConfig:
	var setting: String = "networked/config"
	assert(ProjectSettings.has_setting(setting))
	var config_path: String = ProjectSettings.get_setting(setting)
	assert(FileAccess.file_exists(config_path), "Create a `%s` and \
configure `ProjectSettings/%s` to point to this resource." % ['NetworkedConfig', setting])
	var config: NetworkedConfig = load(config_path)
	return config
