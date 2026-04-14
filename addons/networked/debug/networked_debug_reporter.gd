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

# MultiplayerSpawner -> Callable — tracks which spawners have native confirmation hooks.
var _hooked_spawners: Dictionary = {}

# LobbySynchronizer -> Callable — tracks player-spawn race detection hooks.
var _hooked_lobby_syncs: Dictionary = {}

var _watchdog: ErrorWatchdog

var _cid_stack: Array[StringName] = []

# Whether to call EngineDebugger.debug() after sending a crash manifest.
# Toggled from the editor via the "Break on Manifest" button.
var _auto_break: bool = false

# Telemetry ring buffer — records one entry per flush cycle.
var _telemetry: NetTelemetryBuffer

# Peer events and component events accumulated during the current flush cycle.
var _cycle_peer_events: Array = []
var _cycle_component_events: Array = []
# Last known lobby snapshots keyed by tree_name (updated by _send_lobby_snapshot).
var _last_lobby_snapshots: Dictionary = {}


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

	var capacity: int = ProjectSettings.get_setting(
		"debug/networked/telemetry_buffer_size", 120)
	_telemetry = NetTelemetryBuffer.new(capacity)

	_watchdog = ErrorWatchdog.new()
	add_child(_watchdog)
	_watchdog.cpp_error_caught.connect(_on_cpp_error_caught)


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


func _on_cpp_error_caught(timestamp: int, error_text: String) -> void:
	if not _should_report():
		return

	# Emit crash manifest for the watchdog event so the editor panel shows it.
	EngineDebugger.send_message("networked:crash_manifest", [{
		"cid": str(_cid_stack[0]) if not _cid_stack.is_empty() else "N/A",
		"cid_timeline": _cid_stack.map(func(s: StringName) -> String: return str(s)),
		"trigger": "C++ ERROR / LOG WATCHDOG",
		"frame": Engine.get_process_frames(),
		"timestamp_usec": timestamp,
		"active_scene": get_tree().current_scene.scene_file_path if get_tree() and get_tree().current_scene else "?",
		"error_text": error_text,
		"telemetry_slice": _freeze_and_slice(),
	}])
	_maybe_break()


## Pushes a new Correlation ID onto the breadcrumb stack for diagnostic timeline.
func push_cid(cid: StringName) -> void:
	if cid.is_empty():
		return
	if not _cid_stack.is_empty() and _cid_stack[0] == cid:
		return
	
	_cid_stack.push_front(cid)
	
	var max_size: int = ProjectSettings.get_setting("debug/networked/cid_stack_size", 5)
	if _cid_stack.size() > max_size:
		_cid_stack.pop_back()


func _get_rel_path(node: Node, mt: MultiplayerTree) -> String:
	if not is_instance_valid(node) or not is_instance_valid(mt):
		return "?"
	var tree_root := mt.get_path()
	var node_path := node.get_path()
	var s_root := str(tree_root)
	var s_node := str(node_path)
	
	if s_node.begins_with(s_root):
		var rel := s_node.trim_prefix(s_root)
		if rel.begins_with("/"):
			rel = rel.substr(1)
		return rel
	return s_node


func _on_clock_pong(data: Dictionary, mt: MultiplayerTree) -> void:
	data["tree_name"] = mt.name
	_queue("networked:clock_sample", data)


func _on_peer_connected(peer_id: int, mt: MultiplayerTree) -> void:
	var ev := {"tree_name": mt.name, "peer_id": peer_id, "event": "connected"}
	_cycle_peer_events.append(ev)
	_queue("networked:peer_connected", {"tree_name": mt.name, "peer_id": peer_id})
	if mt.is_server:
		_check_simplify_path_race_on_connect(peer_id, mt)


func _on_peer_disconnected(peer_id: int, mt: MultiplayerTree) -> void:
	var ev := {"tree_name": mt.name, "peer_id": peer_id, "event": "disconnected"}
	_cycle_peer_events.append(ev)
	_queue("networked:peer_disconnected", {"tree_name": mt.name, "peer_id": peer_id})


func _on_lobby_spawned(lobby: Lobby, mt: MultiplayerTree) -> void:
	if not is_instance_valid(lobby) or not is_instance_valid(lobby.level):
		return
	_queue("networked:lobby_event", {
		"tree_name": mt.name,
		"event": "spawned",
		"lobby_name": str(lobby.level.name),
	})
	_hook_spawners_in(lobby.level, mt)
	_check_simplify_path_race_lobby(lobby, mt)
	if mt.is_server and is_instance_valid(lobby.synchronizer):
		var cb := func(player: Node) -> void:
			_check_simplify_path_race_player_spawn(player, mt)
		lobby.synchronizer.spawned.connect(cb)
		_hooked_lobby_syncs[lobby.synchronizer] = cb


func _on_lobby_despawned(lobby: Lobby, mt: MultiplayerTree) -> void:
	if not is_instance_valid(lobby) or not is_instance_valid(lobby.level):
		return
	_queue("networked:lobby_event", {
		"tree_name": mt.name,
		"event": "despawned",
		"lobby_name": str(lobby.level.name),
	})
	_unhook_spawners_in(lobby.level)
	if is_instance_valid(lobby.synchronizer) and lobby.synchronizer in _hooked_lobby_syncs:
		var cb: Callable = _hooked_lobby_syncs[lobby.synchronizer]
		if lobby.synchronizer.spawned.is_connected(cb):
			lobby.synchronizer.spawned.disconnect(cb)
		_hooked_lobby_syncs.erase(lobby.synchronizer)


## Connects to the native [signal MultiplayerSpawner.spawned] signal on all spawners
## found under [param root]. Fires a [code]spawner.native_confirmed[/code] component event
## each time the C++ engine actually spawns a node — this is ground truth for whether the
## spawn packet was received and processed. Skips spawners already hooked.
func _hook_spawners_in(root: Node, mt: MultiplayerTree) -> void:
	for spawner: MultiplayerSpawner in root.find_children("*", "MultiplayerSpawner", true, false):
		if spawner in _hooked_spawners:
			continue
		var cb := func(node: Node) -> void:
			var ev := {
				"tree_name": mt.name,
				"side": "S" if mt.is_server else "C",
				"player_name": node.name if is_instance_valid(node) else "?",
				"event_type": "spawner.native_confirmed",
				"data": {
					"node_name": node.name if is_instance_valid(node) else "?",
					"spawner": spawner.name,
				},
				"correlation_id": "",
				"timestamp_usec": Time.get_ticks_usec(),
				"frame": Engine.get_process_frames(),
			}
			_cycle_component_events.append(ev)
			_queue("networked:component_event", ev)
		spawner.spawned.connect(cb)
		_hooked_spawners[spawner] = cb


## Emits a crash manifest when a lobby is spawned on the server while peers are already
## connected. Every MultiplayerSynchronizer (public_visibility=true) and MultiplayerSpawner
## inside the level has already sent a simplify_path packet to those peers — but the peers
## won't receive the level's own spawn packet until the next network poll cycle, so the
## simplify_path resolution fails with "Node not found".
func _check_simplify_path_race_lobby(lobby: Lobby, mt: MultiplayerTree) -> void:
	if not mt.is_server or not mt.multiplayer_api:
		return
	var peers := mt.multiplayer_api.get_peers()
	if peers.is_empty():
		return

	var races: Array = []
	for child in lobby.level.find_children("*", "MultiplayerSpawner", true, false):
		races.append({
			"type": "MultiplayerSpawner", 
			"path": str(child.get_path()),
			"rel_path": _get_rel_path(child, mt),
			"auth": child.get_multiplayer_authority(),
			"is_auth": child.is_multiplayer_authority(),
			"engine_broadcast": true,
		})
	for child in lobby.level.find_children("*", "MultiplayerSynchronizer", true, false):
		var sync := child as MultiplayerSynchronizer
		if sync.public_visibility:
			races.append({
				"type": "MultiplayerSynchronizer",
				"path": str(sync.get_path()),
				"rel_path": _get_rel_path(sync, mt),
				"auth": sync.get_multiplayer_authority(),
				"is_auth": sync.is_multiplayer_authority(),
				"public_visibility": true,
			})

	if races.is_empty():
		return

	EngineDebugger.send_message("networked:crash_manifest", [{
		"cid": str(_cid_stack[0]) if not _cid_stack.is_empty() else "N/A",
		"cid_timeline": _cid_stack.map(func(s: StringName) -> String: return str(s)),
		"trigger": "SERVER_SIMPLIFY_PATH_RACE",
		"frame": Engine.get_process_frames(),
		"timestamp_usec": Time.get_ticks_usec(),
		"active_scene": "",
		"network_state": {
			"is_server": true,
			"peer_id": mt.multiplayer_api.get_unique_id(),
			"connected_peers": peers,
		},
		"preflight_snapshot": races,
		"player_name": lobby.level.name,
		"in_tree": lobby.level.is_inside_tree(),
		"telemetry_slice": _freeze_and_slice(),
	}])
	_maybe_break()


## Emits a crash manifest when a new peer connects to the server while nodes are already
## registered in active lobbies.
##
## When [method MultiplayerTree._on_peer_connected] fires, C++ has already started sending
## [code]simplify_path[/code] packets for every registered [MultiplayerSynchronizer] and
## [MultiplayerSpawner] to the new peer. The client will process those packets before it
## receives the spawn packets for the lobby and player nodes themselves, producing
## "Node not found" errors in [code]process_simplify_path[/code].
##
## This covers two scenarios:
## - ON_STARTUP lobbies: lobby was spawned before the client connected; client gets
##   simplify_path for Level1/MultiplayerSpawner before the Level1 spawn packet.
## - Already-spawned players: a second client connects after player A is in Level1;
##   client B gets simplify_path for diego|A/MultiplayerSynchronizer before Level1
##   or player spawn packets.
func _check_simplify_path_race_on_connect(peer_id: int, mt: MultiplayerTree) -> void:
	if not mt.lobby_manager or not mt.multiplayer_api:
		return

	var races: Array = []
	for lobby_name: StringName in mt.lobby_manager.active_lobbies:
		var lobby: Lobby = mt.lobby_manager.active_lobbies[lobby_name]
		if not is_instance_valid(lobby) or not is_instance_valid(lobby.level):
			continue
		for spawner in lobby.level.find_children("*", "MultiplayerSpawner", true, false):
			races.append({
				"type": "MultiplayerSpawner",
				"path": str(spawner.get_path()),
				"rel_path": _get_rel_path(spawner, mt),
				"auth": spawner.get_multiplayer_authority(),
				"is_auth": spawner.is_multiplayer_authority(),
				"lobby": str(lobby_name),
				"engine_broadcast": true,
			})
		for child in lobby.level.find_children("*", "MultiplayerSynchronizer", true, false):
			var sync := child as MultiplayerSynchronizer
			if sync.public_visibility:
				races.append({
					"type": "MultiplayerSynchronizer",
					"path": str(sync.get_path()),
					"rel_path": _get_rel_path(sync, mt),
					"auth": sync.get_multiplayer_authority(),
					"is_auth": sync.is_multiplayer_authority(),
					"lobby": str(lobby_name),
					"public_visibility": true,
				})

	if races.is_empty():
		return

	EngineDebugger.send_message("networked:crash_manifest", [{
		"cid": str(_cid_stack[0]) if not _cid_stack.is_empty() else "N/A",
		"cid_timeline": _cid_stack.map(func(s: StringName) -> String: return str(s)),
		"trigger": "SERVER_SIMPLIFY_PATH_RACE",
		"frame": Engine.get_process_frames(),
		"timestamp_usec": Time.get_ticks_usec(),
		"active_scene": "",
		"network_state": {
			"is_server": true,
			"peer_id": mt.multiplayer_api.get_unique_id(),
			"new_peer_id": peer_id,
		},
		"preflight_snapshot": races,
		"player_name": "peer_%d" % peer_id,
		"in_tree": true,
		"telemetry_slice": _freeze_and_slice(),
	}])
	_maybe_break()


## Emits a crash manifest when a player is added to a lobby on the server while peers are
## connected.
##
## [signal LobbySynchronizer.spawned] fires (server-side) when [code]player.tree_entered[/code]
## fires after [method LobbySynchronizer.track_player] is called — i.e., the moment the
## player enters the scene tree on the server. C++ has already sent [code]simplify_path[/code]
## for any public-visibility [MultiplayerSynchronizer] on the player to all connected peers.
## Peers who have not yet received the level spawn packet will get "Node not found" when they
## process those simplify_path packets.
func _check_simplify_path_race_player_spawn(player: Node, mt: MultiplayerTree) -> void:
	if not is_instance_valid(player) or not mt.multiplayer_api:
		return
	var peers := mt.multiplayer_api.get_peers()
	if peers.is_empty():
		return

	var races: Array = []
	for child in player.find_children("*", "MultiplayerSynchronizer", true, false):
		var sync := child as MultiplayerSynchronizer
		if sync.public_visibility:
			var entry: Dictionary = {
				"type": "MultiplayerSynchronizer",
				# get_path() requires is_inside_tree(). The spawned signal can fire
				# mid-NOTIFICATION_ENTER_TREE cascade (via spawner.spawned →
				# child_entered_tree), so some descendants may not yet be in the tree.
				"path": str(sync.get_path()) if sync.is_inside_tree() else sync.name,
				"rel_path": _get_rel_path(sync, mt) if sync.is_inside_tree() else sync.name,
				"public_visibility": true,
			}
			if sync.is_inside_tree():
				entry["auth"] = sync.get_multiplayer_authority()
				entry["is_auth"] = sync.is_multiplayer_authority()
			
			races.append(entry)

	if races.is_empty():
		return

	EngineDebugger.send_message("networked:crash_manifest", [{
		"cid": str(_cid_stack[0]) if not _cid_stack.is_empty() else "N/A",
		"cid_timeline": _cid_stack.map(func(s: StringName) -> String: return str(s)),
		"trigger": "SERVER_SIMPLIFY_PATH_RACE",
		"frame": Engine.get_process_frames(),
		"timestamp_usec": Time.get_ticks_usec(),
		"active_scene": "",
		"network_state": {
			"is_server": true,
			"peer_id": mt.multiplayer_api.get_unique_id(),
			"connected_peers": peers,
		},
		"preflight_snapshot": races,
		"player_name": player.name,
		"in_tree": player.is_inside_tree(),
		"telemetry_slice": _freeze_and_slice(),
	}])
	_maybe_break()


func _unhook_spawners_in(root: Node) -> void:
	for spawner: MultiplayerSpawner in root.find_children("*", "MultiplayerSpawner", true, false):
		if spawner not in _hooked_spawners:
			continue
		var cb: Callable = _hooked_spawners[spawner]
		if spawner.spawned.is_connected(cb):
			spawner.spawned.disconnect(cb)
		_hooked_spawners.erase(spawner)


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
	var snapshot := {"tree_name": mt.name, "lobbies": lobbies_data}
	_last_lobby_snapshots[mt.name] = snapshot
	_queue("networked:lobby_snapshot", snapshot)


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
	# register_message_capture strips the "networked:" prefix before calling here,
	# so match against the suffix only.
	match message:
		"watch_node":     _handle_watch_node(data[0])
		"unwatch_node":   _handle_unwatch_node(data[0])
		"set_auto_break": _auto_break = true if data[0] else false


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
		_cycle_peer_events.clear()
		_cycle_component_events.clear()
		return

	# Snapshot cycle data into the ring buffer before dispatching.
	if _telemetry:
		_telemetry.record(
			Engine.get_process_frames(),
			_cid_stack.map(func(s: StringName) -> String: return str(s)),
			_cycle_component_events,
			_cycle_peer_events,
			_last_lobby_snapshots,
		)
	_cycle_peer_events.clear()
	_cycle_component_events.clear()

	for entry: Array in _message_queue:
		EngineDebugger.send_message(entry[0], [entry[1]])
	_message_queue.clear()


# ─── Telemetry Helpers ────────────────────────────────────────────────────────

## Freezes the ring buffer and returns its snapshot as the telemetry_slice for a manifest.
## Call this immediately before sending any crash_manifest message.
func _freeze_and_slice() -> Array:
	if _telemetry:
		_telemetry.freeze()
		return _telemetry.snapshot()
	return []


## Pauses the engine if "Break on Manifest" is enabled in the editor.
## Call this immediately after sending a crash_manifest message so the editor
## panel has already received the manifest before execution halts.
func _maybe_break() -> void:
	if not _auto_break:
		return
	if EngineDebugger.is_skipping_breakpoints():
		push_warning("[Networked] Break on Manifest is enabled but Skip Breakpoints is active — ignoring.")
		return
	breakpoint


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
