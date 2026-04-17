## Game-side debug telemetry reporter for the Networked debugger plugin.
##
## This is a singleton (Autoload) node that collects telemetry from all active
## [MultiplayerTree] instances in the process and forwards them to the editor.
##
## All operations are guarded by [method _should_report], zero overhead in
## exported builds or headless/test runs.
extends Node

# Cached once per process: whether reporting is fundamentally allowed at all.
static var _reporting_enabled: bool = false
static var _reporting_checked: bool = false

# Tracks whether the EngineDebugger capture has been registered for this process.
static var _capture_registered: bool = false

var _trees: Array[MultiplayerTree] = []
var _message_queue: Array = []
var _flush_pending: bool = false

# Tree Name -> NodePath -> Array of {sync, callable} for demand-driven replication watch.
var _watched: Dictionary = {}

# MultiplayerSpawner -> Callable — tracks which spawners have native confirmation hooks.
var _hooked_spawners: Dictionary = {}

# LobbySynchronizer -> Callable — tracks player-spawn race detection hooks.
var _hooked_lobby_syncs: Dictionary = {}


# Whether to call EngineDebugger.debug() after sending a crash manifest.
# Toggled from the editor via the "Break on Manifest" button.
var _auto_break: bool = false

# Telemetry ring buffer — records one entry per flush cycle.
var _watchdog: ErrorWatchdog
var _telemetry: NetTelemetryBuffer

# Peer events accumulated during the current flush cycle.
var _cycle_peer_events: Array = []


func _enter_tree() -> void:
	if not _should_report():
		return

	# Clear any stale spans left over from a previous run.
	NetTrace.reset()
	# Inject the debugger implementation into the tracing system.
	NetTrace.message_delegate = func(msg: String, payload: Dictionary) -> void:
		EngineDebugger.send_message(msg, [payload])

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
	
	NetTrace.message_delegate = Callable()
	
	for mt in _trees:
		_queue("networked:session_unregistered", {"tree_name": mt.name})
	_flush_now()


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

	# Prefer the active span's ID as the CID so the error attaches to its
	# causal context.
	var active := NetTrace.active_span()
	var cid_val := str(active.id) if active else "N/A"
	var cid_timeline: Array = [cid_val] if active else ["N/A"]

	EngineDebugger.send_message("networked:crash_manifest", [{
		"cid": cid_val,
		"cid_timeline": cid_timeline,
		"trigger": "C++ ERROR / LOG WATCHDOG",
		"tree_name": _active_tree_name(active),
		"frame": Engine.get_process_frames(),
		"timestamp_usec": timestamp,
		"active_scene": get_tree().current_scene.scene_file_path if get_tree() and get_tree().current_scene else "?",
		"error_text": error_text,
		"telemetry_slice": _freeze_and_slice(),
	}])
	_maybe_break()


## Returns the tree_name for the current context: prefers the active span's tree,
## falls back to the first registered tree, falls back to empty string.
func _active_tree_name(active_span: RefCounted = null) -> String:
	if active_span:
		var tn: Variant = active_span.get("tree_name")
		if tn is String and not (tn as String).is_empty():
			return tn as String
	if not _trees.is_empty():
		return _trees[0].name
	return ""


## Returns true if [param sync] has at least one property configured for delta
## replication ([constant REPLICATION_MODE_ALWAYS] or 
## [constant REPLICATION_MODE_ON_CHANGE]).
## Spawn-only synchronizers (all [constant REPLICATION_MODE_NEVER]) and 
## client-authoritative synchronizers do not push state to a newly connecting 
## peer from the server, so they are not a meaningful race risk and are excluded 
## from detection.
func _has_delta_replication(sync: MultiplayerSynchronizer) -> bool:
	if not sync.replication_config:
		return false
	for prop in sync.replication_config.get_properties():
		var mode := sync.replication_config.property_get_replication_mode(prop)
		if mode == SceneReplicationConfig.REPLICATION_MODE_ALWAYS \
				or mode == SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE:
			return true
	return false


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
		var span: NetSpan = NetTrace.begin_peer("peer_connect", [peer_id], {"tree": mt.name}, mt.name)
		span.step("server_received_connect")
		_check_simplify_path_race_on_connect(peer_id, mt, span)


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

	var lobby_span: NetPeerSpan = null
	if mt.is_server and mt.multiplayer_api and not mt.multiplayer_api.get_peers().is_empty():
		lobby_span = NetTrace.begin_peer("lobby_spawn", mt.multiplayer_api.get_peers(), {
			"lobby_name": str(lobby.level.name),
			"tree": mt.name,
		}, mt.name)
		lobby_span.step("spawners_registering")
	_check_simplify_path_race_lobby(lobby, mt, lobby_span)

	if mt.is_server and is_instance_valid(lobby.synchronizer):
		var cb := func(player: Node) -> void:
			var peers: Array = Array(mt.multiplayer_api.get_peers()) if mt.multiplayer_api else []
			var spawn_span: NetSpan = NetTrace.begin_peer("player_spawn", peers, {
				"player": player.name,
				"tree": mt.name,
			}, mt.name)
			spawn_span.step("player_entered_tree")
			_check_simplify_path_race_player_spawn(player, mt, spawn_span)
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


## Connects to the native [signal MultiplayerSpawner.spawned] signal on all 
## spawners found under [param root]. Fires a [code]spawner.native_confirmed[/code] 
## component event each time the C++ engine actually spawns a node, this is ground 
## truth for whether the spawn packet was received and processed. Skips spawners 
## already hooked.
func _hook_spawners_in(root: Node, mt: MultiplayerTree) -> void:
	for spawner: MultiplayerSpawner in root.find_children("*", "MultiplayerSpawner", true, false):
		if spawner in _hooked_spawners:
			continue
		var cb := func(_node: Node) -> void:
			pass
		spawner.spawned.connect(cb)
		_hooked_spawners[spawner] = cb


## Emits a crash manifest when a lobby is spawned on the server while peers are already
## connected. Every [MultiplayerSynchronizer] ([code]public_visibility=true[/code]) 
## and [MultiplayerSpawner] inside the level has already sent a [code]simplify_path[/code] 
## packet to those peers, but the peers won't receive the level's own spawn packet until 
## the next network poll cycle, so the [code]simplify_path[/code] resolution fails with 
## [code]"Node not found"[/code].
func _check_simplify_path_race_lobby(lobby: Lobby, mt: MultiplayerTree, span: NetPeerSpan) -> void:
	if not mt.is_server or not mt.multiplayer_api:
		return
	var peers := mt.multiplayer_api.get_peers()
	if peers.is_empty():
		return

	var races: Array = []
	for child in lobby.level.find_children("*", "MultiplayerSynchronizer", true, false):
		var sync := child as MultiplayerSynchronizer
		if sync.public_visibility and sync.is_multiplayer_authority() and _has_delta_replication(sync):
			races.append({
				"type": "MultiplayerSynchronizer",
				"path": str(sync.get_path()),
				"rel_path": _get_rel_path(sync, mt),
				"auth": sync.get_multiplayer_authority(),
				"is_auth": sync.is_multiplayer_authority(),
				"public_visibility": true,
			})

	if races.is_empty():
		if span:
			span.step("no_race_detected")
			span.end()
		return

	if span:
		span.fail("simplify_path_race", {"node_count": races.size()})

	var cid_val := str(span.id) if span else "N/A"
	EngineDebugger.send_message("networked:crash_manifest", [{
		"cid": cid_val,
		"cid_timeline": [cid_val],
		"trigger": "SERVER_SIMPLIFY_PATH_RACE",
		"frame": Engine.get_process_frames(),
		"timestamp_usec": Time.get_ticks_usec(),
		"active_scene": "",
		"network_state": {
			"is_server": true,
			"tree_name": mt.name,
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
## [code]"Node not found"[/code] errors in [code]process_simplify_path[/code].
##
## This covers two scenarios:
## [br]
## - [constant ON_STARTUP] lobbies: lobby was spawned before the client connected; 
##   client gets [code]simplify_path[/code] for [code]Level1/MultiplayerSpawner[/code] 
##   before the [code]Level1[/code] spawn packet.
## [br]
## - Already-spawned players: a second client connects after player A is in 
##   [code]Level1[/code]; client B gets [code]simplify_path[/code] for 
##   [code]diego|A/MultiplayerSynchronizer[/code] before [code]Level1[/code] or 
##   player spawn packets.
func _check_simplify_path_race_on_connect(peer_id: int, mt: MultiplayerTree, span: NetPeerSpan) -> void:
	if not mt.lobby_manager or not mt.multiplayer_api:
		return

	var races: Array = []
	for lobby_name: StringName in mt.lobby_manager.active_lobbies:
		var lobby: Lobby = mt.lobby_manager.active_lobbies[lobby_name]
		if not is_instance_valid(lobby) or not is_instance_valid(lobby.level):
			continue
		for child in lobby.level.find_children("*", "MultiplayerSynchronizer", true, false):
			var sync := child as MultiplayerSynchronizer
			if sync.public_visibility and sync.is_multiplayer_authority() and _has_delta_replication(sync):
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
		if span:
			span.step("no_race_detected")
			span.end()
		return

	if span:
		span.fail("simplify_path_race", {"node_count": races.size()})

	var cid_val := str(span.id) if span else "N/A"
	EngineDebugger.send_message("networked:crash_manifest", [{
		"cid": cid_val,
		"cid_timeline": [cid_val],
		"trigger": "SERVER_SIMPLIFY_PATH_RACE",
		"frame": Engine.get_process_frames(),
		"timestamp_usec": Time.get_ticks_usec(),
		"active_scene": "",
		"network_state": {
			"is_server": true,
			"tree_name": mt.name,
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
## fires after [method LobbySynchronizer.track_player] is called, i.e., the moment the
## player enters the scene tree on the server. C++ has already sent [code]simplify_path[/code]
## for any public-visibility [MultiplayerSynchronizer] on the player to all connected peers.
## Peers who have not yet received the level spawn packet will get [code]"Node not found"[/code] 
## when they process those [code]simplify_path[/code] packets.
func _check_simplify_path_race_player_spawn(player: Node, mt: MultiplayerTree, span: NetPeerSpan) -> void:
	if not is_instance_valid(player) or not mt.multiplayer_api:
		return
	var peers := mt.multiplayer_api.get_peers()
	if peers.is_empty():
		return

	var races: Array = []
	for child in player.find_children("*", "MultiplayerSynchronizer", true, false):
		var sync := child as MultiplayerSynchronizer
		if sync.is_inside_tree() and sync.public_visibility and sync.is_multiplayer_authority() and _has_delta_replication(sync):
			races.append({
				"type": "MultiplayerSynchronizer",
				"path": str(sync.get_path()),
				"rel_path": _get_rel_path(sync, mt),
				"auth": sync.get_multiplayer_authority(),
				"is_auth": true,
				"public_visibility": true,
			})

	if races.is_empty():
		if span:
			span.step("no_race_detected")
			span.end()
		return

	if span:
		span.fail("simplify_path_race", {"node_count": races.size()})

	var cid_val := str(span.id) if span else "N/A"
	EngineDebugger.send_message("networked:crash_manifest", [{
		"cid": cid_val,
		"cid_timeline": [cid_val],
		"trigger": "SERVER_SIMPLIFY_PATH_RACE",
		"frame": Engine.get_process_frames(),
		"timestamp_usec": Time.get_ticks_usec(),
		"active_scene": "",
		"network_state": {
			"is_server": true,
			"tree_name": mt.name,
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


## Formats Godot's raw "error" debugger message into a readable string.
## Godot 4 sends: 
## [code][source_func, source_file, source_line, error_code, error_descr, is_warning, ...][/code]
func _format_cpp_error(data: Array) -> String:
	var parts: PackedStringArray = []
	var prefix := "WARNING" if data.size() > 5 and data[5] else "ERROR"
	var code: String    = str(data[3]) if data.size() > 3 else ""
	var descr: String   = str(data[4]) if data.size() > 4 else ""
	var func_: String   = str(data[0]) if data.size() > 0 else ""
	var file_: String   = str(data[1]) if data.size() > 1 else ""
	var line_: int      = int(data[2])  if data.size() > 2 else 0
	if not code.is_empty():
		parts.append("%s: %s" % [prefix, code])
	if not descr.is_empty():
		parts.append("  %s" % descr)
	if not func_.is_empty():
		parts.append("  at: %s (%s:%d)" % [func_, file_, line_])
	return "\n".join(parts) if not parts.is_empty() else str(data)


# ─── Incoming Editor Messages ─────────────────────────────────────────────────

func _on_editor_message(message: String, data: Array) -> void:
	if data.is_empty():
		return
	# register_message_capture strips the "networked:" prefix before calling here,
	# so match against the suffix only.
	match message:
		"watch_node":        _handle_watch_node(data[0])
		"unwatch_node":      _handle_unwatch_node(data[0])
		"set_auto_break":    _auto_break = true if data[0] else false
		"cpp_error_caught":  _on_cpp_error_caught(Time.get_ticks_usec(), _format_cpp_error(data))


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
		return

	# Snapshot cycle data into the ring buffer before dispatching.
	# Include the active span's ID at the front of the CID trail so the
	# telemetry slice shows which span was in flight during this cycle.
	if _telemetry:
		var active_span := NetTrace.active_span()
		var cid_trail: Array = [str(active_span.id)] if active_span else ["N/A"]
		_telemetry.record(
			Engine.get_process_frames(),
			cid_trail,
			[], # component events retired
			_cycle_peer_events,
			{}, # lobby snapshots retired
		)
	_cycle_peer_events.clear()

	for entry: Array in _message_queue:
		EngineDebugger.send_message(entry[0], [entry[1]])
	_message_queue.clear()


# ─── Telemetry Helpers ────────────────────────────────────────────────────────

## Freezes the ring buffer and returns its snapshot as the [code]telemetry_slice[/code] 
## for a manifest. Call this immediately before sending any [code]crash_manifest[/code] 
## message.
func _freeze_and_slice() -> Array:
	if _telemetry:
		_telemetry.freeze()
		return _telemetry.snapshot(20)
	return []


## Pauses the engine if break is enabled in the editor for a Crash Manifest.
## Call this immediately after sending a [code]crash_manifest[/code] message so 
## the editor panel has already received the manifest before execution halts.
func _maybe_break() -> void:
	if not _auto_break:
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
