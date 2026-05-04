## Orchestrates debug signals and visuals for a single [MultiplayerTree] instance.
##
## Owns the full lifecycle of all per-tree debug connections: scene events,
## per-scene synchronizer hooks, peer events, and visual decorations
## (nameplates).
## [br][br]
## State is isolated to this instance to support multi-window Embedded Server.
@tool
class_name NetDebugTreeContext
extends Node

## Emitted when the tree has finished initial wiring and is ready for debug tiling.
signal tree_ready

## Emitted when a local clock pong is captured.
signal clock_pong_captured(data: Dictionary)


const _NAMEPLATE_SCENE = "uid://dui4l6oylk8ju"

## The local player node for this tree.
var local_player: Node:
	get:
		var mt := _mt_ref.get_ref() as MultiplayerTree
		return mt.local_player if mt else null

var _mt_ref: WeakRef
var _reporter_ref: WeakRef

## Visualizer state.
## [br][br]
## [b]Key:[/b] [code]viz_name[/code] ([String])
## [br][br]
## [b]Value:[/b] [Dictionary] mapping [code]"stable_id"[/code] ([int] PeerID or
## [String] Path) to [bool].
## [br][br]
## A key of [code]""[/code] in the inner dictionary represents the tree-wide
## default.
var _visualizers: Dictionary = {}

## Scene -> [CheckpointToken] captured from the [code]scene_spawn[/code] span,
## used for causal linking.
var _scene_tokens: Dictionary = {}

## Scene -> [Callable] connected to [member Scene.synchronizer.spawned], for
## disconnect-on-cleanup.
var _hooked_scenes: Dictionary = {}

var _scene_wired: bool = false


func _init(mt: MultiplayerTree, reporter: NetworkedDebugReporter) -> void:
	_mt_ref = weakref(mt)
	_reporter_ref = weakref(reporter)


# ─── Public API ───────────────────────────────────────────────────────────────

## Returns [code]true[/code] if the visualizer is enabled for a specific node.
func is_enabled(viz: String, node: Node = null) -> bool:
	var states: Dictionary = _visualizers.get(viz, {})
	if node:
		var id := _get_stable_id(node)
		if states.has(id):
			return states[id]
	return states.get("", false)


## Updates visualizer state from an editor command.
func apply_command(d: Dictionary) -> void:
	var viz: String = d.get("viz_name", "")
	var enabled: bool = d.get("enabled", false)
	var path: String = d.get("node_path", "")
	var peer_id: int = d.get("peer_id", 0)

	if viz.is_empty():
		return

	var states: Dictionary = _visualizers.get(viz, {})
	
	var id: Variant
	if peer_id != 0:
		id = peer_id
	else:
		id = _get_stable_id_from_path(path) if not path.is_empty() else ""
		
	states[id] = enabled
	_visualizers[viz] = states

	_refresh_all()


## Builds a diagnostic snapshot for a crash manifest.
## [br][br]
## Prefers the span's explicit target node (if set via [method NetSpan.with_node]).
## Falls back to a "Session Snapshot" of the tree root, enriched with high-level
## state from the [MultiplayerSceneManager].
func build_crash_snapshot(span: NetSpan) -> NetNodeSnapshot:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	if not mt:
		return null
	
	# Priority 1: Explicit target node
	var target := span.get_target_node() if span else null
	if is_instance_valid(target):
		return NetNodeSnapshot.from_node(target)
	
	# Priority 2: Session fallback (Tree Root)
	var snap := NetNodeSnapshot.from_node(mt)
	var sm: MultiplayerSceneManager = mt.get_service(MultiplayerSceneManager)

	# Manually enrich the tree root's snapshot with service-level data.
	# This keeps the MultiplayerTree core clean while providing rich context.
	var session_state: Dictionary = {
		"is_server": mt.is_server,
		"peer_id": \
			mt.multiplayer_api.get_unique_id() if mt.multiplayer_api else 0,
		"connected_peers": \
			mt.multiplayer_api.get_peers() if mt.multiplayer_api else [],
		"active_scenes": \
			sm.active_scenes.keys() if sm else [],
		"backend": \
			mt.backend.get_script().get_global_name() if mt.backend else "None",
		"active_scene": get_active_scene_path(),
	}
	
	# Merge into debug_state
	for k in session_state:
		snap.debug_state[k] = session_state[k]
		
	return snap


## Robustly identifies the active scene file path for this tree or a specific
## context node.
func get_active_scene_path(context: Node = null) -> String:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	
	if is_instance_valid(context):
		# Priority: owner scene (e.g. level root for spawned players).
		if context.owner and not context.owner.scene_file_path.is_empty():
			return context.owner.scene_file_path
		
		# Context node might be the scene root itself.
		if not context.scene_file_path.is_empty():
			return context.scene_file_path
		
		# Tree fallback for context node.
		var tree := context.get_tree()
		if tree and tree.current_scene:
			return tree.current_scene.scene_file_path

	# Fallback to the MultiplayerTree's tree.
	if is_instance_valid(mt) and mt.is_inside_tree():
		var tree := mt.get_tree()
		if tree and tree.current_scene:
			return tree.current_scene.scene_file_path
			
	return "?"


# ─── Decoration Lifecycle ─────────────────────────────────────────────────────

func _refresh_all() -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	if not mt:
		return

	for player in _find_players(mt):
		_decorate_player(player)


func _decorate_player(player: Node) -> void:
	if not is_instance_valid(player):
		return
	var existing := player.get_node_or_null("NetDebugNameplate")
	var should_have := is_enabled("nameplate", player)

	if should_have and not existing:
		var client := (
			player.get_node_or_null("%SpawnerComponent")
			as SpawnerComponent
		)
		var username := ""
		if client:
			username = client.username
		else:
			username = player.name.get_slice("|", 0)
		if not username.is_empty():
			var nameplate: DebugClient = load(
				_NAMEPLATE_SCENE
			).instantiate()
			nameplate.name = "NetDebugNameplate"
			nameplate.follow_target(player, username)
			player.add_child(nameplate)
	elif not should_have and existing:
		existing.name = "Nameplate_Deleting"
		existing.queue_free()


# ─── Helpers ──────────────────────────────────────────────────────────────────

func _get_stable_id(node: Node) -> Variant:
	if not is_instance_valid(node):
		return ""
	var client := (
		node.get_node_or_null("%SpawnerComponent") as SpawnerComponent
	)
	if client:
		return node.get_multiplayer_authority()
	var parsed := JoinPayload.parse_authority(node.name)
	return parsed if parsed != 0 else str(node.get_path())


func _get_stable_id_from_path(path: String) -> Variant:
	var node := get_tree().root.get_node_or_null(path)
	return _get_stable_id(node) if is_instance_valid(node) else path


func _find_players(mt: MultiplayerTree) -> Array[Node]:
	return mt.get_all_players()


# ─── Signal Wiring ────────────────────────────────────────────────────────────

func _ready() -> void:
	var mt: MultiplayerTree = _mt_ref.get_ref()
	if not mt:
		return

	# Peer events: notify reporter (spans, topology) and refresh decoration.
	mt.peer_connected.connect(_on_mt_peer_connected)
	mt.peer_disconnected.connect(_on_mt_peer_disconnected)

	# Identity changes: notify reporter to re-emit session registration.
	mt.local_player_changed.connect(_on_local_player_changed)

	# Debug signal wiring for scene/clock requires configured state.
	mt.configured.connect(_on_configured)
	var sm: MultiplayerSceneManager = mt.get_service(MultiplayerSceneManager)
	if sm:
		_on_configured()


func _exit_tree() -> void:
	_disconnect_all()
	NetwServices.unregister(self, NetDebugTreeContext)
	for scene: MultiplayerScene in _hooked_scenes.keys():
		_unhook_synchronizer(scene)
	_hooked_scenes.clear()
	_scene_tokens.clear()


func _disconnect_all() -> void:
	var mt: MultiplayerTree = _mt_ref.get_ref()

	if mt:
		if mt.peer_connected.is_connected(_on_mt_peer_connected):
			mt.peer_connected.disconnect(_on_mt_peer_connected)
		if mt.peer_disconnected.is_connected(_on_mt_peer_disconnected):
			mt.peer_disconnected.disconnect(_on_mt_peer_disconnected)
		if mt.local_player_changed.is_connected(_on_local_player_changed):
			mt.local_player_changed.disconnect(_on_local_player_changed)
		if mt.configured.is_connected(_on_configured):
			mt.configured.disconnect(_on_configured)
		
		var clock: NetworkClock = mt.get_service(NetworkClock)
		if clock and clock.pong_received.is_connected(_on_clock_pong):
			clock.pong_received.disconnect(_on_clock_pong)
			
		var sm: MultiplayerSceneManager = mt.get_service(MultiplayerSceneManager)
		if is_instance_valid(sm):
			if sm.scene_spawned.is_connected(_on_scene_spawned):
				sm.scene_spawned.disconnect(_on_scene_spawned)
			if sm.scene_despawned.is_connected(_on_scene_despawned):
				sm.scene_despawned.disconnect(_on_scene_despawned)


func _on_mt_peer_connected(id: int) -> void:
	var mt: MultiplayerTree = _mt_ref.get_ref()
	var r := _reporter_ref.get_ref() as NetworkedDebugReporter
	if r and mt:
		r._on_peer_connected(id, mt)
	_refresh_all()


func _on_mt_peer_disconnected(id: int) -> void:
	var mt: MultiplayerTree = _mt_ref.get_ref()
	var r := _reporter_ref.get_ref() as NetworkedDebugReporter
	if r and mt:
		r._on_peer_disconnected(id, mt)
	_refresh_all()


func _on_configured() -> void:
	_refresh_all()
	var mt: MultiplayerTree = _mt_ref.get_ref()
	var reporter := _reporter_ref.get_ref() as NetworkedDebugReporter
	if not mt or not reporter or _scene_wired:
		return

	NetwServices.register(self, NetDebugTreeContext)
	_scene_wired = true

	var clock: NetworkClock = mt.get_service(NetworkClock)
	if clock:
		clock.pong_received.connect(_on_clock_pong)

	var sm: MultiplayerSceneManager = mt.get_service(MultiplayerSceneManager)
	if is_instance_valid(sm):
		sm.scene_spawned.connect(_on_scene_spawned)
		sm.scene_despawned.connect(_on_scene_despawned)

		# Retroactively hook scenes that spawned before this context was ready
		# (e.g. ON_STARTUP).
		for scene: MultiplayerScene in sm.active_scenes.values():
			if not is_instance_valid(scene) or _hooked_scenes.has(scene):
				continue
			_scene_tokens[scene] = null # no causal token
			_hook_synchronizer(scene)

		# Emit topology for players already present.
		for player in mt.get_all_players():
			reporter._on_player_spawned_logic(player, mt, null)
	else:
		# Sceneless mode: emit topology for all current players.
		for player in mt.get_all_players():
			reporter._on_player_spawned_logic(player, mt, null)
			
	tree_ready.emit.call_deferred()


# ─── Scene Lifecycle ────────────────────────────────────────────────────

func _on_scene_spawned(scene: MultiplayerScene) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var reporter := _reporter_ref.get_ref() as NetworkedDebugReporter
	if not mt or not reporter:
		return

	var token: CheckpointToken = reporter._on_scene_spawned_logic(scene, mt)
	_scene_tokens[scene] = token
	if is_instance_valid(scene):
		scene.set_meta(&"_net_scene_token", token)
	_hook_synchronizer(scene)


func _on_scene_despawned(scene: MultiplayerScene) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var reporter := _reporter_ref.get_ref() as NetworkedDebugReporter
	if mt and reporter:
		reporter._on_scene_despawned_logic(scene, mt)
	_unhook_synchronizer(scene)
	_scene_tokens.erase(scene)


func _hook_synchronizer(scene: MultiplayerScene) -> void:
	if not is_instance_valid(scene) or \
			not is_instance_valid(scene.synchronizer):
		return
	if _hooked_scenes.has(scene):
		return

	var cb := func(node: Node): _on_player_spawned(node, scene)
	scene.synchronizer.spawned.connect(cb)
	_hooked_scenes[scene] = cb


func _unhook_synchronizer(scene: MultiplayerScene) -> void:
	var cb: Callable = _hooked_scenes.get(scene, Callable())
	if cb.is_valid() and is_instance_valid(scene) and \
			is_instance_valid(scene.synchronizer):
		if scene.synchronizer.spawned.is_connected(cb):
			scene.synchronizer.spawned.disconnect(cb)
	_hooked_scenes.erase(scene)


func _on_player_spawned(player: Node, scene: MultiplayerScene) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var reporter := _reporter_ref.get_ref() as NetworkedDebugReporter
	if not mt or not reporter:
		return

	var token: CheckpointToken = _scene_tokens.get(scene, null)
	if not token and not scene:
		token = mt.get_sceneless_session_token()

	reporter._on_player_spawned_logic(player, mt, token)
	_decorate_player(player)


func _on_local_player_changed(_player: Node) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var reporter := _reporter_ref.get_ref() as NetworkedDebugReporter
	if mt and reporter:
		reporter.report_session_registered(mt)


func _on_clock_pong(data: Dictionary) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	if is_instance_valid(mt) and mt.local_player:
		var player := SpawnerComponent.unwrap(mt.local_player)
		if player:
			data["username"] = player.username
		else:
			data["username"] = mt.local_player.name.get_slice("|", 0)
	clock_pong_captured.emit(data)
