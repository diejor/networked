## Game-side debug telemetry reporter for the Networked debugger plugin.
##
## This is a singleton (Autoload) node that collects telemetry from all active
## [MultiplayerTree] instances in the process and forwards them to the editor.
##
## All operations are guarded by [method _should_report] — zero overhead in
## exported builds or headless/test runs.
extends Node

## Slow-poll interval for component heartbeats and lobby snapshots.
const HEARTBEAT_INTERVAL := 0.5  # 2 Hz

# Cached once per process: whether reporting is fundamentally allowed at all.
static var _reporting_enabled: bool = false
static var _reporting_checked: bool = false

# Tracks whether the EngineDebugger capture has been registered for this process.
static var _capture_registered: bool = false

var _trees: Array[MultiplayerTree] = []
var _message_queue: Array = []
var _flush_pending: bool = false
var _heartbeat_timer: float = 0.0

# Tree Name -> NodePath -> Array of {sync, callable} for demand-driven replication watch.
var _watched: Dictionary = {}


func _enter_tree() -> void:
	if not _should_report():
		return

	# Register the global message capture exactly once.
	if not _capture_registered:
		_capture_registered = true
		EngineDebugger.register_message_capture(
			"networked",
			func(message: String, data: Array) -> bool:
				_on_editor_message(message, data)
				return true
		)


func _exit_tree() -> void:
	if not _should_report():
		return
	for mt in _trees:
		_queue("networked:session_unregistered", {"tree_name": mt.name})
	_flush_now()


func _physics_process(delta: float) -> void:
	if not _should_report() or _trees.is_empty():
		return

	_heartbeat_timer += delta
	if _heartbeat_timer >= HEARTBEAT_INTERVAL:
		_heartbeat_timer = 0.0
		for mt in _trees:
			_send_lobby_snapshot(mt)
			_send_component_heartbeats(mt)


## Registers a [MultiplayerTree] for debug reporting.
func register_tree(mt: MultiplayerTree) -> void:
	if not _should_report():
		return
	if mt in _trees:
		return

	_trees.append(mt)

	mt.peer_connected.connect(_on_peer_connected.bind(mt))
	mt.peer_disconnected.connect(_on_peer_disconnected.bind(mt))
	mt.configured.connect(_on_configured.bind(mt))

	var backend_class := ""
	if mt.backend and mt.backend.get_script():
		backend_class = mt.backend.get_script().get_global_name()

	_queue("networked:session_registered", {
		"tree_name": mt.name,
		"is_server": mt.is_server,
		"backend_class": backend_class,
	})


## Unregisters a [MultiplayerTree] from debug reporting.
func unregister_tree(mt: MultiplayerTree) -> void:
	if mt not in _trees:
		return

	_trees.erase(mt)

	if mt.peer_connected.is_connected(_on_peer_connected):
		mt.peer_connected.disconnect(_on_peer_connected)
	if mt.peer_disconnected.is_connected(_on_peer_disconnected):
		mt.peer_disconnected.disconnect(_on_peer_disconnected)
	if mt.configured.is_connected(_on_configured):
		mt.configured.disconnect(_on_configured)

	if mt.clock and mt.clock.pong_received.is_connected(_on_clock_pong):
		mt.clock.pong_received.disconnect(_on_clock_pong)
	if mt.lobby_manager:
		if mt.lobby_manager.lobby_spawned.is_connected(_on_lobby_spawned):
			mt.lobby_manager.lobby_spawned.disconnect(_on_lobby_spawned)
		if mt.lobby_manager.lobby_despawned.is_connected(_on_lobby_despawned):
			mt.lobby_manager.lobby_despawned.disconnect(_on_lobby_despawned)

	_watched.erase(mt.name)
	_queue("networked:session_unregistered", {"tree_name": mt.name})
	_flush_now()


# ─── Signal Handlers ──────────────────────────────────────────────────────────

func _on_configured(mt: MultiplayerTree) -> void:
	if not _should_report():
		return
	if mt.clock:
		mt.clock.pong_received.connect(_on_clock_pong.bind(mt))
	if mt.lobby_manager:
		mt.lobby_manager.lobby_spawned.connect(_on_lobby_spawned.bind(mt))
		mt.lobby_manager.lobby_despawned.connect(_on_lobby_despawned.bind(mt))


func _on_clock_pong(data: Dictionary, mt: MultiplayerTree) -> void:
	data["tree_name"] = mt.name
	_queue("networked:clock_sample", data)


func _on_peer_connected(peer_id: int, mt: MultiplayerTree) -> void:
	_queue("networked:peer_connected", {"tree_name": mt.name, "peer_id": peer_id})


func _on_peer_disconnected(peer_id: int, mt: MultiplayerTree) -> void:
	_queue("networked:peer_disconnected", {"tree_name": mt.name, "peer_id": peer_id})


func _on_lobby_spawned(lobby: Lobby, mt: MultiplayerTree) -> void:
	if not is_instance_valid(lobby) or not is_instance_valid(lobby.level):
		return
	_queue("networked:lobby_event", {
		"tree_name": mt.name,
		"event": "spawned",
		"lobby_name": str(lobby.level.name),
	})


func _on_lobby_despawned(lobby: Lobby, mt: MultiplayerTree) -> void:
	if not is_instance_valid(lobby) or not is_instance_valid(lobby.level):
		return
	_queue("networked:lobby_event", {
		"tree_name": mt.name,
		"event": "despawned",
		"lobby_name": str(lobby.level.name),
	})


# ─── Slow-Poll (2 Hz) ─────────────────────────────────────────────────────────

func _send_lobby_snapshot(mt: MultiplayerTree) -> void:
	if not mt.lobby_manager:
		return
	var lobbies_data: Array = []
	for lobby_name: StringName in mt.lobby_manager.active_lobbies:
		var lobby: Lobby = mt.lobby_manager.active_lobbies[lobby_name]
		if not is_instance_valid(lobby) or not is_instance_valid(lobby.level):
			continue
		var peers: Array[int] = []
		for peer_id: int in lobby.synchronizer.connected_clients:
			peers.append(peer_id)
		lobbies_data.append({
			"name": str(lobby_name),
			"peer_count": peers.size(),
			"connected_clients": peers,
			"process_mode": int(lobby.level.process_mode),
		})
	_queue("networked:lobby_snapshot", {
		"tree_name": mt.name,
		"lobbies": lobbies_data,
	})


func _send_component_heartbeats(mt: MultiplayerTree) -> void:
	if not mt.lobby_manager:
		return
	for lobby_name: StringName in mt.lobby_manager.active_lobbies:
		var lobby: Lobby = mt.lobby_manager.active_lobbies[lobby_name]
		if not is_instance_valid(lobby) or not is_instance_valid(lobby.level):
			continue
		for player: Node in lobby.synchronizer.tracked_nodes.keys():
			if is_instance_valid(player):
				_send_player_heartbeat(player, mt)


func _send_player_heartbeat(player: Node, mt: MultiplayerTree) -> void:
	var components: Dictionary = {}

	var client: ClientComponent = player.get_node_or_null("%ClientComponent")
	if client:
		components["ClientComponent"] = {
			"username": client.username,
			"authority_mode": int(client.authority_mode),
			"is_multiplayer_authority": client.is_multiplayer_authority(),
		}

	var tp: TPComponent = player.get_node_or_null("%TPComponent")
	if tp:
		components["TPComponent"] = {
			"current_scene_name": tp.current_scene_name,
			"current_scene_path": tp.current_scene_path,
		}

	var save: SaveComponent = player.get_node_or_null("%SaveComponent")
	if save:
		components["SaveComponent"] = {
			"database": save.database.resource_path if save.database else "null",
			"table_name": save.table_name,
		}

	var tis := player.find_children("*", "TickInterpolator", true, false)
	if not tis.is_empty():
		var ti: TickInterpolator = tis[0]
		components["TickInterpolator"] = {
			"display_lag": ti.display_lag,
			"starvation_ticks": ti.starvation_ticks,
			"smart_dilation": ti.enable_smart_dilation,
		}

	if components.is_empty():
		return

	_queue("networked:component_heartbeat", {
		"tree_name": mt.name,
		"player_name": player.name,
		"components": components,
	})


# ─── Demand-Driven Replication Watch ──────────────────────────────────────────

func _handle_watch_node(d: Dictionary) -> void:
	var tree_name: String = d.get("tree_name", "")
	var mt: MultiplayerTree = null
	for t in _trees:
		if t.name == tree_name:
			mt = t
			break
	if not mt:
		return

	var np := NodePath(d.get("node_path", ""))
	if np.is_empty():
		return

	var tree_watched: Dictionary = _watched.get(tree_name, {})
	if np in tree_watched:
		return

	var node: Node = get_tree().root.get_node_or_null(np)
	if not is_instance_valid(node):
		return

	var hooked: Array = []
	for sync: MultiplayerSynchronizer in SynchronizersCache.get_synchronizers(node):
		var cb := func() -> void: _send_replication_snapshot(node, sync, mt)
		sync.delta_synchronized.connect(cb)
		sync.synchronized.connect(cb)
		hooked.append({"sync": sync, "cb": cb})

	tree_watched[np] = hooked
	_watched[tree_name] = tree_watched
	_send_full_replication_snapshot(node, mt)


func _handle_unwatch_node(d: Dictionary) -> void:
	var tree_name: String = d.get("tree_name", "")
	if tree_name not in _watched:
		return

	var np := NodePath(d.get("node_path", ""))
	var tree_watched: Dictionary = _watched[tree_name]
	if np not in tree_watched:
		return

	for entry in tree_watched[np]:
		var sync: MultiplayerSynchronizer = entry["sync"]
		var cb: Callable = entry["cb"]
		if is_instance_valid(sync):
			if sync.delta_synchronized.is_connected(cb):
				sync.delta_synchronized.disconnect(cb)
			if sync.synchronized.is_connected(cb):
				sync.synchronized.disconnect(cb)

	tree_watched.erase(np)
	if tree_watched.is_empty():
		_watched.erase(tree_name)


func _send_replication_snapshot(node: Node, sync: MultiplayerSynchronizer, mt: MultiplayerTree) -> void:
	if not is_instance_valid(node) or not is_instance_valid(sync):
		return
	_queue("networked:replication_snapshot", {
		"tree_name": mt.name,
		"node_path": str(node.get_path()),
		"properties": _collect_properties(node, sync),
	})


func _send_full_replication_snapshot(node: Node, mt: MultiplayerTree) -> void:
	var all_props: Dictionary = {}
	var inventory: Array = []
	for sync: MultiplayerSynchronizer in SynchronizersCache.get_synchronizers(node):
		all_props.merge(_collect_properties(node, sync))
		inventory.append({
			"name": sync.name,
			"authority": sync.get_multiplayer_authority(),
			"root_path": str(sync.root_path),
		})
	_queue("networked:replication_snapshot", {
		"tree_name": mt.name,
		"node_path": str(node.get_path()),
		"properties": all_props,
		"inventory": inventory,
	})


func _collect_properties(node: Node, sync: MultiplayerSynchronizer) -> Dictionary:
	var props: Dictionary = {}
	if not sync.replication_config:
		return props
	var root_node: Node = (sync.get_node(sync.root_path)
		if sync.root_path != NodePath(".") else sync.get_parent())
	if not is_instance_valid(root_node):
		return props
	for prop_path: NodePath in sync.replication_config.get_properties():
		var s := str(prop_path)
		var colon := s.rfind(":")
		if colon < 0:
			continue
		var node_part := s.substr(0, colon)
		var prop_name := s.substr(colon + 1)
		var target: Node = (root_node if node_part.is_empty() or node_part == "."
			else root_node.get_node_or_null(node_part))
		if is_instance_valid(target):
			var val: Variant = target.get(prop_name)
			if typeof(val) not in [TYPE_OBJECT, TYPE_RID, TYPE_CALLABLE, TYPE_SIGNAL]:
				props[s] = val
	return props


# ─── Incoming Editor Messages ─────────────────────────────────────────────────

func _on_editor_message(message: String, data: Array) -> void:
	if data.is_empty():
		return
	match message:
		"networked:watch_node":   _handle_watch_node(data[0])
		"networked:unwatch_node": _handle_unwatch_node(data[0])


# ─── Message Queue ────────────────────────────────────────────────────────────

func _queue(msg_name: String, data: Dictionary) -> void:
	_message_queue.append([msg_name, data])
	if not _flush_pending:
		_flush_pending = true
		call_deferred("_flush_queue")


func _flush_queue() -> void:
	_flush_pending = false
	_flush_now()


func _flush_now() -> void:
	if not _should_report():
		_message_queue.clear()
		return
	for entry: Array in _message_queue:
		EngineDebugger.send_message(entry[0], [entry[1]])
	_message_queue.clear()


# ─── Guard ────────────────────────────────────────────────────────────────────

static func _should_report() -> bool:
	if not _reporting_checked:
		_reporting_checked = true
		_reporting_enabled = true
		for arg in OS.get_cmdline_args():
			if arg in ["--gdunit", "--headless"]:
				_reporting_enabled = false
				break
	return _reporting_enabled and EngineDebugger.is_active()
