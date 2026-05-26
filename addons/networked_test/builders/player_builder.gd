## Fluent builder for programmatically composing player entity scenes.
##
## Hard-wrapped to 80 columns.
class_name PlayerBuilder
extends Object

const SceneAssembly := preload(
	"res://addons/networked_test/builders/scene_assembly.gd"
)
const SyncConfigBuilder := preload(
	"res://addons/networked_test/builders/sync_config_builder.gd"
)
const NetwPathNamespace := preload(
	"res://addons/networked_test/builders/path_namespace.gd"
)

var _name: String
var _root: Node = null
var _has_spawner: bool = false
var _save_database: Resource = null
var _save_table: StringName = &""
var _tp_level_scene_path: String = ""
var _tp_spawner_node_path: String = ""
var _player_sync_config_builder: SyncConfigBuilder = null


# Initializes the player builder with the given entity name.
func _init(player_name: String, base_node: Node = null) -> void:
	_name = player_name
	_root = base_node


## Enables the [SpawnerComponent] on the player.
func with_spawner() -> PlayerBuilder:
	_has_spawner = true
	return self


## Configures the [SaveComponent] on the player entity.
func with_save(database: Resource, table: StringName) -> PlayerBuilder:
	_save_database = database
	_save_table = table
	return self


## Configures the [TPComponent] on the player entity.
func with_tp(
	level_scene_path: String,
	spawner_node_path: String
) -> PlayerBuilder:
	_tp_level_scene_path = level_scene_path
	_tp_spawner_node_path = spawner_node_path
	return self


## Configures the [MultiplayerSynchronizer] (PlayerSync) with a sync config.
func with_player_sync(
	config_builder: SyncConfigBuilder
) -> PlayerBuilder:
	_player_sync_config_builder = config_builder
	return self


## Composes and returns a live player node tree.
func build() -> Node:
	var root: Node = _root.duplicate() if _root != null else Node2D.new()
	root.name = _name
	
	if _has_spawner:
		var spawner := SpawnerComponent.new()
		spawner.set("authority_mode", SpawnerComponent.AuthorityMode.CLIENT)
		spawner.set_meta("_custom_type_script", "uid://bspawnrcomp001")
		var _a1: Node = SceneAssembly.attach(root, spawner, root)
		
	if _save_database != null:
		var save_comp := SaveComponent.new()
		save_comp.set("database", _save_database)
		save_comp.set("table_name", _save_table)
		var save_cfg: SceneReplicationConfig = SceneReplicationConfig.new()
		var pos_path: NodePath = NodePath("..:position")
		save_cfg.add_property(pos_path)
		save_cfg.property_set_spawn(pos_path, true)
		save_cfg.property_set_replication_mode(
			pos_path,
			SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE
		)
		save_comp.replication_config = save_cfg
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
	var packed: PackedScene = SceneAssembly.pack_with_path(root, path)
	NetwPathNamespace.register_resource(packed)
	root.free()
	return packed
