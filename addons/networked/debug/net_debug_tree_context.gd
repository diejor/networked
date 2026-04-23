## Orchestrares debug signals and visuals for a single [MultiplayerTree] instance.
##
## Owns the full lifecycle of all per-tree debug connections: lobby events,
## per-lobby synchronizer hooks, peer events, and visual decorations
## (nameplates).
## [br][br]
## State is isolated to this instance to support multi-window Embedded Server.
@tool
class_name NetDebugTreeContext
extends Node

## Emitted when the tree has finished initial wiring and is ready for debug tiling.
signal tree_ready

const _NAMEPLATE_SCENE = "uid://dui4l6oylk8ju"

## The locally authoritative [ClientComponent] for this tree.
var authority_client: ClientComponent:
	get:
		var mt := _mt_ref.get_ref() as MultiplayerTree
		return mt.authority_client if mt else null

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

## Lobby -> [CheckpointToken] captured from the [code]lobby_spawn[/code] span,
## used for causal linking.
var _lobby_tokens: Dictionary = {}

## Lobby -> [Callable] connected to [member Lobby.synchronizer.spawned], for
## disconnect-on-cleanup.
var _hooked_lobbies: Dictionary = {}

var _lobby_wired: bool = false


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
## state from the [MultiplayerLobbyManager].
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
	var lm: MultiplayerLobbyManager = mt.get_service(MultiplayerLobbyManager)
	
	# Manually enrich the tree root's snapshot with service-level data.
	# This keeps the MultiplayerTree core clean while providing rich context.
	var session_state: Dictionary = {
		"is_server": mt.is_server,
		"peer_id": \
			mt.multiplayer_api.get_unique_id() if mt.multiplayer_api else 0,
		"connected_peers": \
			mt.multiplayer_api.get_peers() if mt.multiplayer_api else [],
		"active_lobbies": \
			lm.active_lobbies.keys() if lm else [],
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
		var client := \
			player.get_node_or_null("%ClientComponent") as ClientComponent
		if client:
			var nameplate: DebugClient = load(_NAMEPLATE_SCENE).instantiate()
			nameplate.name = "NetDebugNameplate"
			nameplate.follow_client(client)
			player.add_child(nameplate)
	elif not should_have and existing:
		existing.name = "Nameplate_Deleting"
		existing.queue_free()


# ─── Helpers ──────────────────────────────────────────────────────────────────

func _get_stable_id(node: Node) -> Variant:
	var client := \
		node.get_node_or_null("%ClientComponent") as ClientComponent
	return node.get_multiplayer_authority() if client else str(node.get_path())


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

	# Decoration observer - safe to connect at any time.
	get_tree().node_added.connect(_on_node_added)
	get_tree().node_removed.connect(_on_node_removed)

	# Peer events: notify reporter (spans, topology) and refresh decoration.
	mt.peer_connected.connect(func(id: int):
		var r := _reporter_ref.get_ref() as NetworkedDebugReporter
		if r:
			r._on_peer_connected(id, mt)
		_refresh_all()
	)
	mt.peer_disconnected.connect(func(id: int):
		var r := _reporter_ref.get_ref() as NetworkedDebugReporter
		if r:
			r._on_peer_disconnected(id, mt)
		_refresh_all()
	)

	# Identity changes: notify reporter to re-emit session registration.
	mt.authority_client_changed.connect(_on_authority_client_changed)

	# Debug signal wiring for lobby/clock requires configured state.
	mt.configured.connect(_on_configured)
	var lm: MultiplayerLobbyManager = mt.get_service(MultiplayerLobbyManager)
	if lm:
		_on_configured()


func _exit_tree() -> void:
	for lobby: Lobby in _hooked_lobbies.keys():
		_unhook_synchronizer(lobby)
	_hooked_lobbies.clear()
	_lobby_tokens.clear()


func _on_configured() -> void:
	_refresh_all()
	var mt: MultiplayerTree = _mt_ref.get_ref()
	var reporter := _reporter_ref.get_ref() as NetworkedDebugReporter
	if not mt or not reporter or _lobby_wired:
		return
	
	mt.register_service(self, NetDebugTreeContext)
	_lobby_wired = true

	var clock: NetworkClock = mt.get_service(NetworkClock)
	if clock:
		clock.pong_received.connect(
			func(data: Dictionary): reporter._on_clock_pong(data, mt)
		)

	var lm: MultiplayerLobbyManager = mt.get_service(MultiplayerLobbyManager)
	if is_instance_valid(lm):
		lm.lobby_spawned.connect(_on_lobby_spawned)
		lm.lobby_despawned.connect(_on_lobby_despawned)

		# Retroactively hook lobbies that spawned before this context was ready
		# (e.g. ON_STARTUP).
		for lobby: Lobby in lm.active_lobbies.values():
			if not is_instance_valid(lobby) or _hooked_lobbies.has(lobby):
				continue
			_lobby_tokens[lobby] = null # no causal token
			_hook_synchronizer(lobby)

		# Emit topology for players already present.
		for player in mt.get_all_players():
			reporter._on_player_spawned_logic(player, mt, null)
	else:
		# Lobbyless mode: emit topology for all current players.
		for player in mt.get_all_players():
			reporter._on_player_spawned_logic(player, mt, null)
			
	tree_ready.emit.call_deferred()


# ─── Lobby Lifecycle ──────────────────────────────────────────────────────────

func _on_lobby_spawned(lobby: Lobby) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var reporter := _reporter_ref.get_ref() as NetworkedDebugReporter
	if not mt or not reporter:
		return

	var token: CheckpointToken = reporter._on_lobby_spawned_logic(lobby, mt)
	_lobby_tokens[lobby] = token
	if is_instance_valid(lobby):
		lobby.set_meta(&"_net_lobby_token", token)
	_hook_synchronizer(lobby)


func _on_lobby_despawned(lobby: Lobby) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var reporter := _reporter_ref.get_ref() as NetworkedDebugReporter
	if mt and reporter:
		reporter._on_lobby_despawned_logic(lobby, mt)
	_unhook_synchronizer(lobby)
	_lobby_tokens.erase(lobby)


func _hook_synchronizer(lobby: Lobby) -> void:
	if not is_instance_valid(lobby) or \
			not is_instance_valid(lobby.synchronizer):
		return
	if _hooked_lobbies.has(lobby):
		return

	var cb := func(node: Node): _on_player_spawned(node, lobby)
	lobby.synchronizer.spawned.connect(cb)
	_hooked_lobbies[lobby] = cb


func _unhook_synchronizer(lobby: Lobby) -> void:
	var cb: Callable = _hooked_lobbies.get(lobby, Callable())
	if cb.is_valid() and is_instance_valid(lobby) and \
			is_instance_valid(lobby.synchronizer):
		if lobby.synchronizer.spawned.is_connected(cb):
			lobby.synchronizer.spawned.disconnect(cb)
	_hooked_lobbies.erase(lobby)


func _on_player_spawned(player: Node, lobby: Lobby) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var reporter := _reporter_ref.get_ref() as NetworkedDebugReporter
	if not mt or not reporter:
		return

	var token: CheckpointToken = _lobby_tokens.get(lobby, null)
	if not token and not lobby:
		token = mt.get_lobbyless_session_token()

	reporter._on_player_spawned_logic(player, mt, token)
	_decorate_player(player)


# ─── Node Observer (Decoration Only) ─────────────────────────────────────────

func _on_node_added(node: Node) -> void:
	# Only decoration - topology is driven by lobby.synchronizer.spawned.
	if node is ClientComponent:
		_on_client_added.call_deferred(node)


func _on_node_removed(_node: Node) -> void:
	pass


func _on_client_added(comp: ClientComponent) -> void:
	if not is_instance_valid(comp) or not comp.is_inside_tree():
		return
	var mt := _mt_ref.get_ref() as MultiplayerTree
	if not mt or not mt.is_ancestor_of(comp):
		return
	
	var player := comp.owner
	if not is_instance_valid(player):
		return
	_decorate_player(player)

	# Notify reporter for topology and spans.
	# In lobbyless mode, this is our only trigger. In lobby mode, this might be
	# redundant with lobby.synchronizer.spawned, but 
	# reporter._on_player_spawned_logic is idempotent for spans (deduped by 
	# node path) and snapshots (just replaces latest).
	var r := _reporter_ref.get_ref() as NetworkedDebugReporter
	if r:
		r._on_player_spawned_logic(player, mt, null)


func _on_authority_client_changed(_client: ClientComponent) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var reporter := _reporter_ref.get_ref() as NetworkedDebugReporter
	if mt and reporter:
		reporter.report_session_registered(mt)
