## Fluent builder for constructing [SceneReplicationConfig] programmatically.
##
## Hard-wrapped to 80 columns.
class_name SyncConfigBuilder
extends RefCounted

## Replication mode constant representing NEVER.
const NEVER = SceneReplicationConfig.REPLICATION_MODE_NEVER
## Replication mode constant representing ON_CHANGE.
const ON_CHANGE = SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE
## Replication mode constant representing ALWAYS.
const ALWAYS = SceneReplicationConfig.REPLICATION_MODE_ALWAYS

var _properties: Array[Dictionary] = []


## Adds a property configuration to the sync configuration.
##
## Returns the [SyncConfigBuilder] instance to support fluent chaining.
func property(
		path: String,
		spawn: bool = true,
		mode: int = ON_CHANGE,
		watch: bool = false,
		sync_flag: bool = false,
) -> SyncConfigBuilder:
	_properties.append(
		{
			"path": NodePath(path),
			"spawn": spawn,
			"mode": mode,
			"watch": watch,
			"sync": sync_flag,
		},
	)
	return self


## Builds and returns the configured [SceneReplicationConfig].
func build() -> SceneReplicationConfig:
	var cfg := SceneReplicationConfig.new()
	for prop in _properties:
		var np: NodePath = prop.path
		cfg.add_property(np)
		cfg.property_set_spawn(np, prop.spawn)
		cfg.property_set_replication_mode(np, prop.mode)
		cfg.property_set_watch(np, prop.watch)
		cfg.property_set_sync(np, prop.sync)
	return cfg
