## Server-authoritative [MultiplayerSynchronizer] that carries spawn-only
## state for an entity from the spawning server to remote clients.
##
## Walks the owning entity's sibling [MultiplayerSynchronizer]s, collects
## their replication-config properties, and re-adds them as spawn-only
## (mode [code]REPLICATION_MODE_NEVER[/code] with [code]spawn = true[/code]).
## Also adds a baseline [member SpawnerComponent.username] property when
## the parent component carries one and a [TPComponent.current_scene_path]
## property when present.
## [br][br]
## Used by [SpawnerComponent] for player spawning and by
## [EntityComponent] when [member EntityComponent.build_spawn_sync] is
## enabled.
class_name SpawnSynchronizer
extends MultiplayerSynchronizer

## The [Node] whose siblings supply spawn-only properties. Set at
## [code]_init[/code] time by the owning component.
var anchor_component: NetwComponent


func _init(component: NetwComponent) -> void:
	name = "SpawnSynchronizer"
	unique_name_in_owner = true
	visibility_update_mode = MultiplayerSynchronizer.VISIBILITY_PROCESS_NONE
	anchor_component = component
	component.add_child(self)
	owner = component
	root_path = get_path_to(component.owner)


## Builds a [SceneReplicationConfig] collecting spawn-only properties from
## all client synchronizers of [param target_node].
##
## Marks each property as spawn-only
## ([code]REPLICATION_MODE_NEVER[/code] with spawn enabled) so initial
## state transfers on spawn without ongoing delta replication.
## [br][br]
## [b]How spawn discovery works:[/b]
## [br]- [method SynchronizersCache.get_client_synchronizers] finds all
##   [MultiplayerSynchronizer] nodes whose root_path points to the entity.
## [br]- Each synchronizer's replication_config properties are added as
##   spawn-only.
## [br]- [SaveComponent] pivots its root_path to [code]"."[/code] after
##   baking, but spawn config paths were already resolved.
func config_spawn_properties(target_node: Node) -> void:
	Netw.dbg.trace(
		"Configuring spawn properties for %s", [target_node.name]
	)
	replication_config = SceneReplicationConfig.new()

	if target_node.owner:
		_add_optional_username_property(target_node)
		_add_optional_tp_property(target_node)

	var entity_root: Node = (
		target_node.owner if target_node is NetwComponent else target_node
	)
	var syncs := SynchronizersCache.get_client_synchronizers(entity_root)
	var sync_names := syncs.map(func(s): return s.name)
	Netw.dbg.debug(
		"Found %d synchronizers for spawn: [%s]",
		[syncs.size(), ", ".join(sync_names)]
	)

	for sync: MultiplayerSynchronizer in syncs:
		if sync == self or not sync.replication_config:
			continue

		var source: SceneReplicationConfig = sync.replication_config
		Netw.dbg.trace(
			"Adding %d properties from %s",
			[source.get_properties().size(), sync.name]
		)

		for property: NodePath in source.get_properties():
			if replication_config.has_property(property):
				continue
			_add_spawn_property(property)


func _add_optional_username_property(target_node: Node) -> void:
	var spawner := target_node as SpawnerComponent
	if not spawner:
		spawner = target_node.owner.get_node_or_null("%SpawnerComponent")
	if not spawner:
		return
	var comp_path := target_node.owner.get_path_to(spawner)
	var uname_path := NodePath(str(comp_path) + ":username")
	_add_spawn_property(uname_path)


func _add_optional_tp_property(target_node: Node) -> void:
	var tp := target_node.owner.get_node_or_null("%TPComponent")
	if not tp:
		return
	var tp_path := target_node.owner.get_path_to(tp)
	var scene_path := NodePath(str(tp_path) + ":current_scene_path")
	_add_spawn_property(scene_path)


func _add_spawn_property(property: NodePath) -> void:
	replication_config.add_property(property)
	replication_config.property_set_replication_mode(
		property, SceneReplicationConfig.REPLICATION_MODE_NEVER
	)
	replication_config.property_set_spawn(property, true)
	replication_config.property_set_sync(property, false)
	replication_config.property_set_watch(property, false)
