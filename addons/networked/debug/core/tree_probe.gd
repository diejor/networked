## Orchestrates debug signals and visuals for one [MultiplayerTree].
##
## Owns the full lifecycle of all per-tree debug connections: scene events,
## per-scene synchronizer hooks, peer events, and visual decorations
## (nameplates).
## [br][br]
## State is isolated to this instance to support multi-window Embedded Server.
@tool
class_name TreeProbe
extends Node

## Emitted when the tree has finished initial debug wiring.
signal tree_ready

## Emitted when a local clock pong is captured.
signal clock_pong_captured(data: Dictionary)

const _NAMEPLATE_SCENE = "uid://dui4l6oylk8ju"

## The local player identity for this tree.
var local_player: NetwEntity:
	get:
		var mt := _mt_ref.get_ref() as MultiplayerTree
		return mt.local_player if mt else null

var _mt_ref: WeakRef
var _reporter_ref: WeakRef

# Visualizer state, represented as a [Dictionary] mapping each visualizer to
# its states:
# [codeblock]
# Dictionary
#  ┖╴viz_name (String)
#     ┖╴{ }
#        ┠╴stable_id (int PeerID or String Path) (bool)
#        ┖╴"" (bool)                      # tree-wide default
# [/codeblock]
var _visualizers: Dictionary = { }

# Scene -> [CheckpointToken] captured from the [code]scene_spawn[/code] span,
# used for causal linking.
var _scene_tokens: Dictionary = { }

# Scene -> [Callable] connected to [signal MultiplayerScene.spawned], for
# disconnect-on-cleanup.
var _hooked_scenes: Dictionary = { }

var _scene_wired: bool = false

# Delayed-scan timers a validator scheduled through [method after], tracked so
# they can be cancelled on teardown.
var _pending_timers: Array[SceneTreeTimer] = []

# Demand-driven replication watch state, keyed by watched [NodePath]:
# [codeblock]
# Dictionary
#  ┖╴node_path (NodePath)
#     ┖╴Array
#        ┖╴{ }
#           ┠╴sync (MultiplayerSynchronizer)
#           ┖╴cb (Callable)                 # connected to sync signals
# [/codeblock]
var _watched: Dictionary = { }


func _init(mt: MultiplayerTree, reporter: DebugReporter) -> void:
	_mt_ref = weakref(mt)
	_reporter_ref = weakref(reporter)

# Public API.


## Returns [code]true[/code] if the visualizer is enabled for a specific node.
func is_enabled(viz: String, node: Node = null) -> bool:
	var states: Dictionary = _visualizers.get(viz, { })
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

	var states: Dictionary = _visualizers.get(viz, { })

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
## Prefers the span's explicit target node (if set via [method NetwSpan.with_node]).
## Falls back to a "Session Snapshot" of the tree root, enriched with high-level
## state from the [MultiplayerSceneManager].
func build_crash_snapshot(span: NetwSpan) -> NetwNodeSnapshot:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	if not mt:
		return null

	# Priority 1: Explicit target node
	var target := span.get_target_node() if span else null
	if is_instance_valid(target):
		return NetwNodeSnapshot.from_node(target)

	# Priority 2: Session fallback (Tree Root)
	var snap := NetwNodeSnapshot.from_node(mt)
	var scenes := mt.api.scenes if mt.api else null

	# Manually enrich the tree root's snapshot with service-level data.
	# This keeps the MultiplayerTree core clean while providing rich context.
	var session_state: Dictionary = {
		"is_server": mt.role == NetwSessionInterface.Role.DEDICATED_SERVER or mt.role == NetwSessionInterface.Role.LISTEN_SERVER,
		"role": mt.role,
		"role_name": NetwSessionInterface.Role.keys()[mt.role],
		"peer_id": \
		mt.multiplayer_api.get_unique_id() if mt.multiplayer_api else 0,
		"connected_peers": \
		mt.multiplayer_api.get_peers() if mt.multiplayer_api else [],
		"active_scenes": \
		scenes.scenes.keys() if scenes else [],
		"backend": String(mt.scheme),
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


## Schedules [param cb] to run after [param delay] seconds on this tree's
## [SceneTree]. Used by validators (which are [RefCounted] and cannot create
## timers) to run delayed scans such as the zombie check.
func after(delay: float, cb: Callable) -> void:
	if not is_inside_tree():
		return
	var timer := get_tree().create_timer(delay)
	_pending_timers.append(timer)
	timer.timeout.connect(
		func() -> void:
			_pending_timers.erase(timer)
			if cb.is_valid():
				cb.call(),
		CONNECT_ONE_SHOT,
	)


## Forwards a validator-produced [param finding] to the reporter's finding
## pipeline. Used by validators that report outside a live [NetwReport] (e.g.
## after a delayed scan).
func emit_finding(finding: NetwFinding) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var reporter := _reporter_ref.get_ref() as DebugReporter
	if reporter and is_instance_valid(mt):
		reporter._emit_finding(finding, mt)

# Topology snapshots.


## Builds and emits a [NetwTopologySnapshot] for [param player] on this tree.
## [br][br]
## On a client only the locally-owned player is emitted; the host emits for
## every player so its topology view is complete. The reporter still ships the
## payload over the wire.
func send_topology_snapshot(player: Node) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var reporter := _reporter_ref.get_ref() as DebugReporter
	if not is_instance_valid(mt) or not reporter or not is_instance_valid(player):
		return

	if not mt.is_host:
		if local_player == null or local_player.owner != player:
			return

	var snap := NetwTopologySnapshot.new()
	snap.tree_name = mt.get_tree_name()
	snap.node_path = str(player.get_path())
	snap.username = player.name.get_slice("|", 0) if "|" in player.name else \
	player.name
	snap.peer_id = player.get_multiplayer_authority()
	snap.role = mt.role
	snap.is_server = mt.is_host
	snap.lobby_name = player.get_parent().name \
	if is_instance_valid(player.get_parent()) else ""
	snap.active_scene = get_active_scene_path(player)
	snap.cache_info = {
		"hit": player.has_meta(SynchronizersCache.META_KEY),
		"hooked": player.has_meta(&"_sc_invalidation_connected"),
	}

	for sync: MultiplayerSynchronizer in SynchronizersCache.get_synchronizers(player):
		var si := NetwTopologySnapshot.SyncInfo.new()
		si.name = sync.name
		si.root_path = str(sync.root_path)
		si.authority = sync.get_multiplayer_authority()
		si.enabled = true

		var root := sync.get_node_or_null(sync.root_path)

		if sync.replication_config:
			for prop: NodePath in sync.replication_config.get_properties():
				var pi := NetwTopologySnapshot.PropInfo.new()
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
	reporter.emit_debug_event("networked:topology_snapshot", snap.to_dict(), mt)

# Demand-driven replication watch.


## Starts watching the node at [param np] for replication changes, emitting a
## snapshot on each sync. No-op if already watched or the node is missing. Driven
## by the editor's [code]watch_node[/code] command through the reporter.
func watch_node(np: NodePath) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	if not is_instance_valid(mt) or np.is_empty() or np in _watched:
		return

	var node: Node = get_tree().root.get_node_or_null(np)
	if not is_instance_valid(node):
		return

	var hooked: Array = []
	for sync: MultiplayerSynchronizer in SynchronizersCache.get_synchronizers(node):
		var cb := func() -> void: _send_replication_snapshot(node, sync)
		sync.delta_synchronized.connect(cb)
		sync.synchronized.connect(cb)
		hooked.append({ "sync": sync, "cb": cb })

	_watched[np] = hooked
	_send_full_replication_snapshot(node)


## Stops watching the node at [param np]. No-op if it was not watched.
func unwatch_node(np: NodePath) -> void:
	if np not in _watched:
		return

	for entry in _watched[np]:
		var sync: MultiplayerSynchronizer = entry["sync"]
		var cb: Callable = entry["cb"]
		if is_instance_valid(sync):
			if sync.delta_synchronized.is_connected(cb):
				sync.delta_synchronized.disconnect(cb)
			if sync.synchronized.is_connected(cb):
				sync.synchronized.disconnect(cb)

	_watched.erase(np)


func _send_replication_snapshot(node: Node, sync: MultiplayerSynchronizer) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var reporter := _reporter_ref.get_ref() as DebugReporter
	if not reporter or not is_instance_valid(mt):
		return
	if not is_instance_valid(node) or not is_instance_valid(sync):
		return

	var snapshot := NetwReplicationSnapshot.new()
	snapshot.tree_name = mt.get_tree_name()
	snapshot.node_path = str(node.get_path())
	snapshot.properties = _collect_properties(node, sync)
	reporter._queue("networked:replication_snapshot", snapshot.to_dict(), mt)


func _send_full_replication_snapshot(node: Node) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var reporter := _reporter_ref.get_ref() as DebugReporter
	if not reporter or not is_instance_valid(mt):
		return

	var all_props: Dictionary = { }
	var inventory: Array = []
	for sync: MultiplayerSynchronizer in SynchronizersCache.get_synchronizers(node):
		all_props.merge(_collect_properties(node, sync))
		inventory.append(
			{
				"name": sync.name,
				"authority": sync.get_multiplayer_authority(),
				"root_path": str(sync.root_path),
			},
		)

	var snapshot := NetwReplicationSnapshot.new()
	snapshot.tree_name = mt.get_tree_name()
	snapshot.node_path = str(node.get_path())
	snapshot.properties = all_props
	snapshot.inventory = inventory
	reporter._queue("networked:replication_snapshot", snapshot.to_dict(), mt)


func _collect_properties(
		node: Node,
		sync: MultiplayerSynchronizer,
) -> Dictionary:
	var props: Dictionary = { }
	if not sync.replication_config:
		return props

	var root_node := sync.get_node_or_null(sync.root_path)
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
				TYPE_OBJECT,
				TYPE_RID,
				TYPE_CALLABLE,
				TYPE_SIGNAL,
			]:
				props[s] = val

	return props

# Decoration lifecycle.


func _refresh_all() -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	if not mt:
		return

	for player in _find_players(mt):
		_decorate_player(player)


func _decorate_player(player: Node) -> void:
	if not is_instance_valid(player):
		return
	var existing := player.get_node_or_null("DebugNameplate")
	var should_have := is_enabled("nameplate", player)

	if should_have and not existing:
		var username := NetwIdentity.username_of(player)
		if not username.is_empty():
			var nameplate: DebugClient = load(
				_NAMEPLATE_SCENE,
			).instantiate()
			nameplate.name = "DebugNameplate"
			nameplate.follow_target(player, username)
			player.add_child(nameplate)
	elif not should_have and existing:
		existing.name = "Nameplate_Deleting"
		existing.queue_free()

# Helpers.


func _get_stable_id(node: Node) -> Variant:
	return NetwIdentity.stable_id_of(node)


func _get_stable_id_from_path(path: String) -> Variant:
	var node := get_tree().root.get_node_or_null(path)
	return _get_stable_id(node) if is_instance_valid(node) else path


# Builds a typed event and hands it to the reporter's dispatch seam. The
# reporter's per-event forwarding still drives all current behavior; this is
# additive observation for the validator registry to consume later.
func _dispatch(
		kind: NetwTreeEvent.Kind,
		node: Node = null,
		peer_id: int = 0,
		data: Dictionary = { },
) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var r := _reporter_ref.get_ref() as DebugReporter
	if not mt or not r:
		return
	var ev := NetwTreeEvent.new(kind, mt)
	ev.node = node
	ev.peer_id = peer_id
	ev.data = data
	r.dispatch_tree_event(ev)


func _find_players(mt: MultiplayerTree) -> Array[Node]:
	var nodes: Array[Node] = []
	for player in mt.get_all_players():
		if player != null and is_instance_valid(player.owner):
			nodes.append(player.owner)
	return nodes

# Signal wiring.


func _ready() -> void:
	var mt: MultiplayerTree = _mt_ref.get_ref()
	if not mt:
		return

	# Peer events: notify reporter (spans, topology) and refresh decoration.
	mt.peer_connected.connect(_on_mt_peer_connected)
	mt.peer_disconnected.connect(_on_mt_peer_disconnected)

	# Identity changes: notify reporter to re-emit session registration.
	mt.local_player_changed.connect(_on_local_player_changed)

	# Role transitions: observed offline so Role.NONE -> live is in the stream.
	mt.state_changed.connect(_on_role_changed)

	# Debug signal wiring for scene/clock requires a live session.
	mt.session_entered.connect(_on_configured)
	var sm: MultiplayerSceneManager = mt.get_service(MultiplayerSceneManager)
	if sm:
		_on_configured()


func _exit_tree() -> void:
	_disconnect_all()
	NetwService.unregister(self, TreeProbe)
	for scene: MultiplayerScene in _hooked_scenes.keys():
		_unhook_synchronizer(scene)
	_hooked_scenes.clear()
	_scene_tokens.clear()

	for timer in _pending_timers:
		if is_instance_valid(timer):
			for connection in timer.timeout.get_connections():
				timer.timeout.disconnect(connection.callable)
	_pending_timers.clear()

	for np in _watched.keys():
		unwatch_node(np)
	_watched.clear()


func _disconnect_all() -> void:
	var mt: MultiplayerTree = _mt_ref.get_ref()

	if mt:
		if mt.peer_connected.is_connected(_on_mt_peer_connected):
			mt.peer_connected.disconnect(_on_mt_peer_connected)
		if mt.peer_disconnected.is_connected(_on_mt_peer_disconnected):
			mt.peer_disconnected.disconnect(_on_mt_peer_disconnected)
		if mt.local_player_changed.is_connected(_on_local_player_changed):
			mt.local_player_changed.disconnect(_on_local_player_changed)
		if mt.state_changed.is_connected(_on_role_changed):
			mt.state_changed.disconnect(_on_role_changed)
		if mt.session_entered.is_connected(_on_configured):
			mt.session_entered.disconnect(_on_configured)

		var clock: MultiplayerClock = mt.get_service(MultiplayerClock)
		if clock and clock.pong_received.is_connected(_on_clock_pong):
			clock.pong_received.disconnect(_on_clock_pong)

		var scenes := mt.api.scenes if mt.api else null
		if scenes:
			if scenes.scene_spawned.is_connected(_on_scene_spawned):
				scenes.scene_spawned.disconnect(_on_scene_spawned)
			if scenes.scene_despawned.is_connected(_on_scene_despawned):
				scenes.scene_despawned.disconnect(_on_scene_despawned)


func _on_mt_peer_connected(id: int) -> void:
	var mt: MultiplayerTree = _mt_ref.get_ref()
	var r := _reporter_ref.get_ref() as DebugReporter
	var span: NetwPeerSpan = null
	if r and mt:
		span = r._on_peer_connected(id, mt)
	# The reporter opened the operation span; validators fail it during dispatch,
	# then the probe closes it (no-op if a validator already failed it).
	_dispatch(NetwTreeEvent.Kind.PEER_CONNECTED, null, id, { "span": span })
	if span:
		span.end()
	_refresh_all()


func _on_mt_peer_disconnected(id: int) -> void:
	var mt: MultiplayerTree = _mt_ref.get_ref()
	var r := _reporter_ref.get_ref() as DebugReporter
	if r and mt:
		r._on_peer_disconnected(id, mt)
	_dispatch(NetwTreeEvent.Kind.PEER_DISCONNECTED, null, id)
	_refresh_all()


func _on_configured() -> void:
	_refresh_all()
	var mt: MultiplayerTree = _mt_ref.get_ref()
	var reporter := _reporter_ref.get_ref() as DebugReporter
	if not mt or not reporter or _scene_wired:
		return

	NetwService.register(self, TreeProbe)
	_scene_wired = true

	var clock: MultiplayerClock = mt.get_service(MultiplayerClock)
	if clock:
		clock.pong_received.connect(_on_clock_pong)

	var scenes := mt.api.scenes if mt.api else null
	if scenes:
		scenes.scene_spawned.connect(_on_scene_spawned)
		scenes.scene_despawned.connect(_on_scene_despawned)

		# Retroactively hook scenes that spawned before this context was ready
		# (e.g. ON_STARTUP).
		for scene: MultiplayerScene in scenes.scenes.values():
			if not is_instance_valid(scene) or _hooked_scenes.has(scene):
				continue
			_scene_tokens[scene] = null # no causal token
			_hook_synchronizer(scene)

		# Emit topology for players already present.
		for player in mt.get_all_players():
			_emit_retroactive_player_spawned(reporter, mt, player.owner)
	else:
		# Sceneless mode: emit topology for all current players.
		for player in mt.get_all_players():
			_emit_retroactive_player_spawned(reporter, mt, player.owner)

	tree_ready.emit.call_deferred()

# Scene lifecycle.


func _on_scene_spawned(scene: MultiplayerScene) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var reporter := _reporter_ref.get_ref() as DebugReporter
	if not mt or not reporter:
		return

	var span: NetwPeerSpan = reporter._on_scene_spawned_logic(scene, mt)
	var token: CheckpointToken = span.checkpoint() if span else null
	_scene_tokens[scene] = token
	if is_instance_valid(scene):
		scene.set_meta(&"_net_scene_token", token)
	_dispatch(NetwTreeEvent.Kind.SCENE_SPAWNED, scene, 0, { "span": span, "token": token })
	if span:
		span.end()
	_hook_synchronizer(scene)


func _on_scene_despawned(scene: MultiplayerScene) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var reporter := _reporter_ref.get_ref() as DebugReporter
	if mt and reporter:
		reporter._on_scene_despawned_logic(scene, mt)
	_dispatch(NetwTreeEvent.Kind.SCENE_DESPAWNED, scene)
	_unhook_synchronizer(scene)
	_scene_tokens.erase(scene)


func _hook_synchronizer(scene: MultiplayerScene) -> void:
	if not is_instance_valid(scene):
		return
	if _hooked_scenes.has(scene):
		return

	var cb := func(node: Node): _on_player_spawned(node, scene)
	scene.spawned.connect(cb)
	_hooked_scenes[scene] = cb


func _unhook_synchronizer(scene: MultiplayerScene) -> void:
	var cb: Callable = _hooked_scenes.get(scene, Callable())
	if cb.is_valid() and is_instance_valid(scene):
		if scene.spawned.is_connected(cb):
			scene.spawned.disconnect(cb)
	_hooked_scenes.erase(scene)


func _on_player_spawned(player: Node, scene: MultiplayerScene) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var reporter := _reporter_ref.get_ref() as DebugReporter
	if not mt or not reporter:
		return

	var token: CheckpointToken = _scene_tokens.get(scene, null)
	if not token and not scene:
		token = mt.get_sceneless_session_token()

	send_topology_snapshot(player)
	var span: NetwSpan = reporter._on_player_spawned_logic(player, mt, token)
	_dispatch(NetwTreeEvent.Kind.PLAYER_SPAWNED, player, 0, { "span": span, "token": token })
	if span:
		span.end()
	_decorate_player(player)


# Emits PLAYER_SPAWNED for a player that existed before this probe wired its
# scene signals, mirroring the live path's span lifecycle so the race and
# topology validators still see it.
func _emit_retroactive_player_spawned(
		reporter: DebugReporter,
		mt: MultiplayerTree,
		player: Node,
) -> void:
	send_topology_snapshot(player)
	var span: NetwSpan = reporter._on_player_spawned_logic(player, mt, null)
	_dispatch(NetwTreeEvent.Kind.PLAYER_SPAWNED, player, 0, { "span": span, "token": null })
	if span:
		span.end()


func _on_local_player_changed(player: NetwEntity) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var reporter := _reporter_ref.get_ref() as DebugReporter
	if mt and reporter:
		reporter.report_session_registered(mt)
	var owner_node: Node = player.owner if is_instance_valid(player) else null
	_dispatch(NetwTreeEvent.Kind.LOCAL_PLAYER_CHANGED, owner_node)


func _on_role_changed(
		old_state: NetwSessionInterface.State,
		new_state: NetwSessionInterface.State,
) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var role := mt.role if is_instance_valid(mt) else NetwSessionInterface.Role.NONE
	_dispatch(
		NetwTreeEvent.Kind.ROLE_CHANGED,
		null,
		0,
		{
			"role": role,
			"old_state": old_state,
			"new_state": new_state,
		},
	)


func _on_clock_pong(data: Dictionary) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	if is_instance_valid(mt) and mt.local_player:
		data["username"] = NetwIdentity.username_of(mt.local_player.owner)
	_dispatch(NetwTreeEvent.Kind.CLOCK_PONG, null, 0, data)
	clock_pong_captured.emit(data)
