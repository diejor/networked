## Game-side debug telemetry reporter for the Networked debugger plugin.
##
## This is a singleton (Autoload) node that collects telemetry from all active
## [MultiplayerTree] instances in the process and forwards them to the editor.
## [br][br]
## [b]Note:[/b] All operations are guarded by [method _debug_build], ensuring
## zero overhead in release exports or headless/test runs.
extends Node
class_name NetworkedDebugReporter

static var _reporting_enabled: bool = false
static var _reporting_checked: bool = false
static var _capture_registered: bool = false


## Explicitly enables or disables debug reporting.
## [br][br]
## If [param enabled] is [code]true[/code], the reporter attempts to register
## a message capture with the [EngineDebugger].
static func set_enabled(enabled: bool) -> void:
	_reporting_enabled = enabled
	_reporting_checked = true
	var reporter := _get_instance()
	if reporter and enabled:
		reporter._try_register_capture()


## Property-style access for the singleton instance.
var enabled: bool:
	get:
		return _debug_build()
	set(value):
		set_enabled(value)

## Unique ID for this reporter instance; used to deduplicate echos in the editor.
var reporter_id: String = ""


var _trees: Array[MultiplayerTree] = []

var _debug_contexts: Dictionary = {}
var _message_queue: Array = []
var _flush_pending: bool = false

var _watched: Dictionary = {}
var _auto_break: bool = false

var _watchdog: ErrorWatchdog
var _telemetry: NetTelemetryBuffer
var _cycle_peer_events: Array = []
var _validators: Array[NetValidator] = []

var _manifest_count_sec: Dictionary = {}
var _manifest_count_min: Dictionary = {}
var _last_manifest_sec_msec: Dictionary = {}
var _last_manifest_min_msec: Dictionary = {}

var _is_sending_manifest: bool = false
var _clock_monitor: NetClockMonitor = null
var _pending_zombie_checks: Array[SceneTreeTimer] = []

var _dbg: NetwHandle = Netw.dbg.handle(self)


## Resets all debug registries, history, and telemetry.
## [br][br]
## This performs a deep reset, freeing all internal [NetDebugTreeContext]
## instances and clearing the [NetTrace] history.
func reset_state() -> void:
	_reporting_checked = false
	_reporting_enabled = false
	_capture_registered = false

	if _clock_monitor:
		_clock_monitor.clear_all()

	for ctx in _debug_contexts.values():
		if is_instance_valid(ctx):
			ctx.free()

	_debug_contexts.clear()
	_trees.clear()

	_message_queue.clear()
	_cycle_peer_events.clear()
	_watched.clear()
	active_visualizers.clear()

	for timer in _pending_zombie_checks:
		if is_instance_valid(timer):
			# Disconnect all connections from the timeout signal
			for connection in timer.timeout.get_connections():
				timer.timeout.disconnect(connection.callable)
	_pending_zombie_checks.clear()

	if _telemetry:
		_telemetry.clear()

	if LocalLoopbackSession.shared:
		LocalLoopbackSession.shared.reset()
		LocalLoopbackSession.shared = null
	
	Netw.dbg.reset()
	_dbg.trace("Reporter: State reset (deep).")

static func _get_instance() -> NetworkedDebugReporter:
	if Engine.has_singleton("NetworkedDebugger"):
		return Engine.get_singleton("NetworkedDebugger") as NetworkedDebugReporter
	return null


func _try_register_capture() -> void:
	if _has_local_session() and not _capture_registered:
		_capture_registered = true
		EngineDebugger.register_message_capture(
			"networked",
			func(message: String, data: Array) -> bool:
				_on_editor_message(message, data)
				return true
		)


func _init() -> void:
	randomize()
	reporter_id = "%08x" % (randi() % 0xFFFFFFFF)


func _enter_tree() -> void:
	if not _debug_build():
		return

	Netw.dbg.reset()
	NetTrace.message_delegate = func(
		msg: String, payload: Dictionary, mt: MultiplayerTree = null
	) -> void:
		_queue(msg, payload, mt)

	_try_register_capture()

	var cap: int = ProjectSettings.get_setting(
		"debug/networked/telemetry_buffer_size", 120
	)
	_telemetry = NetTelemetryBuffer.new(cap)
	_validators = [TopologyNetValidator.new()]

	_clock_monitor = NetClockMonitor.new()
	_clock_monitor.name = "NetClockMonitor"
	add_child(_clock_monitor)

	_watchdog = ErrorWatchdog.new()
	add_child(_watchdog)
	_watchdog.cpp_error_caught.connect(_on_cpp_error_caught)

	var multi_instance := NetMultiInstance.new()
	multi_instance.name = "NetMultiInstance"
	add_child(multi_instance)


func _exit_tree() -> void:
	if not _debug_build():
		return

	NetTrace.message_delegate = Callable()

	for mt: MultiplayerTree in _trees:
		var event := NetSessionEvent.new()
		event.tree_name = mt.get_tree_name()
		_queue("networked:session_unregistered", event.to_dict(), mt)

	_flush_now()


## Registers a [MultiplayerTree] for debug reporting.
func register_tree(mt: MultiplayerTree) -> void:
	if not _debug_build() or mt in _trees:
		return
		
	var tree_name := mt.get_tree_name()
	_dbg.info(
		"Reporter: [Register] '%s' (is_server=%s, local_editor=%s)" % \
		[tree_name, mt.is_server, _has_local_session()]
	)
	
	_trees.append(mt)
	
	var ctx := NetDebugTreeContext.new(mt, self)
	ctx.name = "NetDebugContext"
	ctx.tree_ready.connect(func() -> void: Netw.dbg.tiling_requested.emit())
	ctx.clock_pong_captured.connect(func(d: Dictionary) -> void: 
		if _clock_monitor:
			_clock_monitor.update_local_clock(mt, d)
		var sample := NetClockSample.from_dict(d, mt.get_tree_name())
		_queue("networked:clock_sample", sample.to_dict(), mt)
	)
	mt.add_child(ctx)
	_debug_contexts[mt] = ctx
	
	report_session_registered(mt)


## Emits a session registration event for the given [param mt].
## [br][br]
## Called by [NetDebugTreeContext] when the tree's authority client changes.
func report_session_registered(mt: MultiplayerTree) -> void:
	if not is_instance_valid(mt):
		return
		
	var backend_class := ""
	if mt.backend and mt.backend.get_script():
		backend_class = mt.backend.get_script().get_global_name()
		
	var event := NetSessionEvent.new()
	event.tree_name = mt.get_tree_name()
	event.username = mt.authority_client.username if mt.authority_client else ""
	event.is_server = mt.is_server
	event.backend_class = backend_class
	event.rid = reporter_id
	event.peer_id = mt.multiplayer_api.get_unique_id() if \
		mt.multiplayer_api else 0
	_queue("networked:session_registered", event.to_dict(), mt)


## Unregisters a [MultiplayerTree] from debug reporting.
func unregister_tree(mt: MultiplayerTree) -> void:
	if mt not in _trees:
		return
		
	_trees.erase(mt)
	var ctx: NetDebugTreeContext = _debug_contexts.get(mt)
	if is_instance_valid(ctx):
		ctx.free()
	_debug_contexts.erase(mt)
	
	var tree_name := mt.get_tree_name()
	_watched.erase(tree_name)
	
	var event := NetSessionEvent.new()
	event.tree_name = tree_name
	event.rid = reporter_id
	event.peer_id = mt.multiplayer_api.get_unique_id() if \
		mt.multiplayer_api else 0
	_queue("networked:session_unregistered", event.to_dict(), mt)
	_flush_now()


#region --- Signal Handlers ----------------------------------------------------


var _sending_manifest: bool = false


func _on_cpp_error_caught(timestamp: int, error_text: String) -> void:
	if not _debug_build() or _sending_manifest:
		return
		
	_sending_manifest = true
	var active := Netw.dbg.active_span()
	var cid_val := str(active.id) if active else "N/A"
	
	var mt: MultiplayerTree = null
	if active and active.get("_mt") is WeakRef:
		mt = (active.get("_mt") as WeakRef).get_ref() as MultiplayerTree
		
	if not is_instance_valid(mt):
		if not _trees.is_empty():
			_dbg.warn(
				"Reporter: [CppError] no active span tree context - " + \
				"attributing to first tree",
				func(m): push_warning(m)
			)
			mt = _trees[0]
		else:
			_dbg.warn(
				"Reporter: [CppError] no active span and no trees - dropping",
				func(m): push_warning(m)
			)
			_sending_manifest = false
			return
			
	var m := NetCppErrorManifest.new()
	_fill_base(m, "CPP_ERROR_LOG_WATCHDOG", cid_val, mt)
	m.timestamp_usec = timestamp
	m.network_state = {
		"tree_name": _active_tree_name(active),
		"peer_id": mt.multiplayer_api.get_unique_id() if \
			is_instance_valid(mt) and mt.multiplayer_api else 0,
	}
	m.errors = [error_text]
	m.error_text = error_text
	
	if is_instance_valid(mt) and mt in _debug_contexts:
		var ctx := _debug_contexts[mt] as NetDebugTreeContext
		m.node_snapshot = ctx.build_crash_snapshot(active)
		
	_send_manifest(m, mt)
	_sending_manifest = false

#endregion


## Returns the [code]tree_name[/code] for the current context.
func _active_tree_name(active_span: RefCounted = null) -> String:
	if active_span:
		var tn: Variant = active_span.get("tree_name")
		if tn is String and not (tn as String).is_empty():
			return tn as String
			
	if not _trees.is_empty():
		var mt: MultiplayerTree = _trees[0]
		return mt.get_tree_name()
		
	return ""


func _on_peer_connected(peer_id: int, mt: MultiplayerTree) -> void:
	var tree_name := mt.get_tree_name()
	var ev := {
		"tree_name": tree_name, 
		"peer_id": peer_id, 
		"event": "connected"
	}
	_cycle_peer_events.append(ev)
	
	var event := NetPeerEvent.new()
	event.tree_name = tree_name
	event.peer_id = peer_id
	_queue("networked:peer_connected", event.to_dict(), mt)
	
	if mt.is_server:
		var span: NetPeerSpan = Netw.dbg.peer_span(
			mt, "peer_connect", [peer_id], {"tree": tree_name}
		)
		span.step("server_received_connect")
		_check_simplify_path_race_on_connect(peer_id, mt, span)


func _on_peer_disconnected(peer_id: int, mt: MultiplayerTree) -> void:
	var tree_name := mt.get_tree_name()
	var ev := {
		"tree_name": tree_name, 
		"peer_id": peer_id, 
		"event": "disconnected"
	}
	_cycle_peer_events.append(ev)
	
	var event := NetPeerEvent.new()
	event.tree_name = tree_name
	event.peer_id = peer_id
	_queue("networked:peer_disconnected", event.to_dict(), mt)
	
	var mt_ref := weakref(mt)
	var timer := get_tree().create_timer(2.0)
	_pending_zombie_checks.append(timer)
	
	timer.timeout.connect(
		func() -> void:
			if timer in _pending_zombie_checks:
				_pending_zombie_checks.erase(timer)
				
			var mt_instance = mt_ref.get_ref()
			if mt_instance:
				_check_zombie_player(peer_id, mt_instance)
	, CONNECT_ONE_SHOT)


## Scans active lobbies for nodes still owned by [param peer_id].
func _check_zombie_player(peer_id: int, mt: MultiplayerTree) -> void:
	if not _debug_build() or not is_instance_valid(mt):
		return
		
	var lm: MultiplayerLobbyManager = mt.get_service(MultiplayerLobbyManager)
	if not lm:
		return
		
	var zombies: Array[String] = []
	for lobby_name: StringName in lm.active_lobbies:
		var lobby: Lobby = lm.active_lobbies[lobby_name]
		if not is_instance_valid(lobby) or not is_instance_valid(lobby.level):
			continue
			
		for node: Node in lobby.level.find_children("*", "Node", true, false):
			if is_instance_valid(node) and \
					node.get_multiplayer_authority() == peer_id:
				zombies.append(str(node.get_path()))
				
	if zombies.is_empty():
		return
		
	var m := NetZombieManifest.new()
	_fill_base(m, "ZOMBIE_PLAYER_DETECTED", "N/A", mt)
	m.network_state["disconnected_peer_id"] = peer_id
	m.errors = zombies
	_send_manifest(m, mt)


## Called by [NetDebugTreeContext] when a lobby spawns.
func _on_lobby_spawned_logic(
	lobby: Lobby, mt: MultiplayerTree
) -> CheckpointToken:
	var tree_name := mt.get_tree_name()
	var event := NetLobbyEvent.new()
	event.tree_name = tree_name
	event.event = "spawned"
	event.lobby_name = str(lobby.level.name)
	_queue("networked:lobby_event", event.to_dict(), mt)

	var lobby_span: NetPeerSpan = null
	if mt.is_server:
		var peers: Array = []
		if mt.multiplayer_api:
			peers = mt.multiplayer_api.get_peers()

		lobby_span = Netw.dbg.peer_span(
			mt, "lobby_spawn", peers,
			{"lobby_name": str(lobby.level.name), "tree": tree_name}
		)
		lobby_span.step("spawners_registering")

	var lobby_token: CheckpointToken = \
		lobby_span.checkpoint() if lobby_span else null
	_check_simplify_path_race_lobby(lobby, mt, lobby_span)
	return lobby_token


## Called by [NetDebugTreeContext] when a player spawns inside a lobby.
func _on_player_spawned_logic(
	player: Node, mt: MultiplayerTree, lobby_token: CheckpointToken
) -> void:
	var tree_name := mt.get_tree_name()
	_send_topology_snapshot(player, mt)
	if mt.is_server:
		var peers: Array = []
		if mt.multiplayer_api:
			peers = Array(mt.multiplayer_api.get_peers())

		var spawn_span: NetSpan = Netw.dbg.peer_span(
			mt, "player_spawn", peers, {
				"player": player.name,
				"tree": tree_name,
			}, lobby_token).with_node(player)
		spawn_span.step("player_entered_tree")
		_check_simplify_path_race_player_spawn(player, mt, spawn_span)
		_check_player_topology_validation(player, mt, spawn_span)


## Called by [NetDebugTreeContext] when a lobby despawns.
func _on_lobby_despawned_logic(lobby: Lobby, mt: MultiplayerTree) -> void:
	var tree_name := mt.get_tree_name()
	var event := NetLobbyEvent.new()
	event.tree_name = tree_name
	event.event = "despawned"
	event.lobby_name = str(lobby.level.name)
	_queue("networked:lobby_event", event.to_dict(), mt)


## Emits a crash manifest when a lobby is spawned on the server while peers are
## already connected.
func _check_simplify_path_race_lobby(
	lobby: Lobby, mt: MultiplayerTree, span: NetPeerSpan
) -> void:
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
	m.network_state["connected_peers"] = \
		mt.multiplayer_api.get_peers() if mt.multiplayer_api else []
	m.errors = _races_to_strings(races)
	m.preflight_snapshot = races
	m.player_name = lobby.level.name
	m.in_tree = lobby.level.is_inside_tree()
	_send_manifest(m, mt)


## Emits a crash manifest when a new peer connects to the server while nodes are
## already registered.
func _check_simplify_path_race_on_connect(
	peer_id: int, mt: MultiplayerTree, span: NetPeerSpan
) -> void:
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
	_send_manifest(m, mt)


## Emits a crash manifest when a player is added to a lobby on the server.
func _check_simplify_path_race_player_spawn(
	player: Node, mt: MultiplayerTree, span: NetSpan
) -> void:
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
	m.network_state["connected_peers"] = \
		mt.multiplayer_api.get_peers() if mt.multiplayer_api else []
	m.errors = _races_to_strings(races)
	m.preflight_snapshot = races
	m.player_name = player.name
	m.in_tree = player.is_inside_tree()
	_send_manifest(m, mt)


## Runs topology validators for a newly spawned player (server-only).
func _check_player_topology_validation(
	player: Node, mt: MultiplayerTree, span: NetSpan
) -> void:
	var errors := _execute_validators(
		"player_spawn", {"player": player, "mt": mt}
	)
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
	
	if is_instance_valid(mt) and mt in _debug_contexts:
		var ctx := _debug_contexts[mt] as NetDebugTreeContext
		m.node_snapshot = ctx.build_crash_snapshot(span)
	else:
		m.node_snapshot = NetNodeSnapshot.from_node(player)
		
	_send_manifest(m, mt)


## Builds and emits a [NetTopologySnapshot] for [param player].
func _send_topology_snapshot(player: Node, mt: MultiplayerTree) -> void:
	if not _debug_build():
		return
	
	var ctx := _debug_contexts.get(mt) as NetDebugTreeContext
	
	# Clients only emit topology for their 'owned' player. 
	# Servers emit for everyone (to populate the server's topology view).
	if not mt.is_server:
		if not is_instance_valid(ctx) or \
				not is_instance_valid(ctx.authority_client) or \
				ctx.authority_client.owner != player:
			return
		
	var tree_name := mt.get_tree_name()
	var snap := NetTopologySnapshot.new()
	snap.tree_name = tree_name
	snap.node_path = str(player.get_path())
	snap.username = player.name.get_slice("|", 0) if "|" in player.name else \
		player.name
	snap.peer_id = player.get_multiplayer_authority()
	snap.is_server = mt.is_server
	snap.lobby_name = player.get_parent().name \
		if is_instance_valid(player.get_parent()) else ""
	
	snap.active_scene = ctx.get_active_scene_path(player) if ctx else "?"
		
	var syncs := SynchronizersCache.get_synchronizers(player)
	
	snap.cache_info = {
		"hit": player.has_meta(SynchronizersCache.META_KEY),
		"hooked": player.has_meta(&"_sc_invalidation_connected")
	}
	
	for sync: MultiplayerSynchronizer in syncs:
		var si := NetTopologySnapshot.SyncInfo.new()
		si.name = sync.name
		si.root_path = str(sync.root_path)
		si.authority = sync.get_multiplayer_authority()
		si.enabled = true
		
		var root := sync.get_node_or_null(sync.root_path)
		
		if sync.replication_config:
			for prop: NodePath in sync.replication_config.get_properties():
				var pi := NetTopologySnapshot.PropInfo.new()
				pi.path = str(prop)
				
				if is_instance_valid(root):
					var res := root.get_node_and_resource(prop)
					var target: Object = res[0]
					var subpath: NodePath = res[2]
					
					if is_instance_valid(target):
						for pinfo in target.get_property_list():
							if pinfo.name == str(subpath):
								pi.type = pinfo.type
								pi.target_class = pinfo.class_name if \
									pi.type == TYPE_OBJECT else \
									type_string(pi.type)
								break
								
						if pi.type == TYPE_NIL:
							var value = target.get_indexed(subpath)
							pi.type = typeof(value)
							pi.target_class = value.get_class() if \
								pi.type == TYPE_OBJECT and \
								is_instance_valid(value) else \
								type_string(pi.type)
								
					if pi.target_class.is_empty():
						pi.target_class = type_string(pi.type)
						
				var config := sync.replication_config
				pi.replication_mode = config.property_get_replication_mode(prop)
				pi.spawn = config.property_get_spawn(prop)
				pi.sync = config.property_get_sync(prop)
				pi.watch = config.property_get_watch(prop)
				si.properties.append(pi)
		snap.synchronizers.append(si)
	emit_debug_event("networked:topology_snapshot", snap.to_dict(), mt)


# --- Snapshot Protocol --------------------------------------------------------


## Re-emits current state for a late-joining editor.
func _on_request_snapshot() -> void:
	if not _debug_build():
		return
	_emit_current_state()


## Emits session and topology snapshots for all active trees.
func _emit_current_state() -> void:
	for mt: MultiplayerTree in _trees:
		var tree_name := mt.get_tree_name()
		var backend_class := ""
		if mt.backend and mt.backend.get_script():
			backend_class = mt.backend.get_script().get_global_name()
			
		var event := NetSessionEvent.new()
		event.tree_name = tree_name
		event.username = mt.authority_client.username if mt.authority_client else ""
		event.is_server = mt.is_server
		event.backend_class = backend_class
		event.rid = reporter_id
		event.peer_id = mt.multiplayer_api.get_unique_id() if \
			mt.multiplayer_api else 0
		_queue("networked:session_registered", event.to_dict(), mt)
		
		var ctx := _debug_contexts.get(mt) as NetDebugTreeContext
		if not is_instance_valid(ctx):
			continue
			
		if mt.is_server:
			# Server sends topology for all active players.
			for player in mt.get_all_players():
				_send_topology_snapshot(player, mt)
		elif is_instance_valid(ctx.authority_client) and \
				is_instance_valid(ctx.authority_client.owner):
			# Client only sends its own.
			_send_topology_snapshot(ctx.authority_client.owner, mt)


# --- Demand-Driven Replication Watch ------------------------------------------


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
	for sync: MultiplayerSynchronizer in \
			SynchronizersCache.get_synchronizers(node):
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


func _send_replication_snapshot(
	node: Node, sync: MultiplayerSynchronizer, mt: MultiplayerTree
) -> void:
	if not is_instance_valid(node) or not is_instance_valid(sync):
		return
		
	var tree_name := mt.get_tree_name()
	var snapshot := NetReplicationSnapshot.new()
	snapshot.tree_name = tree_name
	snapshot.node_path = str(node.get_path())
	snapshot.properties = _collect_properties(node, sync)
	_queue("networked:replication_snapshot", snapshot.to_dict(), mt)


func _send_full_replication_snapshot(node: Node, mt: MultiplayerTree) -> void:
	var all_props: Dictionary = {}
	var inventory: Array = []
	for sync: MultiplayerSynchronizer in \
			SynchronizersCache.get_synchronizers(node):
		all_props.merge(_collect_properties(node, sync))
		inventory.append({
			"name": sync.name,
			"authority": sync.get_multiplayer_authority(),
			"root_path": str(sync.root_path),
		})
		
	var tree_name := mt.get_tree_name()
	var snapshot := NetReplicationSnapshot.new()
	snapshot.tree_name = tree_name
	snapshot.node_path = str(node.get_path())
	snapshot.properties = all_props
	snapshot.inventory = inventory
	_queue("networked:replication_snapshot", snapshot.to_dict(), mt)


func _collect_properties(
	node: Node, sync: MultiplayerSynchronizer
) -> Dictionary:
	var props: Dictionary = {}
	if not sync.replication_config:
		return props
		
	var root_node: Node = null
	if sync.root_path != NodePath("."):
		root_node = sync.get_node(sync.root_path)
	else:
		root_node = sync.get_parent()
		
	if not is_instance_valid(root_node):
		return props
		
	for prop_path: NodePath in sync.replication_config.get_properties():
		var s := str(prop_path)
		var colon := s.rfind(":")
		if colon < 0:
			continue
			
		var node_part := s.substr(0, colon)
		var prop_name := s.substr(colon + 1)
		var target: Node = null
		if node_part.is_empty() or node_part == ".":
			target = root_node
		else:
			target = root_node.get_node_or_null(node_part)
			
		if is_instance_valid(target):
			var val: Variant = target.get(prop_name)
			if typeof(val) not in [
				TYPE_OBJECT, TYPE_RID, TYPE_CALLABLE, TYPE_SIGNAL
			]:
				props[s] = val
				
	return props


## Formats Godot's raw [code]error[/code] debugger message.
func _format_cpp_error(data: Array) -> String:
	var parts: PackedStringArray = []
	var prefix := "WARNING" if data.size() > 5 and data[5] else "ERROR"
	var code: String = str(data[3]) if data.size() > 3 else ""
	var descr: String = str(data[4]) if data.size() > 4 else ""
	var func_: String = str(data[0]) if data.size() > 0 else ""
	var file_: String = str(data[1]) if data.size() > 1 else ""
	var line_: int = int(data[2]) if data.size() > 2 else 0
	
	if not code.is_empty():
		parts.append("%s: %s" % [prefix, code])
	if not descr.is_empty():
		parts.append("  %s" % descr)
	if not func_.is_empty():
		parts.append("  at: %s (%s:%d)" % [func_, file_, line_])
		
	return "\n".join(parts) if not parts.is_empty() else str(data)


static var active_visualizers: Dictionary = {}


static func is_visualizer_enabled(_viz_name: String) -> bool:
	return false


static func get_peer_debug_color(peer_id: int) -> Color:
	var phi_conj := 0.618033988749895
	var hue := fmod(float(abs(peer_id)) * phi_conj, 1.0)
	return Color.from_hsv(hue, 0.6, 0.9)


# --- Incoming Editor Messages -------------------------------------------------


func _on_editor_message(message: String, data: Array) -> void:
	if data.is_empty():
		_dbg.warn("Reporter: [EditorMessage] '%s' received with empty " + \
			"payload." % [message], func(m: String) -> void: push_warning(m))
		return
		
	match message:
		"tiling_update":
			Netw.dbg.tiling_requested.emit()
		"watch_node":
			_handle_watch_node(data[0])
		"unwatch_node":
			_handle_unwatch_node(data[0])
		"set_auto_break":
			_auto_break = true if data[0] else false
		"cpp_error_caught":
			_on_cpp_error_caught(Time.get_ticks_usec(), _format_cpp_error(data))
		"request_snapshot":
			_on_request_snapshot()
		"inspect_node":
			_handle_inspect_node(data[0])
		"visualizer_toggle":
			_handle_visualizer_toggle(data[0])
		"remote_clock_sample":
			if _clock_monitor:
				_clock_monitor.update_relayed_clock(NetEnvelope.from_dict(data[0]))
		"remote_session_unregistered":
			if _clock_monitor:
				_clock_monitor.remove_relayed_clock(NetEnvelope.from_dict(data[0]))


## Forwards a node inspection request from the editor.
func _handle_inspect_node(d: Variant) -> void:
	if not EngineDebugger.is_active() or not is_inside_tree():
		return
	
	var path: String = ""
	var pid: int = 0
	
	if d is String:
		path = d
	elif d is Dictionary:
		path = d.get("node_path", "")
		pid = d.get("peer_id", 0)
		
	var node: Node = get_tree().root.get_node_or_null(path)
	
	# If path lookup failed, try finding the player node by peer_id.
	if not is_instance_valid(node) and pid != 0:
		for mt in _trees:
			for player in mt.get_all_players():
				if player.get_multiplayer_authority() == pid:
					node = player
					break
			if node:
				break
				
	if is_instance_valid(node):
		var snapshot := [node.get_instance_id(), node.get_class(), []]
		EngineDebugger.send_message("remote_objects_selected", [snapshot])


func _handle_visualizer_toggle(d: Dictionary) -> void:
	# Visualizer toggles (like nameplates) are local debug session preferences.
	# We apply them to every MultiplayerTree in this process so they manifest
	# on whatever view the user is looking at.
	for context in _debug_contexts.values():
		context.apply_command(d)


# --- Message Queue ------------------------------------------------------------


func _queue(
	msg_name: String, data: Dictionary, mt: MultiplayerTree = null
) -> void:
	_message_queue.append([msg_name, data, mt])
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
		
	var mt: MultiplayerTree = null
	if not _trees.is_empty():
		mt = _trees[0]

	if _telemetry:
		var active_span := Netw.dbg.active_span()
		var cid_trail: Array = [str(active_span.id)] if active_span else ["N/A"]
		_telemetry.record(
			Engine.get_process_frames(),
			cid_trail,
			[],
			_cycle_peer_events,
			{},
		)
	_cycle_peer_events.clear()
	
	for entry: Array in _message_queue:
		var entry_mt = entry[2] if entry.size() >= 3 else null
		
		if not is_instance_valid(entry_mt):
			if not mt:
				_dbg.warn(
					"Reporter: [QueueDrop] '%s' - no tree context", [entry[0]]
				)
				continue
			entry_mt = mt

		emit_debug_event(entry[0], entry[1], entry_mt)
	_message_queue.clear()


# --- Telemetry Helpers --------------------------------------------------------


## Freezes the ring buffer and returns its snapshot.
func _freeze_and_slice() -> Array:
	if _telemetry:
		_telemetry.freeze()
		return _telemetry.snapshot(20)
	return []


## Single dispatch point for all crash manifest emissions.
func _send_manifest(manifest: NetManifest, mt: MultiplayerTree = null) -> void:
	if _is_sending_manifest:
		return
	_is_sending_manifest = true
	
	var now := Time.get_ticks_msec()
	var t := manifest.trigger
	
	if now - _last_manifest_sec_msec.get(t, 0) > 1000:
		_manifest_count_sec[t] = 0
		_last_manifest_sec_msec[t] = now
	if now - _last_manifest_min_msec.get(t, 0) > 60000:
		_manifest_count_min[t] = 0
		_last_manifest_min_msec[t] = now
		
	_manifest_count_sec[t] = _manifest_count_sec.get(t, 0) + 1
	_manifest_count_min[t] = _manifest_count_min.get(t, 0) + 1
	
	if _manifest_count_sec[t] > 3 or _manifest_count_min[t] > 10:
		_dbg.error(
			"Reporter: [RateLimit] Manifest blocked for trigger: %s", [t]
		)
		_maybe_break()
		_is_sending_manifest = false
		return
		
	manifest.validate_contract()
	var payload := manifest.to_dict()
	_dbg.info(
		"Reporter: [SendManifest] %s (cid=%s)" % \
		[manifest.trigger, manifest.cid]
	)
	
	var target_mt := mt
	if not is_instance_valid(target_mt) and manifest._mt:
		target_mt = manifest._mt.get_ref() as MultiplayerTree
		
	if not is_instance_valid(target_mt):
		_dbg.warn(
			"Reporter: [ManifestDrop] '%s' - no valid tree",
			[manifest.trigger]
		)
		_maybe_break()
		_is_sending_manifest = false
		return
		
	if _has_local_session():
		emit_debug_event("networked:crash_manifest", payload, target_mt)
	else:
		push_error("[NETWORKED] %s | scene=%s peer=%d errors=%s" % [
			manifest.trigger,
			manifest.active_scene,
			manifest.network_state.get("peer_id", 0),
			str(payload.get("errors", [])),
		])
		
	_maybe_break()
	_is_sending_manifest = false


## Fills the common base fields of [param m] from engine state.
func _fill_base(
	m: NetManifest, trigger: String, cid_val: String, mt: MultiplayerTree
) -> void:
	m.trigger = trigger
	m.cid = cid_val
	m._mt = weakref(mt) if is_instance_valid(mt) else null
	m.cid_timeline = [cid_val]
	m.frame = Engine.get_process_frames()
	m.timestamp_usec = Time.get_ticks_usec()
	
	var ctx := _debug_contexts.get(mt) as NetDebugTreeContext
	m.active_scene = ctx.get_active_scene_path() if ctx else "?"
		
	m.network_state = {
		"is_server": mt.is_server if is_instance_valid(mt) else false,
		"tree_name": mt.get_tree_name() if \
			is_instance_valid(mt) else "",
		"peer_id": mt.multiplayer_api.get_unique_id() if \
			is_instance_valid(mt) and mt.multiplayer_api else 0,
	}
	m.telemetry_slice = _freeze_and_slice()


## Runs all registered validators for [param trigger].
func _execute_validators(trigger: String, ctx: Dictionary) -> Array[String]:
	for phase in [
		NetValidator.STRUCTURAL, 
		NetValidator.LOGICAL, 
		NetValidator.HEAVY_HEURISTIC
	]:
		var phase_errors: Array[String] = []
		for v: NetValidator in _validators:
			if v.phase == phase:
				phase_errors.append_array(v.execute(trigger, ctx))
				
		if not phase_errors.is_empty():
			return phase_errors
	return []


static func _races_to_strings(races: Array[Dictionary]) -> Array[String]:
	var out: Array[String] = []
	for r: Dictionary in races:
		var p := r.get("rel_path", r.get("path", "?"))
		out.append("simplify_path race on %s" % [p])
	return out


## Pauses the engine if break is enabled.
func _maybe_break() -> void:
	if EngineDebugger.is_active() and _auto_break:
		breakpoint


# --- Unified send -------------------------------------------------------------


## Single dispatch point for all outgoing debug messages.
func emit_debug_event(
	msg: String, data: Dictionary, mt: MultiplayerTree
) -> void:
	if not _debug_build() or not is_instance_valid(mt):
		return
		
	var envelope := NetEnvelope.from_mt(mt, msg, data, reporter_id)
	var bytes := var_to_bytes(envelope.to_dict())
	
	if _has_local_session():
		_trace_emit("[EmitDirect]", msg, "(path=%s)" % [envelope.source_path])
		EngineDebugger.send_message("networked:envelope", [bytes])
	else:
		_dbg.warn(
			"Reporter: [EmitDropped] %s - no active debugger session", [msg]
		)


func _trace_emit(prefix: String, msg: String, extra: String = "") -> void:
	if msg == "networked:clock_sample" or msg.begins_with("networked:span_"):
		return
		
	if extra:
		_dbg.trace("Reporter: %s %s %s" % [prefix, msg, extra])
	else:
		_dbg.trace("Reporter: %s %s" % [prefix, msg])


# --- Guards -------------------------------------------------------------------


## True in editor runs and exported debug builds.
static func _debug_build() -> bool:
	if not _reporting_checked:
		_reporting_checked = true
		_reporting_enabled = true
		
		if Netw.is_test_env():
			_reporting_enabled = false
		else:
			var args := OS.get_cmdline_args()
			for arg in args:
				if arg == "--headless":
					_reporting_enabled = false
					break
					
	return _reporting_enabled and OS.has_feature("debug")


## True when this process has a live [EngineDebugger] connection.
static func _has_local_session() -> bool:
	return EngineDebugger.is_active()
