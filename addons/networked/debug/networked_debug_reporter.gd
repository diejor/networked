## Game-side debug telemetry reporter for the Networked debugger plugin.
##
## This is a singleton (Autoload) node that collects telemetry from all active
## [MultiplayerTree] instances in the process and forwards them to the editor.
##
## All operations are guarded by [method _debug_build], zero overhead in
## release exports or headless/test runs. The local [EngineDebugger] send path
## is additionally gated by [method _has_local_session].
extends Node
class_name NetworkedDebugReporter

# Cached once per process: whether reporting is fundamentally allowed at all.
static var _reporting_enabled: bool = false
static var _reporting_checked: bool = false

## Explicitly enables or disables debug reporting.
## Useful in tests to override the default auto-detection.
static func set_enabled(enabled: bool) -> void:
	_reporting_enabled = enabled
	_reporting_checked = true
	# If we are enabling mid-run, ensure we try to register capture if possible.
	var reporter = _get_instance()
	if reporter and enabled:
		reporter._try_register_capture()


## Property-style access for the singleton instance.
## [code]NetworkedDebugger.enabled = true[/code]
var enabled: bool:
	get: return _debug_build()
	set(value): set_enabled(value)


## Resets all debug registries, history, and telemetry.
## Call this between tests to ensure a clean slate.
func reset_state() -> void:
	# Force re-detection of environment on next check.
	_reporting_checked = false
	_reporting_enabled = false

	# Free debug context nodes that are still alive (e.g. when reset is called
	# before a queued free processes). Each context's _exit_tree cleans up its
	# own spawner / lobby-sync hooks.
	for ctx in _debug_contexts.values():
		if is_instance_valid(ctx):
			ctx.free()
	_debug_contexts.clear()
	_trees.clear()
	_relays.clear()

	_process_recipients.clear()
	_recipient_map.clear()
	_message_queue.clear()
	_cycle_peer_events.clear()
	_crash_history.clear()
	_watched.clear()

	if _telemetry:
		_telemetry.clear()
	NetTrace.reset()
	NetLog.trace("Reporter: State reset (deep).")


static func _get_instance() -> NetworkedDebugReporter:
	return Engine.get_singleton("NetworkedDebugger") as NetworkedDebugReporter


func _try_register_capture() -> void:
	if _has_local_session() and not _capture_registered:
		_capture_registered = true
		EngineDebugger.register_message_capture(
			"networked",
			func(message: String, data: Array) -> bool:
				_on_editor_message(message, data)
				return true
		)


# Tracks whether the EngineDebugger capture has been registered for this process.
static var _capture_registered: bool = false

# Static registry of debug recipient peer IDs (shared across all trees in this process).
var _process_recipients: Array[int] = []

# Static registry of debug recipient reporter IDs: reporter_id -> current_peer_id.
# Shared across all trees in this process to deduplicate multi-client relay echos.
var _recipient_map: Dictionary = {}

# Unique ID for this reporter instance; used to deduplicate echos in the editor.
var reporter_id: String = ""

var _trees: Array[MultiplayerTree] = []

# Map of MultiplayerTree -> NetDebugRelay.
# Each tree must have its own relay child so that RPCs are correctly routed
# through that tree's specific MultiplayerAPI root.
var _relays: Dictionary = {}

# Map of MultiplayerTree -> NetDebugTreeContext (the per-tree signal wiring node).
var _debug_contexts: Dictionary = {}

var _message_queue: Array = []
var _flush_pending: bool = false

# Tree Name -> NodePath -> Array of {sync, callable} for demand-driven replication watch.
var _watched: Dictionary = {}


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

# Ring buffer of crash manifests emitted this session — replayed to late-joining editors.
# Bounded to prevent runaway memory and large replay payloads.
var _crash_history: Array[Dictionary] = []
const CRASH_HISTORY_CAP := 20


func _init() -> void:
	randomize()
	reporter_id = "%08x" % (randi() % 0xFFFFFFFF)
	NetLog.debug("Reporter: Started with ID %s" % reporter_id)


func _enter_tree() -> void:
	if not _debug_build():
		return

	# Clear any stale spans left over from a previous run.
	NetTrace.reset()
	# Route span tracing through the unified queue so spans are correctly 
	# buffered until registration flushes.
	NetTrace.message_delegate = func(msg: String, payload: Dictionary, mt: MultiplayerTree = null) -> void:
		_queue(msg, payload, mt)

	# The message capture (editor → game commands) only makes sense when a local
	# editor session exists. Skip registration on headless / relay-only processes.
	_try_register_capture()

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
	
	for mt: MultiplayerTree in _trees:
		var event := NetSessionEvent.new()
		event.tree_name = mt.get_meta(&"_original_name", mt.name)
		_queue("networked:session_unregistered", event.to_dict(), mt)
	_flush_now()


## Registers a [MultiplayerTree] for debug reporting.
func register_tree(mt: MultiplayerTree) -> void:
	if not _debug_build():
		return
	if mt in _trees:
		return
	
	var tree_name := mt.get_meta(&"_original_name", mt.name)
	NetLog.info("Reporter: [Register] '%s' (is_server=%s, local_editor=%s)" % [tree_name, mt.is_server, _has_local_session()])

	_trees.append(mt)

	var ctx := NetDebugTreeContext.new(mt, self)
	ctx.name = "NetDebugContext"
	mt.add_child(ctx)
	_debug_contexts[mt] = ctx

	_setup_relay(mt)

	var backend_class := ""
	if mt.backend and mt.backend.get_script():
		backend_class = mt.backend.get_script().get_global_name()

	var event := NetSessionEvent.new()
	event.tree_name = tree_name
	event.is_server = mt.is_server
	event.backend_class = backend_class
	event.rid = reporter_id
	event.peer_id = mt.multiplayer_api.get_unique_id() if mt.multiplayer_api else 0
	_queue("networked:session_registered", event.to_dict(), mt)


## Unregisters a [MultiplayerTree] from debug reporting.
func unregister_tree(mt: MultiplayerTree) -> void:
	if mt not in _trees:
		return

	_trees.erase(mt)
	_relays.erase(mt)
	_debug_contexts.erase(mt)

	var tree_name := mt.get_meta(&"_original_name", mt.name)
	_watched.erase(tree_name)

	var event := NetSessionEvent.new()
	event.tree_name = tree_name
	event.rid = reporter_id
	event.peer_id = mt.multiplayer_api.get_unique_id() if mt.multiplayer_api else 0
	_queue("networked:session_unregistered", event.to_dict(), mt)
	_flush_now()


# ─── Signal Handlers ──────────────────────────────────────────────────────────

var _sending_manifest: bool = false


func _on_cpp_error_caught(timestamp: int, error_text: String) -> void:
	if not _debug_build() or _sending_manifest:
		return

	_sending_manifest = true
	var active := NetTrace.active_span()
	var cid_val := str(active.id) if active else "N/A"

	# Resolve tree from the active span's _mt WeakRef (set by NetTrace.begin).
	# If no span is active, fall back to the first registered tree with a warning.
	var mt: MultiplayerTree = null
	if active and active.get("_mt") is WeakRef:
		mt = (active.get("_mt") as WeakRef).get_ref() as MultiplayerTree
	if not is_instance_valid(mt):
		if not _trees.is_empty():
			NetLog.warn("Reporter: [CppError] no active span with tree context — attributing to first tree")
			mt = _trees[0]
		else:
			NetLog.warn("Reporter: [CppError] no active span and no trees — dropping")
			_sending_manifest = false
			return

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
	_sending_manifest = false


## Returns the tree_name for the current context: prefers the active span's tree,
## falls back to the first registered tree, falls back to empty string.
func _active_tree_name(active_span: RefCounted = null) -> String:
	if active_span:
		var tn: Variant = active_span.get("tree_name")
		if tn is String and not (tn as String).is_empty():
			return tn as String
	if not _trees.is_empty():
		# Intentional informational fallback — display only, not used for routing.
		var mt: MultiplayerTree = _trees[0]
		return mt.get_meta(&"_original_name", mt.name)
	return ""


func _on_clock_pong(data: Dictionary, mt: MultiplayerTree) -> void:
	var tree_name := mt.get_meta(&"_original_name", mt.name)
	_queue("networked:clock_sample", NetClockSample.from_dict(data, tree_name).to_dict(), mt)


func _on_peer_connected(peer_id: int, mt: MultiplayerTree) -> void:
	var tree_name := mt.get_meta(&"_original_name", mt.name)
	var ev := {"tree_name": tree_name, "peer_id": peer_id, "event": "connected"}
	_cycle_peer_events.append(ev)

	var event := NetPeerEvent.new()
	event.tree_name = tree_name
	event.peer_id = peer_id
	_queue("networked:peer_connected", event.to_dict(), mt)

	if mt.is_server:
		var span: NetSpan = NetTrace.begin_peer("peer_connect", [peer_id], mt, {"tree": tree_name})
		span.step("server_received_connect")
		_check_simplify_path_race_on_connect(peer_id, mt, span)
	elif peer_id == 1 and _has_local_session():
		# Client with editor: register on the server relay now that we are connected.
		var relay: NetDebugRelay = _relays.get(mt)
		if relay:
			var token: String = ProjectSettings.get_setting("networked/debug/relay_token", "")
			NetLog.info("Reporter: [RelayRegister] Client connected to server; registering via RPC")
			relay.register_as_recipient.rpc_id(1, token, reporter_id)


func _on_peer_disconnected(peer_id: int, mt: MultiplayerTree) -> void:
	var tree_name := mt.get_meta(&"_original_name", mt.name)
	var ev := {"tree_name": tree_name, "peer_id": peer_id, "event": "disconnected"}
	_cycle_peer_events.append(ev)

	var event := NetPeerEvent.new()
	event.tree_name = tree_name
	event.peer_id = peer_id
	_queue("networked:peer_disconnected", event.to_dict(), mt)

	if mt.is_server and _relays.has(mt):
		_relays[mt].deregister_recipient(peer_id)

	# Notify the local editor that all relay-forwarded (remote) peers are gone.
	# Covers the case where the server crashes ungracefully while a client's editor
	# is watching: no session_unregistered arrives, so the session must infer it.
	if peer_id == 1 and _has_local_session():
		EngineDebugger.send_message("networked:relay_disconnected", [true])
	
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
	if not is_instance_valid(mt):
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
			if is_instance_valid(node) and node.get_multiplayer_authority() == peer_id:
				zombies.append(str(node.get_path()))
	if zombies.is_empty():
		return

	var m := NetZombieManifest.new()
	_fill_base(m, "ZOMBIE_PLAYER_DETECTED", "N/A", mt)
	m.network_state["disconnected_peer_id"] = peer_id
	m.errors = zombies
	_send_manifest(m)


## Called by [NetDebugTreeContext] when a lobby spawns.
## Queues the lobby event and creates the span / race check.
## Returns the [CheckpointToken] for player-spawn causal linking (may be null).
func _on_lobby_spawned_logic(lobby: Lobby, mt: MultiplayerTree) -> CheckpointToken:
	var event := NetLobbyEvent.new()
	event.tree_name = mt.name
	event.event = "spawned"
	event.lobby_name = str(lobby.level.name)
	_queue("networked:lobby_event", event.to_dict(), mt)

	var lobby_span: NetPeerSpan = null
	if mt.is_server:
		lobby_span = NetTrace.begin_peer("lobby_spawn",
			mt.multiplayer_api.get_peers() if mt.multiplayer_api else [],
			mt, {"lobby_name": str(lobby.level.name), "tree": mt.name})
		lobby_span.step("spawners_registering")
	# Capture token before _check_simplify_path_race_lobby closes/fails the span,
	# so player_spawn spans can declare the causal link.
	var lobby_token: CheckpointToken = lobby_span.checkpoint() if lobby_span else null
	_check_simplify_path_race_lobby(lobby, mt, lobby_span)
	return lobby_token


## Called by [NetDebugTreeContext] when a player spawns inside a lobby.
func _on_player_spawned_logic(player: Node, mt: MultiplayerTree, lobby_token: CheckpointToken) -> void:
	_send_topology_snapshot(player, mt)  # always: server and client
	if mt.is_server:
		var peers: Array = Array(mt.multiplayer_api.get_peers()) if mt.multiplayer_api else []
		var spawn_span: NetSpan = NetTrace.begin_peer("player_spawn", peers, mt, {
			"player": player.name,
			"tree": mt.name,
		}, "", lobby_token)
		spawn_span.step("player_entered_tree")
		_check_simplify_path_race_player_spawn(player, mt, spawn_span)
		_check_player_topology_validation(player, mt, spawn_span)


## Called by [NetDebugTreeContext] when a lobby despawns.
## Spawner / synchronizer cleanup is handled by the context itself.
func _on_lobby_despawned_logic(lobby: Lobby, mt: MultiplayerTree) -> void:
	var event := NetLobbyEvent.new()
	event.tree_name = mt.name
	event.event = "despawned"
	event.lobby_name = str(lobby.level.name)
	_queue("networked:lobby_event", event.to_dict(), mt)




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


## Runs topology validators for a newly spawned player (server-only).
##
## Runs all registered [NetValidator]s for the [code]"player_spawn"[/code] trigger
## in phase order. On failure, emits a [NetTopologyManifest] with the error list
## and a [NetNodeSnapshot] of the player node.
## The topology snapshot itself is sent unconditionally by the caller.
func _check_player_topology_validation(player: Node, mt: MultiplayerTree, span: NetSpan) -> void:
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
	var tree_name := mt.get_meta(&"_original_name", mt.name)
	var snap := NetTopologySnapshot.new()
	snap.tree_name = tree_name
	snap.node_path = str(player.get_path())
	snap.username = player.name.get_slice("|", 0) if "|" in player.name else player.name
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
	emit_debug_event("networked:topology_snapshot", snap.to_dict(), mt)



# ─── Snapshot Protocol ────────────────────────────────────────────────────────

## Re-emits current state for a late-joining editor.
## Called when the editor sends [code]networked:request_snapshot[/code].
## The relay stays stateless; this generates a fresh snapshot on demand.
func _on_request_snapshot() -> void:
	if not _debug_build():
		return
	
	_emit_current_state()

	for mt: MultiplayerTree in _trees:
		# Propagate the request to the rest of the network.
		var relay: NetDebugRelay = _relays.get(mt)
		if is_instance_valid(relay) and relay.is_relay_active():
			if mt.is_server:
				# Server: tell all clients to send their snapshots.
				relay.broadcast_snapshot_request()
			elif _has_local_session():
				# Client with editor: ask the server to re-emit its current state.
				# (Re-registering is idempotent and won't trigger a snapshot if already registered.)
				relay.request_snapshot_from_server.rpc_id(1)


## Called by the [NetDebugRelay] when a remote peer requests a snapshot.
## Only emits local state; does NOT propagate further to prevent recursion.
func _on_remote_snapshot_request() -> void:
	if not _debug_build():
		return
	_emit_current_state()


## Emits [code]session_registered[/code] and [code]topology_snapshot[/code] for all 
## active trees and players.
func _emit_current_state() -> void:
	for mt: MultiplayerTree in _trees:
		# Re-announce the tree so the editor registers the peer.
		var tree_name := mt.get_meta(&"_original_name", mt.name)
		var backend_class := ""
		if mt.backend and mt.backend.get_script():
			backend_class = mt.backend.get_script().get_global_name()
		var event := NetSessionEvent.new()
		event.tree_name = tree_name
		event.is_server = mt.is_server
		event.backend_class = backend_class
		event.rid = reporter_id
		event.peer_id = mt.multiplayer_api.get_unique_id() if mt.multiplayer_api else 0
		_queue("networked:session_registered", event.to_dict(), mt)

		# Re-emit topology for every player currently in any lobby.
		var lm: MultiplayerLobbyManager = mt.get_service(MultiplayerLobbyManager)
		if is_instance_valid(lm):
			for lobby: Lobby in lm.active_lobbies.values():
				if not is_instance_valid(lobby) or not is_instance_valid(lobby.level):
					continue
				var comps := lobby.level.find_children("*", "ClientComponent", true, false)
				for comp: Node in comps:
					if is_instance_valid(comp.owner):
						_send_topology_snapshot(comp.owner, mt)

	# Replay crash history so late-joining editors see manifests from before they connected.
	# The editor-side session deduplicates by cid so live events never double-appear.
	for entry: Dictionary in _crash_history:
		var ref: WeakRef = entry.get("tree") as WeakRef
		var target_mt: MultiplayerTree = (ref.get_ref() as MultiplayerTree) if ref else null
		if not is_instance_valid(target_mt):
			NetLog.warn("Reporter: [ReplaySkip] crash history entry skipped — tree freed")
			continue
		emit_debug_event("networked:crash_manifest", entry.get("payload", {}), target_mt)


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

	var tree_name := mt.get_meta(&"_original_name", mt.name)
	var snapshot := NetReplicationSnapshot.new()
	snapshot.tree_name = tree_name
	snapshot.node_path = str(node.get_path())
	snapshot.properties = _collect_properties(node, sync)
	_queue("networked:replication_snapshot", snapshot.to_dict(), mt)

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
	
	var tree_name := mt.get_meta(&"_original_name", mt.name)
	var snapshot := NetReplicationSnapshot.new()
	snapshot.tree_name = tree_name
	snapshot.node_path = str(node.get_path())
	snapshot.properties = all_props
	snapshot.inventory = inventory
	_queue("networked:replication_snapshot", snapshot.to_dict(), mt)


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


static var active_visualizers: Dictionary = {}

static func is_visualizer_enabled(_viz_name: String) -> bool:
	return false

static func get_peer_debug_color(peer_id: int) -> Color:
	var phi_conj := 0.618033988749895
	var hue := fmod(float(abs(peer_id)) * phi_conj, 1.0)
	return Color.from_hsv(hue, 0.6, 0.9)


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
		"request_snapshot":    _on_request_snapshot()
		"inspect_node":        _handle_inspect_node(data[0])
		"visualizer_toggle":   _handle_visualizer_toggle(data[0])


## Forwards a node inspection request from the editor to Godot's built-in
## Remote Scene Tree. Resolves the string path and sends the
## [code]remote_objects_selected[/code] command.
func _handle_inspect_node(node_path: String) -> void:
	if not EngineDebugger.is_active() or not is_inside_tree():
		return

	var node: Node = get_tree().root.get_node_or_null(node_path)
	if is_instance_valid(node):
		var snapshot := [node.get_instance_id(), node.get_class(), []]
		EngineDebugger.send_message("remote_objects_selected", [snapshot])


func _handle_visualizer_toggle(d: Dictionary) -> void:
	var pk: String = d.get("peer_key", "")
	var mt: MultiplayerTree = null

	for t in _trees:
		var key := "%s|%s" % [str(t.get_path()), reporter_id]
		if key == pk:
			mt = t
			break

	if mt and mt in _debug_contexts:
		var ctx := _debug_contexts[mt] as NetDebugTreeContext
		ctx.apply_command(d)

# ─── Message Queue ────────────────────────────────────────────────────────────

func _queue(msg_name: String, data: Dictionary, mt: MultiplayerTree = null) -> void:
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
		var entry_mt: MultiplayerTree = entry[2] if entry.size() >= 3 else null
		
		# Safety Drop: messages without a tree context are orphaned and dropped.
		if not is_instance_valid(entry_mt):
			NetLog.warn("Reporter: [QueueDrop] '%s' — no tree context" % entry[0])
			continue

		emit_debug_event(entry[0], entry[1], entry_mt)
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
func _send_manifest(manifest: NetManifest, mt: MultiplayerTree = null) -> void:
	var payload := manifest.to_dict()

	var target_mt := mt
	if not is_instance_valid(target_mt):
		NetLog.warn("Reporter: [ManifestDrop] '%s' — no valid tree" % manifest.trigger)
		_maybe_break()
		return

	if _has_local_session() or _relay_active(target_mt):
		emit_debug_event("networked:crash_manifest", payload, target_mt)
	else:
		push_error("[NETWORKED] %s | scene=%s peer=%d errors=%s" % [
			manifest.trigger,
			manifest.active_scene,
			manifest.network_state.get("peer_id", 0),
			str(payload.get("errors", [])),
		])

	# Keep a bounded history so late-joining editors can see crashes that
	# happened before they connected. Replayed in _emit_current_state().
	_crash_history.append({
		"payload": payload,
		"tree": weakref(target_mt) if is_instance_valid(target_mt) else null,
	})
	if _crash_history.size() > CRASH_HISTORY_CAP:
		_crash_history.pop_front()

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
		"tree_name": mt.get_meta(&"_original_name", mt.name) if is_instance_valid(mt) else "",
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
## Two mutually exclusive paths — no echoes by design:
## [br]
## - Direct: fires immediately when a live [EngineDebugger] TCP connection exists.
##   The reporter sends its own events directly to its editor.
## [br]
## - Relay: fires when the relay is active. Server calls [method NetDebugRelay.dispatch_to_recipients]
##   to fan-out to remote editors only (skipping local; already delivered above).
##   Clients call [method NetDebugRelay.forward_to_relay] so the server relay
##   delivers to the server's editor and any other registered remote editors.
func emit_debug_event(msg: String, data: Dictionary, mt: MultiplayerTree) -> void:
	if not _debug_build() or not is_instance_valid(mt):
		return

	var envelope := NetEnvelope.from_mt(mt, msg, data, reporter_id)
	var bytes := var_to_bytes(envelope.to_dict())

	# Path 1: Direct to local editor (zero round-trip, no echo from relay).
	if _has_local_session():
		NetLog.trace("Reporter: [EmitDirect] %s (path=%s)" % [msg, envelope.source_path])
		EngineDebugger.send_message("networked:envelope", [bytes])

	# Path 2: Relay path for remote editors.
	if not _relay_active(mt):
		if not _has_local_session():
			NetLog.warn("Reporter: [EmitDropped] %s — no relay or local session" % msg)
		return

	var relay: NetDebugRelay = _relays[mt]
	if mt.is_server:
		# Server's editor already received it above; only fan-out to remote recipients.
		NetLog.trace("Reporter: [EmitRelayServer] %s" % msg)
		relay.dispatch_to_recipients(bytes)
	else:
		# Relay decides whether to deliver locally based on _is_local_peer — always use one RPC.
		NetLog.trace("Reporter: [EmitRelayClient] %s" % msg)
		relay.forward_to_relay.rpc_id(1, bytes)


# ─── Guards ───────────────────────────────────────────────────────────────────

## True in editor runs and exported debug builds; false in release.
## Gates all debug overhead: relay setup, hook registration, telemetry collection.
static func _debug_build() -> bool:
	if not _reporting_checked:
		_reporting_checked = true
		_reporting_enabled = true
		# By default, disable in headless or unit test runs to avoid noise/overhead.
		# Can be explicitly enabled via set_enabled(true) or .enabled = true.
		var args := OS.get_cmdline_args()
		for arg in args:
			if arg == "--gdunit" or arg == "--headless" or "GdUnitTestRunner.tscn" in arg:
				_reporting_enabled = false
				break
	return _reporting_enabled and OS.has_feature("debug")


## True when this process has a live EngineDebugger TCP connection to an editor.
## Gates only the direct [method EngineDebugger.send_message] calls.
static func _has_local_session() -> bool:
	return EngineDebugger.is_active()


## True when [param mt] has an active relay node.
func _relay_active(mt: MultiplayerTree) -> bool:
	return _relays.has(mt) and is_instance_valid(_relays[mt]) and _relays[mt].is_relay_active()


# ─── Relay setup ──────────────────────────────────────────────────────────────

## Creates a unique relay node for the given [param mt] (debug builds only).
##
## The relay is added as a child of [param mt] so its RPCs use [code]mt.backend.api[/code]
## (the game multiplayer context), not the default /root SceneMultiplayer.
## [br]
## Server: owns the relay. Its editor is served directly by EngineDebugger; it does
## NOT register as a recipient (no echo). Clients' editors register as recipients so
## they see server telemetry forwarded via RPC.
## [br]
## Client with editor: registers on the server relay so the server forwards telemetry
## back. The client receives it via [method forward_to_peer] RPC.
func _setup_relay(mt: MultiplayerTree) -> void:
	if not _debug_build():
		return
	if _relays.has(mt) and is_instance_valid(_relays[mt]):
		return

	var relay := NetDebugRelay.new()
	relay.name = "NetDebugRelay"
	mt.add_child(relay)
	_relays[mt] = relay

	NetLog.info("Reporter: [RelayCreated] for path %s" % mt.get_path())

	if not _has_local_session():
		return  # Headless process: relay exists for forwarding only, no local recipient

	var token: String = ProjectSettings.get_setting("networked/debug/relay_token", "")
	if mt.is_server:
		# Server's own editor uses the direct EngineDebugger path — no recipient registration.
		# Remote clients will call register_as_recipient() via RPC when they connect.
		NetLog.info("Reporter: [RelayReady] Server relay at %s (editor uses direct path)" % mt.get_path())
	else:
		# Client with editor: register on the server relay so it forwards server telemetry here.
		NetLog.info("Reporter: [RelayRegister] Client registering as recipient via RPC")
		relay.register_as_recipient.rpc_id(1, token, reporter_id)
