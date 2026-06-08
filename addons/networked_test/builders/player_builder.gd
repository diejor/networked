## Fluent builder for programmatically composing player entity scenes.
class_name PlayerBuilder
extends RefCounted

## The unique name identifier for this player builder.
var player_name: StringName
## The resource path assigned to the packed scene.
var resource_path: String = ""
## The compiled [PackedScene] after calling [method pack].
var packed: PackedScene = null

var _name: String
var _root_type: Variant = Node
var _has_entity: bool = false
var _save_database: Resource = null
var _save_table: StringName = &""
var _tp_level_scene_path: String = ""
var _tp_spawner_node_path: String = ""
var _player_sync_config_builder: SyncConfigBuilder = null

static var _uid_counter: int = 0


# Initializes the player builder. If no name is provided, a unique sequential name is auto-generated.
func _init(p_player_name: String = "") -> void:
	if p_player_name.is_empty():
		_uid_counter += 1
		p_player_name = "AutogenPlayer_%d" % _uid_counter
	_name = p_player_name
	player_name = StringName(p_player_name)


## Resets the unique sequential name counter. Used in test teardown for determinism.
static func reset_counter() -> void:
	_uid_counter = 0


## Configures the custom root node class or script type.
func with_root(type: Variant) -> PlayerBuilder:
	var dummy = type.new()
	assert(dummy is Node, "PlayerBuilder: root type must inherit from Node.")
	dummy.free()
	_root_type = type
	return self


## Enables the [MultiplayerEntity] on the player.
func with_multiplayer_entity() -> PlayerBuilder:
	_has_entity = true
	return self


## Configures the [SaveComponent] on the player entity.
func with_save(database: Resource, table: StringName) -> PlayerBuilder:
	_save_database = database
	_save_table = table
	return self


## Configures the [TPComponent] on the player entity.
func with_tp(
		level_scene_path: String,
		spawner_node_path: String,
) -> PlayerBuilder:
	_tp_level_scene_path = level_scene_path
	_tp_spawner_node_path = spawner_node_path
	return self


## Configures the [MultiplayerSynchronizer] (PlayerSync) with a sync config.
func with_player_sync(
		config_builder: SyncConfigBuilder,
) -> PlayerBuilder:
	_player_sync_config_builder = config_builder
	return self


## Composes and returns a live player node tree.
func build() -> Node:
	var root: Node = _root_type.new()
	root.name = _name

	if _has_entity:
		var entity := MultiplayerEntity.new()
		entity.set("authority_mode", MultiplayerEntity.AuthorityMode.CLIENT)
		entity.set_meta("_custom_type_script", "uid://bspawnrcomp001")
		var _a1: Node = SceneAssembly.attach(root, entity, root)

	if _save_database != null:
		var save_comp := SaveComponent.new()
		save_comp.set("database", _save_database)
		save_comp.set("table_name", _save_table)
		save_comp.replication_config = SceneReplicationConfig.new()
		var _a2: Node = SceneAssembly.attach(root, save_comp, root)

	if not _tp_level_scene_path.is_empty():
		var tp_comp := TPComponent.new()
		var snp: SceneNodePath = SceneNodePath.new()
		snp.scene_path = _tp_level_scene_path
		snp.node_path = _tp_spawner_node_path
		tp_comp.set("starting_scene_path", snp)
		var _a3: Node = SceneAssembly.attach(root, tp_comp, root)

	var player_sync: MultiplayerSynchronizer = MultiplayerSynchronizer.new()
	player_sync.name = "PlayerSync"
	var sync_cfg: SceneReplicationConfig
	if _player_sync_config_builder != null:
		sync_cfg = _player_sync_config_builder.build()
	else:
		sync_cfg = SceneReplicationConfig.new()
	player_sync.replication_config = sync_cfg
	var _a4: Node = SceneAssembly.attach(root, player_sync, root)

	return root


## Composes, packs, and returns a [PackedScene] registered in memory.
func pack(custom_path: String = "") -> PackedScene:
	var root: Node = build()
	var path: String = custom_path if not custom_path.is_empty() else \
	NetwPathNamespace.next_path("player", _name)
	var p: PackedScene = SceneAssembly.pack_with_path(root, path)
	NetwPathNamespace.register_resource(p)
	root.free()
	packed = p
	resource_path = path
	return p
