## Game-side debug telemetry reporter for the Networked debugger plugin.
##
## This is a singleton (Autoload) node that collects telemetry from all active
## [MultiplayerTree] instances in the process and forwards them to the editor.
##
## All operations are guarded by [method _debug_build], zero overhead in
## release exports or headless/test runs. The local [EngineDebugger] send path
## is additionally gated by [method _has_local_session].
extends Node

# Cached once per process: whether reporting is fundamentally allowed at all.
static var _reporting_enabled: bool = false
static var _reporting_checked: bool = false

# Tracks whether the EngineDebugger capture has been registered for this process.
static var _capture_registered: bool = false

var _trees: Array[MultiplayerTree] = []

# Set on first register_tree(); used as the source_tree_name for relay payloads.
var _local_tree_name: String = ""

# Relay node — created lazily when the first MultiplayerTree registers in a debug build.
# Null in release builds and when no tree has registered yet.
var _relay_node: Variant = null  # NetDebugRelay — typed as Variant to avoid forward-ref error
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

# Phased validation engine — topology-style validators only.
# Race and zombie detection are handled as named methods (see _check_* below)
# because they need structured output incompatible with Array[String].
var _validators: Array[NetValidator] = []


func _enter_tree() -> void:
	if not _debug_build():
		return

	# Clear any stale spans left over from a previous run.
	NetTrace.reset()
	# Route span tracing through the unified emit path so spans reach the relay too.
	NetTrace.message_delegate = func(msg: String, payload: Dictionary) -> void:
		emit_debug_event(msg, payload)

	# The message capture (editor → game commands) only makes sense when a local
	# editor session exists. Skip registration on headless / relay-only processes.
	if _has_local_session() and not _capture_registered:
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

	_validators = [TopologyNetValidator.new()]

	_watchdog = ErrorWatchdog.new()
	add_child(_watchdog)
	_watchdog.cpp_error_caught.connect(_on_cpp_error_caught)


func _exit_tree() -> void:
	if not _debug_build():
		return

	NetTrace.message_delegate = Callable()
	
	for mt in _trees:
		var event := NetSessionEvent.new()
		event.tree_name = mt.name
		_queue("networked:session_unregistered", event.to_dict())
	_flush_now()


## Registers a [MultiplayerTree] for debug reporting.
func register_tree(mt: MultiplayerTree) -> void:
	if not _debug_build():
		return
	if mt in _trees:
		return
	if _local_tree_name.is_empty():
		_local_tree_name = mt.name

	_trees.append(mt)

	mt.peer_connected.connect(_on_peer_connected.bind(mt))
	mt.peer_disconnected.connect(_on_peer_disconnected.bind(mt))
	mt.configured.connect(_on_configured.bind(mt))

	_setup_relay(mt)

	var backend_class := ""
	if mt.backend and mt.backend.get_script():
		backend_class = mt.backend.get_script().get_global_name()

	var event := NetSessionEvent.new()
	event.tree_name = mt.name
	event.is_server = mt.is_server
	event.backend_class = backend_class
	_queue("networked:session_registered", event.to_dict())


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
	
	var event := NetSessionEvent.new()
	event.tree_name = mt.name
	_queue("networked:session_unregistered", event.to_dict())
	_flush_now()


# ─── Signal Handlers ──────────────────────────────────────────────────────────

func _on_configured(mt: MultiplayerTree) -> void:
	if not _debug_build():
		return
	if mt.clock:
		mt.clock.pong_received.connect(_on_clock_pong.bind(mt))
	if mt.lobby_manager:
		mt.lobby_manager.lobby_spawned.connect(_on_lobby_spawned.bind(mt))
		mt.lobby_manager.lobby_despawned.connect(_on_lobby_despawned.bind(mt))


func _on_cpp_error_caught(timestamp: int, error_text: String) -> void:
	if not _debug_build():
		return

	var active := NetTrace.active_span()
	var cid_val := str(active.id) if active else "N/A"
	var mt: MultiplayerTree = _trees[0] if not _trees.is_empty() else null

	var m := NetCppErrorManifest.new()
	_fill_base(m, "CPP_ERROR_LOG_WATCHDOG", cid_val, mt)
	m.timestamp_usec = timestamp  # use watchdog timestamp, not current tick
	m.network_state = {
		"tree_name": _active_tree_name(active),
		"peer_id": mt.multiplayer_api.get_unique_id() if is_instance_valid(mt) and mt.multiplayer_api else 0,
	}
	m.errors = [error_text]
	m.error_text = error_text
	_send_manifest(m)


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


func _on_clock_pong(data: Dictionary, mt: MultiplayerTree) -> void:
	_queue("networked:clock_sample", NetClockSample.from_dict(data, mt.name).to_dict())


func _on_peer_connected(peer_id: int, mt: MultiplayerTree) -> void:
	var ev := {"tree_name": mt.name, "peer_id": peer_id, "event": "connected"}
	_cycle_peer_events.append(ev)

	var event := NetPeerEvent.new()
	event.tree_name = mt.name
	event.peer_id = peer_id
	_queue("networked:peer_connected", event.to_dict())

	if mt.is_server and _relay_node:
		_relay_node.authorize(peer_id)

	if mt.is_server:
		var span: NetSpan = NetTrace.begin_peer("peer_connect", [peer_id], {"tree": mt.name}, mt.name)
		span.step("server_received_connect")
		_check_simplify_path_race_on_connect(peer_id, mt, span)


func _on_peer_disconnected(peer_id: int, mt: MultiplayerTree) -> void:
	var ev := {"tree_name": mt.name, "peer_id": peer_id, "event": "disconnected"}
	_cycle_peer_events.append(ev)

	var event := NetPeerEvent.new()
	event.tree_name = mt.name
	event.peer_id = peer_id
	_queue("networked:peer_disconnected", event.to_dict())

	if mt.is_server and _relay_node:
		_relay_node.deauthorize(peer_id)
	
	get_tree().create_timer(2.0).timeout.connect(
		func() -> void: _check_zombie_player(peer_id, mt),
		CONNECT_ONE_SHOT
	)


## Scans active lobbies for nodes still owned by [param peer_id] two seconds after
## that peer disconnected. If any are found, emits a [code]ZOMBIE_PLAYER_DETECTED[/code]
## manifest so the developer knows cleanup did not run in time.
func _check_zombie_player(peer_id: int, mt: MultiplayerTree) -> void:
	if not _debug_build():
		return
	if not is_instance_valid(mt) or not mt.lobby_manager:
		return
	var zombies: Array[String] = []
	for lobby_name: StringName in mt.lobby_manager.active_lobbies:
		var lobby: Lobby = mt.lobby_manager.active_lobbies[lobby_name]
		if not is_instance_valid(lobby) or not is_instance_valid(lobby.level):
			continue
		for node: Node in lobby.level.find_children("*", "Node", true, false):
			if is_instance_valid(node) and node.get_multiplayer_authority() == peer_id:
				zombies.append(str(node.get_path()))
	if zombies.is_empty():
		return

	var m := NetZombieManifest.new()
	_fill_base(m, "ZOMBIE_PLAYER_DETECTED", "N/A", mt)
	m.network_state["disconnected_peer_id"] = peer_id
	m.errors = zombies
	_send_manifest(m)


func _on_lobby_spawned(lobby: Lobby, mt: MultiplayerTree) -> void:
	if not is_instance_valid(lobby) or not is_instance_valid(lobby.level):
		return
	
	var event := NetLobbyEvent.new()
	event.tree_name = mt.name
	event.event = "spawned"
	event.lobby_name = str(lobby.level.name)
	_queue("networked:lobby_event", event.to_dict())
	
	_hook_spawners_in(lobby.level, mt)

	var lobby_span: NetPeerSpan = null
	if mt.is_server:
		lobby_span = NetTrace.begin_peer("lobby_spawn",
			mt.multiplayer_api.get_peers() if mt.multiplayer_api else [],
			{"lobby_name": str(lobby.level.name), "tree": mt.name}, mt.name)
		lobby_span.step("spawners_registering")
	# Capture token before _check_simplify_path_race_lobby closes/fails the span,
	# so player_spawn spans can declare the causal link.
	var lobby_token: CheckpointToken = lobby_span.checkpoint() if lobby_span else null
	_check_simplify_path_race_lobby(lobby, mt, lobby_span)

	if mt.is_server and is_instance_valid(lobby.synchronizer):
		var cb := func(player: Node) -> void:
			var peers: Array = Array(mt.multiplayer_api.get_peers()) if mt.multiplayer_api else []
			var spawn_span: NetSpan = NetTrace.begin_peer("player_spawn", peers, {
				"player": player.name,
				"tree": mt.name,
			}, mt.name, lobby_token)
			spawn_span.step("player_entered_tree")
			_check_simplify_path_race_player_spawn(player, mt, spawn_span)
			_check_player_topology(player, mt, spawn_span)
		lobby.synchronizer.spawned.connect(cb)

		_hooked_lobby_syncs[lobby.synchronizer] = cb


func _on_lobby_despawned(lobby: Lobby, mt: MultiplayerTree) -> void:
	if not is_instance_valid(lobby) or not is_instance_valid(lobby.level):
		return
	
	var event := NetLobbyEvent.new()
	event.tree_name = mt.name
	event.event = "despawned"
	event.lobby_name = str(lobby.level.name)
	_queue("networked:lobby_event", event.to_dict())
	
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
		var cb := func(node: Node) -> void:
			var active := NetTrace.active_span()
			if active and active.label == "player_spawn":
				active.step("spawner_native_confirmed", {"node_path": str(node.get_path())})
		spawner.spawned.connect(cb)
		_hooked_spawners[spawner] = cb


## Emits a crash manifest when a lobby is spawned on the server while peers are already
## connected. Every [MultiplayerSynchronizer] ([code]public_visibility=true[/code])
## and [MultiplayerSpawner] inside the level has already sent a [code]simplify_path[/code]
## packet to those peers, but the peers won't receive the level's own spawn packet until
## the next network poll cycle, so the [code]simplify_path[/code] resolution fails with
## [code]"Node not found"[/code].
func _check_simplify_path_race_lobby(lobby: Lobby, mt: MultiplayerTree, span: NetPeerSpan) -> void:
	var races := NetRaceDetector.find_lobby_races(lobby, mt)
	if races.is_empty():
		if span:
			span.step("no_race_detected")
			span.end()
		return

	if span:
		span.fail("simplify_path_race", {"node_count": races.size()})

	var cid_val := str(span.id) if span else "N/A"
	var m := NetRaceManifest.new()
	_fill_base(m, "SERVER_SIMPLIFY_PATH_RACE", cid_val, mt)
	m.network_state["connected_peers"] = mt.multiplayer_api.get_peers() if mt.multiplayer_api else []
	m.errors = _races_to_strings(races)
	m.preflight_snapshot = races
	m.player_name = lobby.level.name
	m.in_tree = lobby.level.is_inside_tree()
	_send_manifest(m)


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
	var races := NetRaceDetector.find_connect_races(peer_id, mt)
	if races.is_empty():
		if span:
			span.step("no_race_detected")
			span.end()
		return

	if span:
		span.fail("simplify_path_race", {"node_count": races.size()})

	var cid_val := str(span.id) if span else "N/A"
	var m := NetRaceManifest.new()
	_fill_base(m, "SERVER_SIMPLIFY_PATH_RACE", cid_val, mt)
	m.network_state["new_peer_id"] = peer_id
	m.errors = _races_to_strings(races)
	m.preflight_snapshot = races
	m.player_name = "peer_%d" % peer_id
	m.in_tree = true
	_send_manifest(m)


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
	var races := NetRaceDetector.find_player_races(player, mt)
	if races.is_empty():
		if span:
			span.step("no_race_detected")
			span.end()
		return

	if span:
		span.fail("simplify_path_race", {"node_count": races.size()})

	var cid_val := str(span.id) if span else "N/A"
	var m := NetRaceManifest.new()
	_fill_base(m, "SERVER_SIMPLIFY_PATH_RACE", cid_val, mt)
	m.network_state["connected_peers"] = mt.multiplayer_api.get_peers() if mt.multiplayer_api else []
	m.errors = _races_to_strings(races)
	m.preflight_snapshot = races
	m.player_name = player.name
	m.in_tree = player.is_inside_tree()
	_send_manifest(m)


## Validates the synchronizer topology of a newly spawned player.
##
## Runs all registered [NetValidator]s for the [code]"player_spawn"[/code] trigger
## in phase order. On failure, emits a [NetTopologyManifest] with the error list
## and a [NetNodeSnapshot] of the player node. Always emits a topology snapshot
## for the Topology panel regardless of validation outcome.
func _check_player_topology(player: Node, mt: MultiplayerTree, span: NetSpan) -> void:
	_send_topology_snapshot(player, mt)

	var errors := _execute_validators("player_spawn", {"player": player, "mt": mt})
	if errors.is_empty():
		if span:
			span.step("topology_validated")
		return

	if span:
		span.fail("topology_invalid", {"errors": errors})

	var cid_val := str(span.id) if span else "N/A"
	var m := NetTopologyManifest.new()
	_fill_base(m, "TOPOLOGY_VALIDATION_FAILED", cid_val, mt)
	m.errors = errors
	m.player_name = player.name
	m.in_tree = player.is_inside_tree()
	m.node_snapshot = NetNodeSnapshot.from_node(player)
	_send_manifest(m)


## Builds and emits a [NetTopologySnapshot] for [param player] via [method emit_debug_event].
## Called on every player spawn so the Topology panel always reflects current state.
func _send_topology_snapshot(player: Node, mt: MultiplayerTree) -> void:
	if not _debug_build():
		return
	var snap := NetTopologySnapshot.new()
	snap.tree_name = mt.name
	snap.node_path = str(player.get_path())
	snap.peer_id = player.get_multiplayer_authority()
	snap.is_server = mt.is_server
	snap.lobby_name = player.get_parent().name \
		if is_instance_valid(player.get_parent()) else ""
	for sync: MultiplayerSynchronizer in SynchronizersCache.get_synchronizers(player):
		var si := NetTopologySnapshot.SyncInfo.new()
		si.name = sync.name
		si.root_path = str(sync.root_path)
		si.authority = sync.get_multiplayer_authority()
		si.enabled = true
		if sync.replication_config:
			for prop: NodePath in sync.replication_config.get_properties():
				var pi := NetTopologySnapshot.PropInfo.new()
				pi.path = str(prop)
				pi.replication_mode = sync.replication_config.property_get_replication_mode(prop)
				pi.spawn = sync.replication_config.property_get_spawn(prop)
				pi.sync = sync.replication_config.property_get_sync(prop)
				pi.watch = sync.replication_config.property_get_watch(prop)
				si.properties.append(pi)
		snap.synchronizers.append(si)
	emit_debug_event("networked:topology_snapshot", snap.to_dict())


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
	
	var snapshot := NetReplicationSnapshot.new()
	snapshot.tree_name = mt.name
	snapshot.node_path = str(node.get_path())
	snapshot.properties = _collect_properties(node, sync)
	_queue("networked:replication_snapshot", snapshot.to_dict())


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
	
	var snapshot := NetReplicationSnapshot.new()
	snapshot.tree_name = mt.name
	snapshot.node_path = str(node.get_path())
	snapshot.properties = all_props
	snapshot.inventory = inventory
	_queue("networked:replication_snapshot", snapshot.to_dict())


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
		"watch_node":          _handle_watch_node(data[0])
		"unwatch_node":        _handle_unwatch_node(data[0])
		"set_auto_break":      _auto_break = true if data[0] else false
		"cpp_error_caught":    _on_cpp_error_caught(Time.get_ticks_usec(), _format_cpp_error(data))
		"inspect_node":        pass  # Reserved: open node_path in game-side inspector
		"visualizer_toggle":   pass  # Reserved: toggle overlay on receiving peer


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
	if not _debug_build():
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
		emit_debug_event(entry[0], entry[1])
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


## Single dispatch point for all [code]networked:crash_manifest[/code] emissions.
##
## Routes via [method emit_debug_event] when in a debug build (covers both local
## EngineDebugger and the relay path); falls back to [method push_error] otherwise
## so failures surface in release exports too. Always calls [method _maybe_break].
func _send_manifest(manifest: NetManifest) -> void:
	var payload := manifest.to_dict()
	if _has_local_session() or _relay_active():
		emit_debug_event("networked:crash_manifest", payload)
	else:
		push_error("[NETWORKED] %s | scene=%s peer=%d errors=%s" % [
			manifest.trigger,
			manifest.active_scene,
			manifest.network_state.get("peer_id", 0),
			str(payload.get("errors", [])),
		])
	_maybe_break()


## Fills the common base fields of [param m] from current engine state.
## Callers may overwrite individual fields (e.g. [code]timestamp_usec[/code],
## extra [code]network_state[/code] keys) after calling this.
func _fill_base(m: NetManifest, trigger: String, cid_val: String, mt: MultiplayerTree) -> void:
	m.trigger = trigger
	m.cid = cid_val
	m.cid_timeline = [cid_val]
	m.frame = Engine.get_process_frames()
	m.timestamp_usec = Time.get_ticks_usec()
	m.active_scene = get_tree().current_scene.scene_file_path \
		if get_tree() and get_tree().current_scene else "?"
	m.network_state = {
		"is_server": mt.is_server if is_instance_valid(mt) else false,
		"tree_name": mt.name if is_instance_valid(mt) else "",
		"peer_id": mt.multiplayer_api.get_unique_id() \
			if is_instance_valid(mt) and mt.multiplayer_api else 0,
	}
	m.telemetry_slice = _freeze_and_slice()


## Runs all registered validators for [param trigger] in phase order.
## Stops at the first phase that produces errors (prevents cascading failures).
## Returns an empty array when all checks pass.
func _execute_validators(trigger: String, ctx: Dictionary) -> Array[String]:
	for phase: int in [NetValidator.STRUCTURAL, NetValidator.LOGICAL, NetValidator.HEAVY_HEURISTIC]:
		var phase_errors: Array[String] = []
		for v: NetValidator in _validators:
			if v.phase == phase:
				phase_errors.append_array(v.execute(trigger, ctx))
		if not phase_errors.is_empty():
			return phase_errors
	return []


## Converts raw race detail dicts (from [NetRaceDetector]) to human-readable strings
## for the [code]errors[/code] field in [NetRaceManifest].
static func _races_to_strings(races: Array[Dictionary]) -> Array[String]:
	var out: Array[String] = []
	for r: Dictionary in races:
		out.append("simplify_path race on %s" % r.get("rel_path", r.get("path", "?")))
	return out


## Pauses the engine if break is enabled in the editor for a Crash Manifest.
## Call this immediately after sending a [code]crash_manifest[/code] message so
## the editor panel has already received the manifest before execution halts.
func _maybe_break() -> void:
	if EngineDebugger.is_active() and _auto_break:
		breakpoint


# ─── Unified send ─────────────────────────────────────────────────────────────

## Single dispatch point for all outgoing debug messages.
##
## Always attempts both paths in parallel — they are not alternatives.
## Local path: fires immediately when a live [EngineDebugger] TCP connection exists.
## Relay path: fires when a relay node is active (Phase 2+), so other editors on
##             the same game network can see this process's telemetry.
func emit_debug_event(msg: String, data: Dictionary) -> void:
	if _has_local_session():
		EngineDebugger.send_message(msg, [data])
	if _relay_active():
		_relay_node.send(msg, data, _local_tree_name)


# ─── Guards ───────────────────────────────────────────────────────────────────

## True in editor runs and exported debug builds; false in release.
## Gates all debug overhead: relay setup, hook registration, telemetry collection.
static func _debug_build() -> bool:
	if not _reporting_checked:
		_reporting_checked = true
		_reporting_enabled = true
		for arg in OS.get_cmdline_args():
			if arg in ["--gdunit", "--headless"]:
				_reporting_enabled = false
				break
	return _reporting_enabled and OS.has_feature("debug")


## True when this process has a live EngineDebugger TCP connection to an editor.
## Gates only the direct [method EngineDebugger.send_message] calls.
static func _has_local_session() -> bool:
	return EngineDebugger.is_active()


## True when the relay node is ready and the multiplayer session is active.
func _relay_active() -> bool:
	return _relay_node != null and _relay_node.is_relay_active()


# ─── Relay setup ──────────────────────────────────────────────────────────────

## Creates the relay node the first time a MultiplayerTree registers (debug builds only).
##
## Server: owns the relay (receives forwards from clients, dispatches to recipients).
## Client with editor: creates a stub relay child so RPCs are addressable, then
##   self-registers as a recipient on the server relay.
## Client without editor: creates stub relay only (will send telemetry but not receive).
func _setup_relay(mt: MultiplayerTree) -> void:
	if not _debug_build():
		return
	if _relay_node:
		return  # Already set up from a previous register_tree call.

	var relay := NetDebugRelay.new()
	relay.name = "NetDebugRelay"
	add_child(relay)
	_relay_node = relay

	if _has_local_session() and not mt.is_server:
		# Ask the server relay to add this process to its recipients list.
		relay.register_as_recipient.rpc_id(1)
