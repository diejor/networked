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
var _custom_synchronizers: Array[Dictionary] = []
var _save_properties: Array[Dictionary] = []

var _has_interest: bool = false
var _interest_layers: Array[StringName] = []
var _interest_report: bool = false

var _has_state: bool = false
var _state_props: Array[StringName] = []
var _state_bundled: bool = false
var _has_input: bool = false
var _input_props: Array[StringName] = []
var _has_prediction: bool = false
var _prediction_missing_policy: PredictionComponent.MissingInput = \
		PredictionComponent.MissingInput.STALL
var _prediction_epsilon: float = 0.01

static var _uid_counter: int = 0


# Initializes the player builder. Autogenerates a unique name when omitted.
func _init(p_player_name: String = "") -> void:
	if p_player_name.is_empty():
		_uid_counter += 1
		p_player_name = "AutogenPlayer_%d" % _uid_counter
	_name = p_player_name
	player_name = StringName(p_player_name)


## Resets the unique sequential name counter for deterministic tests.
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


## Pre-bakes root [param property] as a save-tracked property.
##
## The path [code]NodePath(".:" + property)[/code] is baked into the
## [SaveComponent] replication config before [method pack], so
## [method ProxySynchronizer.finalize] can process it without a post-spawn
## contribution call. [param save_mode] and [param interval] bake the per-property
## persistence trust and snapshot cadence into the component's declaration maps.
func with_save_property(
		property: StringName,
		save_mode: SaveComponent.SaveMode = SaveComponent.SaveMode.SNAPSHOT,
		interval: float = 0.0,
		spawn: bool = false,
		watch: bool = true,
) -> PlayerBuilder:
	_save_properties.append(
		{
			"property": property,
			"save_mode": save_mode,
			"interval": interval,
			"spawn": spawn,
			"watch": watch,
		},
	)
	return self


## Configures the [MultiplayerSynchronizer] (PlayerSync) with a sync config.
func with_player_sync(
		config_builder: SyncConfigBuilder,
) -> PlayerBuilder:
	_player_sync_config_builder = config_builder
	return self


## Configures a custom [MultiplayerSynchronizer] to be attached to the player.
##
## Places [param sync] under [param parent_path] relative to the player root.
## Uses [member Node.name] for the final node name. Intermediate nodes along
## [param parent_path] are created automatically when missing.
## [member MultiplayerSynchronizer.root_path] is resolved to the player root.
## [br][br]
## [codeblock]
## var sync := MultiplayerSynchronizer.new()
## sync.name = "ProxySync"
## var builder := PlayerBuilder.new()
## builder.with_synchronizer(sync, "Components")
## # Result: root/Components/ProxySync
## [/codeblock]
func with_synchronizer(
		sync: MultiplayerSynchronizer,
		parent_path: String = "",
) -> PlayerBuilder:
	_custom_synchronizers.append(
		{
			"synchronizer": sync,
			"parent_path": parent_path,
		},
	)
	return self


## Configures the [InterestComponent] on the player entity.
##
## Attaches an [InterestComponent] child node to the player, pre-configuring
## it with the specified interest [param layers] and observer reporting mode
## [param report_observers].
func with_interest(
		layers: Array[StringName] = [],
		report_observers: bool = false,
) -> PlayerBuilder:
	_has_interest = true
	_interest_layers = layers
	_interest_report = report_observers
	return self


## Configures a [StateSynchronizer] on the player entity.
##
## Attaches the server-authoritative state slot and registers each of
## [param props] as an ON_CHANGE payload property at [code].:prop[/code] on the
## entity root, so a predicting client compares against and a server records the
## same whole-entity snapshot. Set [param bundled] to ride the payload as one
## packed blob (the props are then suppressed to NEVER on the wire but stay
## resolvable through [method ProxySynchronizer.get_real_path]).
##
## The payload is baked into the synchronizer's [code]replication_config[/code]
## with real paths, so it survives [method pack] / instantiate, not just
## [method build].
func with_state(props: Array[StringName], bundled: bool = false) -> PlayerBuilder:
	_has_state = true
	_state_props = props
	_state_bundled = bundled
	return self


## Configures an [InputSynchronizer] on the player entity.
##
## Attaches the controller-authoritative input slot under an [code]Inputs[/code]
## child (mirroring bomber's [code]$Inputs[/code]) and registers each of
## [param props] as an ALWAYS payload property at [code].:prop[/code] on the
## entity root. Authority binds to the controller through the entity lifecycle.
func with_input(props: Array[StringName]) -> PlayerBuilder:
	_has_input = true
	_input_props = props
	return self


## Configures a [PredictionComponent] on the player entity.
##
## Attaches the prediction and reconciliation slot with [param missing_policy]
## and [param epsilon]. Requires [method with_state] and [method with_input], and
## an entity root that defines [code]_network_tick[/code] so
## [member PredictionComponent.simulate] auto-binds.
func with_prediction(
		missing_policy: PredictionComponent.MissingInput = \
		PredictionComponent.MissingInput.STALL,
		epsilon: float = 0.01,
) -> PlayerBuilder:
	_has_prediction = true
	_prediction_missing_policy = missing_policy
	_prediction_epsilon = epsilon
	return self


# Builds a real-path [SceneReplicationConfig] for [param props] at [code].:prop[/code]
# on the entity root. Baking real paths (rather than relying on register_property's
# in-memory _properties map, which is not serialized) is what lets a payload
# survive pack()/instantiate: finalize -> _import_from_config rebuilds the virtual
# map from this config on every peer.
func _payload_config(
		props: Array[StringName],
		mode: SceneReplicationConfig.ReplicationMode,
		watch: bool,
) -> SceneReplicationConfig:
	var cfg := SceneReplicationConfig.new()
	for prop in props:
		var path := NodePath(".:" + prop)
		cfg.add_property(path)
		cfg.property_set_replication_mode(path, mode)
		cfg.property_set_spawn(path, false)
		cfg.property_set_watch(path, watch)
	return cfg


## Composes and returns a live player node tree.
func build() -> Node:
	var root: Node = _root_type.new()
	root.name = _name

	if _has_entity:
		var entity := MultiplayerEntity.new()
		entity.initial_controller = \
		MultiplayerEntity.InitialController.REPRESENTED_PEER
		entity.set_meta("_custom_type_script", "uid://bspawnrcomp001")
		var _a1: Node = SceneAssembly.attach(root, entity, root)

	if _save_database != null:
		var save_comp := SaveComponent.new()
		save_comp.set("database", _save_database)
		save_comp.set("table_name", _save_table)
		var cfg := SceneReplicationConfig.new()
		for entry: Dictionary in _save_properties:
			var prop: StringName = entry["property"]
			var path := NodePath(".:" + prop)
			cfg.add_property(path)
			cfg.property_set_replication_mode(
				path,
				SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
			)
			cfg.property_set_spawn(path, entry.get("spawn", false))
			cfg.property_set_watch(path, entry.get("watch", true))
			save_comp._save_modes[prop] = entry.get(
				"save_mode", SaveComponent.SaveMode.SNAPSHOT,
			)
			save_comp._save_intervals[prop] = entry.get("interval", 0.0)
		save_comp.replication_config = cfg
		var _a2: Node = SceneAssembly.attach(root, save_comp, root)
		save_comp.root_path = save_comp.get_path_to(root)

	if not _tp_level_scene_path.is_empty():
		var tp_comp := TPComponent.new()
		var snp: SceneNodePath = SceneNodePath.new()
		snp.scene_path = _tp_level_scene_path
		snp.node_path = _tp_spawner_node_path
		tp_comp.set("starting_scene_path", snp)
		var _a3: Node = SceneAssembly.attach(root, tp_comp, root)

	if _has_interest:
		var interest_comp := InterestComponent.new()
		interest_comp.name = "InterestComponent"
		interest_comp.layer_ids = _interest_layers
		interest_comp.report_observers = _interest_report
		var _a5: Node = SceneAssembly.attach(root, interest_comp, root)

	if _has_state or _has_input or _has_prediction:
		# The state and input synchronizers resolve NetwEntity.of in
		# NOTIFICATION_PARENTED, which fires on attach before tree entry, so the
		# entity must exist on the root before any lag-comp component attaches.
		# Reuse the one with_multiplayer_entity() created, else ensure it now.
		NetwEntity.ensure(root)

	if _has_state:
		var state := StateSynchronizer.new()
		state.name = "StateSync"
		state.bundle_payload = _state_bundled
		# register_property populates _properties for the build() path (callers
		# inspect it pre-tree). The baked replication_config carries the same real
		# paths so finalize -> _import_from_config reconstructs the payload on the
		# pack()/instantiate path too, where _properties is not serialized.
		for prop in _state_props:
			state.register_property(
				prop,
				NodePath(".:" + prop),
				SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
				false,
				true,
			)
		state.replication_config = _payload_config(
			_state_props,
			SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
			true,
		)
		var _a6: Node = SceneAssembly.attach(root, state, root)
		state.root_path = state.get_path_to(root)

	if _has_input:
		var inputs := Node.new()
		inputs.name = "Inputs"
		var _a7: Node = SceneAssembly.attach(root, inputs, root)
		var input := InputSynchronizer.new()
		input.name = "InputSync"
		# Same dual registration as state: _properties for build(), baked config
		# for pack().
		for prop in _input_props:
			input.register_property(
				prop,
				NodePath(".:" + prop),
				SceneReplicationConfig.REPLICATION_MODE_ALWAYS,
				false,
				false,
			)
		input.replication_config = _payload_config(
			_input_props,
			SceneReplicationConfig.REPLICATION_MODE_ALWAYS,
			false,
		)
		var _a8: Node = SceneAssembly.attach(inputs, input, root)
		input.root_path = input.get_path_to(root)

	if _has_prediction:
		var prediction := PredictionComponent.new()
		prediction.name = "PredictionComponent"
		prediction.missing_policy = _prediction_missing_policy
		prediction.divergence_epsilon = _prediction_epsilon
		var _a9: Node = SceneAssembly.attach(root, prediction, root)

	var player_sync: MultiplayerSynchronizer = MultiplayerSynchronizer.new()
	player_sync.name = "PlayerSync"
	var sync_cfg: SceneReplicationConfig
	if _player_sync_config_builder != null:
		sync_cfg = _player_sync_config_builder.build()
	else:
		sync_cfg = SceneReplicationConfig.new()
	player_sync.replication_config = sync_cfg
	var _a4: Node = SceneAssembly.attach(root, player_sync, root)
	player_sync.root_path = player_sync.get_path_to(root)

	for entry in _custom_synchronizers:
		var sync: MultiplayerSynchronizer = entry["synchronizer"]
		var parent_path: String = entry["parent_path"]

		var parent: Node = root
		if not parent_path.is_empty():
			var path_node := NodePath(parent_path)
			for i in range(path_node.get_name_count()):
				var part_name: String = path_node.get_name(i)
				var child := parent.get_node_or_null(part_name)
				if child == null:
					child = Node.new()
					child.name = part_name
					SceneAssembly.attach(parent, child, root)
				parent = child

		assert(parent != null, "PlayerBuilder: parent_path not found: " + parent_path)
		if sync.get_parent() != null:
			sync.owner = null
			sync.get_parent().remove_child(sync)
		SceneAssembly.attach(parent, sync, root)
		sync.root_path = sync.get_path_to(root)

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
