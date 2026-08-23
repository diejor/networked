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
var _has_save: bool = false
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
var _has_input: bool = false
var _input_props: Array[StringName] = []
var _has_broadcast: bool = false
var _broadcast_props: Array[StringName] = []
var _has_prediction: bool = false
var _prediction_missing_policy: PredictionComponent.MissingInput = \
		PredictionComponent.MissingInput.STALL
var _prediction_epsilon: float = 0.01
var _prediction_schedule: PredictionComponent.Schedule = \
		PredictionComponent.Schedule.TICK

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


## Enables player-represented control on the [NetwEntity].
##
## Bakes [member NetwEntity.initial_controller] to
## [constant NetwEntity.INITIAL_REPRESENTED_PEER] the serialization-safe
## way: [method build] extends the [method with_root] type with a generated root
## script whose [code]_init()[/code] writes the archetype config, mirroring how a
## hand-authored player root configures itself. The write re-runs on every
## instantiate, so it survives [method PackedScene.pack].
func with_multiplayer_entity() -> PlayerBuilder:
	_has_entity = true
	return self


## Declares persistence on the player archetype, baking the table into the packed
## scene as metadata the [NetwPersistenceEngine] reads. The database is set per
## instance after spawn through
## [method NetwPersistenceEngine.meta_database].
func with_save(database: Resource, table: StringName) -> PlayerBuilder:
	_has_save = true
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


## Bakes root [param property] as a persistence column with an optional
## per-column snapshot [param interval] ([code]0.0[/code] inherits the archetype
## default). The server reads the live value at flush time, so a synced field is
## persistable exactly when the server sees it.
func with_save_property(
		property: StringName,
		interval: float = 0.0,
) -> PlayerBuilder:
	_save_properties.append(
		{
			"property": property,
			"interval": interval,
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


## Configures [member NetwEntity.interest] on the player entity.
##
## Bakes the specified interest [param layers] and observer reporting mode
## [param report_observers] into the generated root script, so packing and
## instantiation preserve the handle configuration without a marker node.
func with_interest(
		layers: Array[StringName] = [],
		report_observers: bool = false,
) -> PlayerBuilder:
	_has_interest = true
	_interest_layers = layers
	_interest_report = report_observers
	return self


## Declares that the entity root's script owns a derived state set covering
## [param props].
##
## The server-authored predicted state is the set a script marks with
## [method NetwScriptModel.PropertyConfig.state], so this attaches no node. It
## asserts at [method build] that the root script marks every one of [param props],
## the schema both peers derive from the shared script. Pair it with a
## [method with_root] whose script declares the marks.
func with_state(props: Array[StringName]) -> PlayerBuilder:
	_has_state = true
	_state_props = props
	return self


## Declares that the entity root's script owns a derived input set covering
## [param props].
##
## The controller-authored input is the set a script marks with
## [method NetwScriptModel.PropertyConfig.input], so this attaches no node. It
## asserts at [method build] that the root script marks every one of [param props].
## Authority follows the controller through the set's policy, evaluated per pump.
func with_input(props: Array[StringName]) -> PlayerBuilder:
	_has_input = true
	_input_props = props
	return self


## Declares that the entity root's script owns a derived broadcast set covering
## [param props].
##
## The controller-authored display stream is the set a script marks with
## [method NetwScriptModel.PropertyConfig.broadcast], so this attaches no node. It
## asserts at [method build] that the root script marks every one of [param props],
## and ensures the [NetwEntity] the set resolves through.
func with_broadcast(props: Array[StringName]) -> PlayerBuilder:
	_has_broadcast = true
	_broadcast_props = props
	return self


## Configures a [PredictionComponent] on the player entity.
##
## Attaches the prediction and reconciliation slot with [param missing_policy],
## [param epsilon] and [param schedule]. Requires [method with_state] and
## [method with_input], and an entity root that defines
## [code]_network_tick[/code] so [member PredictionComponent.simulate]
## auto-binds.
##
## [param schedule] is a build-time parameter rather than a write on the built
## node because [PredictionComponent] reads it once, when the entity resolves,
## and a rule declared for a tier the entity did not resolve under is accepted
## and then permanently retired on first use.
func with_prediction(
		missing_policy: PredictionComponent.MissingInput = \
		PredictionComponent.MissingInput.STALL,
		epsilon: float = 0.01,
		schedule: PredictionComponent.Schedule = \
		PredictionComponent.Schedule.TICK,
) -> PlayerBuilder:
	_has_prediction = true
	_prediction_missing_policy = missing_policy
	_prediction_epsilon = epsilon
	_prediction_schedule = schedule
	return self


# Asserts the entity-root script declares each of [param props] in its derived
# set of [param record], the marks [method Netw.configure_property] registers. The
# root script owns the marks, so a scriptless or unmarked root is a build error that
# names the missing declaration.
func _assert_root_declares(
		root: Node,
		record: int,
		props: Array[StringName],
		verb: String,
) -> void:
	var script := root.get_script() as Script
	assert(
		script != null,
		"PlayerBuilder.%s requires a root script declaring the marks through "
		% verb + "Netw.configure_property().",
	)
	var set := NetwPropertySet.from_script(script, record)
	assert(
		set != null,
		"PlayerBuilder.%s: the root script declares no matching set." % verb,
	)
	var keys := set.keys()
	for prop in props:
		assert(
			prop in keys,
			"PlayerBuilder.%s: the root script does not mark '%s'." % [verb, prop],
		)


# Generates the entity-root script that bakes the represented-peer archetype
# config into the packed scene. The script re-runs its _init on every instantiate,
# the serialization-safe home a marker node once stood in for. A scripted root's
# own _init is preserved with super(); a native root has no _init to chain.
func _entity_root_script() -> GDScript:
	var lines := PackedStringArray()
	var chains_super := _root_type is Script and _script_defines_init(_root_type)
	if _root_type is Script:
		var script_path: String = (_root_type as Script).resource_path
		assert(
			not script_path.is_empty(),
			"PlayerBuilder: with_multiplayer_entity needs a root script with a resource path.",
		)
		lines.append("extends \"%s\"" % script_path)
	else:
		var dummy: Object = _root_type.new()
		lines.append("extends %s" % dummy.get_class())
		dummy.free()
	lines.append("func _init() -> void:")
	if chains_super:
		lines.append("\tsuper()")
	if _has_entity:
		lines.append(
			"\tNetwEntity.resolve(self).initial_controller ="
			+ " NetwEntity.INITIAL_REPRESENTED_PEER",
		)
	if _has_interest:
		if _interest_layers.is_empty():
			lines.append("\tNetw.configure_interest(self)")
		else:
			lines.append("\tvar interest := Netw.configure_interest(self)")
			for layer_id in _interest_layers:
				lines.append(
					"\tinterest.layer(StringName(%s))"
					% var_to_str(String(layer_id)),
				)
		if _interest_report:
			lines.append(
				"\tNetwEntity.resolve(self).interest"
				+ "._set_report_observers(true)",
			)
	var script := GDScript.new()
	script.source_code = "\n".join(lines) + "\n"
	var err := script.reload()
	assert(err == OK, "PlayerBuilder: failed to generate the entity root script.")
	return script


# Whether [param script] declares its own _init, the only case where super()
# resolves. GDScript super() reaches the immediate base's own body, not an _init
# inherited from further up the chain, so an inherited-only _init reports false.
func _script_defines_init(script: Script) -> bool:
	for method in script.get_script_method_list():
		if method.name == "_init":
			return true
	return false


## Composes and returns a live player node tree.
func build() -> Node:
	var needs_root_config := _has_entity or _has_interest
	var root: Node = _entity_root_script().new() \
	if needs_root_config else _root_type.new()
	root.name = _name

	if _has_save:
		# Persistence is declared as metadata on the root, the pack-surviving
		# scriptless path. Only value-typed table and columns bake in; the live
		# database is set on the spawned instance after spawn.
		root.set_meta(
			NetwPersistenceEngine.meta_table(),
			_save_table,
		)
		var columns: Array = []
		for entry: Dictionary in _save_properties:
			columns.append(
				{
					"property": entry["property"],
					"interval": entry.get("interval", 0.0),
				},
			)
		root.set_meta(
			NetwPersistenceEngine.meta_columns(),
			columns,
		)

	if not _tp_level_scene_path.is_empty():
		var tp_comp := TPComponent.new()
		var snp: SceneNodePath = SceneNodePath.new()
		snp.scene_path = _tp_level_scene_path
		snp.node_path = _tp_spawner_node_path
		tp_comp.set("starting_scene_path", snp)
		var _a3: Node = SceneAssembly.attach(root, tp_comp, root)

	if _has_state or _has_input or _has_broadcast or _has_prediction:
		# The prediction component resolves NetwEntity.of in NOTIFICATION_PARENTED,
		# which fires on attach before tree entry, so the entity must exist on the
		# root before it attaches. Reuse the one with_multiplayer_entity() created,
		# else ensure it now.
		NetwEntity.ensure(root)

	if _has_state:
		_assert_root_declares(
			root,
			NetwPropertySet.Record.RECORD_STATE,
			_state_props,
			"with_state",
		)

	if _has_input:
		_assert_root_declares(
			root,
			NetwPropertySet.Record.RECORD_INPUT,
			_input_props,
			"with_input",
		)

	if _has_broadcast:
		_assert_root_declares(
			root,
			NetwPropertySet.Record.RECORD_BROADCAST,
			_broadcast_props,
			"with_broadcast",
		)

	if _has_prediction:
		var prediction := PredictionComponent.new()
		prediction.name = "PredictionComponent"
		prediction.missing_policy = _prediction_missing_policy
		prediction.divergence_epsilon = _prediction_epsilon
		prediction.schedule = _prediction_schedule
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
