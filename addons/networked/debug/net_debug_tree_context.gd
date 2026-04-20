## Orchestrates debug signals and visuals for a single [MultiplayerTree] instance.
##
## Owns the full lifecycle of all per-tree debug connections: lobby events,
## per-lobby synchronizer hooks, peer events, and visual decorations (nameplates).
## State is isolated to this instance to support multi-window Embedded Server.
@tool
class_name NetDebugTreeContext
extends Node

const _NAMEPLATE_SCENE = "uid://dui4l6oylk8ju"

var _mt_ref: WeakRef
var _reporter_ref: WeakRef

## Visualizer state.
## Key: viz_name (String)
## Value: Dictionary mapping "stable_id" (int PeerID or String Path) to bool.
## A key of "" in the inner dictionary represents the tree-wide default.
var _visualizers: Dictionary = {}

## Lobby → CheckpointToken captured from the lobby_spawn span, used for causal linking.
var _lobby_tokens: Dictionary = {}

## Lobby → Callable connected to lobby.synchronizer.spawned, for disconnect-on-cleanup.
var _hooked_lobbies: Dictionary = {}

var _lobby_wired: bool = false


func _init(mt: MultiplayerTree, reporter: NetworkedDebugReporter) -> void:
	_mt_ref = weakref(mt)
	_reporter_ref = weakref(reporter)


# ─── Public API ───────────────────────────────────────────────────────────────

## Returns true if the visualizer is enabled for a specific node.
func is_enabled(viz: String, node: Node = null) -> bool:
	var states: Dictionary = _visualizers.get(viz, {})
	if node:
		var id := _get_stable_id(node)
		if states.has(id): return states[id]
	return states.get("", false)


## Updates visualizer state from an editor command.
func apply_command(d: Dictionary) -> void:
	var viz: String = d.get("viz_name", "")
	var enabled: bool = d.get("enabled", false)
	var path: String = d.get("node_path", "")

	if viz.is_empty(): return

	var states: Dictionary = _visualizers.get(viz, {})
	var id := _get_stable_id_from_path(path) if not path.is_empty() else ""
	states[id] = enabled
	_visualizers[viz] = states

	_refresh_all()


# ─── Decoration Lifecycle ─────────────────────────────────────────────────────

func _refresh_all() -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	if not mt: return

	for player in _find_players(mt):
		_decorate_player(player)


func _decorate_player(player: Node) -> void:
	if not is_instance_valid(player): return
	var existing := player.get_node_or_null("NetDebugNameplate")
	var should_have := is_enabled("nameplate", player)

	if should_have and not existing:
		var client := player.get_node_or_null("%ClientComponent") as ClientComponent
		if client:
			var nameplate: DebugClient = load(_NAMEPLATE_SCENE).instantiate()
			nameplate.name = "NetDebugNameplate"
			nameplate.follow_client(client)
			player.add_child(nameplate)
	elif not should_have and existing:
		existing.queue_free()


# ─── Helpers ──────────────────────────────────────────────────────────────────

func _get_stable_id(node: Node) -> Variant:
	var client := node.get_node_or_null("%ClientComponent") as ClientComponent
	return node.get_multiplayer_authority() if client else str(node.get_path())


func _get_stable_id_from_path(path: String) -> Variant:
	var node := get_tree().root.get_node_or_null(path)
	return _get_stable_id(node) if is_instance_valid(node) else path


func _find_players(mt: MultiplayerTree) -> Array[Node]:
	var players: Array[Node] = []
	if not mt.lobby_manager: return players

	for lobby in mt.lobby_manager.active_lobbies.values():
		if is_instance_valid(lobby) and is_instance_valid(lobby.level):
			var comps: Array[Node] = lobby.level.find_children("*", "ClientComponent", true, false)
			for c in comps: players.append(c.owner)
	return players


# ─── Signal Wiring ────────────────────────────────────────────────────────────

func _ready() -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	if not mt: return

	# Decoration observer — safe to connect at any time.
	get_tree().node_added.connect(_on_node_added)

	# Peer events: notify reporter (spans, relay registration) and refresh decoration.
	mt.peer_connected.connect(func(id: int):
		var r := _reporter_ref.get_ref() as NetworkedDebugReporter
		if r: r._on_peer_connected(id, mt)
		_refresh_all()
	)
	mt.peer_disconnected.connect(func(id: int):
		var r := _reporter_ref.get_ref() as NetworkedDebugReporter
		if r: r._on_peer_disconnected(id, mt)
		_refresh_all()
	)

	# Debug signal wiring for lobby/clock requires configured state.
	mt.configured.connect(_on_configured)
	if mt.lobby_manager:
		_on_configured()


func _exit_tree() -> void:
	for lobby: Lobby in _hooked_lobbies.keys():
		_unhook_synchronizer(lobby)
	_hooked_lobbies.clear()
	_lobby_tokens.clear()


func _on_configured() -> void:
	_refresh_all()
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var reporter := _reporter_ref.get_ref() as NetworkedDebugReporter
	if not mt or not reporter or _lobby_wired: return
	_lobby_wired = true

	if mt.clock:
		mt.clock.pong_received.connect(func(data: Dictionary): reporter._on_clock_pong(data, mt))

	if not mt.lobby_manager: return
	mt.lobby_manager.lobby_spawned.connect(_on_lobby_spawned)
	mt.lobby_manager.lobby_despawned.connect(_on_lobby_despawned)

	# Retroactively hook lobbies that spawned before this context was ready (e.g. ON_STARTUP).
	for lobby: Lobby in mt.lobby_manager.active_lobbies.values():
		if not is_instance_valid(lobby) or _hooked_lobbies.has(lobby): continue
		_lobby_tokens[lobby] = null  # no causal token for lobbies we didn't witness
		_hook_synchronizer(lobby)
		# Emit topology for players already present in this lobby.
		if is_instance_valid(lobby.level):
			var comps := lobby.level.find_children("*", "ClientComponent", true, false)
			for c: Node in comps:
				if is_instance_valid(c.owner):
					reporter._on_player_spawned_logic(c.owner, mt, null)


# ─── Lobby Lifecycle ──────────────────────────────────────────────────────────

func _on_lobby_spawned(lobby: Lobby) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var reporter := _reporter_ref.get_ref() as NetworkedDebugReporter
	if not mt or not reporter: return

	var token: CheckpointToken = reporter._on_lobby_spawned_logic(lobby, mt)
	_lobby_tokens[lobby] = token
	_hook_synchronizer(lobby)


func _on_lobby_despawned(lobby: Lobby) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var reporter := _reporter_ref.get_ref() as NetworkedDebugReporter
	if mt and reporter:
		reporter._on_lobby_despawned_logic(lobby, mt)
	_unhook_synchronizer(lobby)
	_lobby_tokens.erase(lobby)


func _hook_synchronizer(lobby: Lobby) -> void:
	if not is_instance_valid(lobby) or not is_instance_valid(lobby.synchronizer): return
	if _hooked_lobbies.has(lobby): return

	var cb := func(node: Node): _on_player_spawned(node, lobby)
	lobby.synchronizer.spawned.connect(cb)
	_hooked_lobbies[lobby] = cb


func _unhook_synchronizer(lobby: Lobby) -> void:
	var cb: Callable = _hooked_lobbies.get(lobby, Callable())
	if cb.is_valid() and is_instance_valid(lobby) and is_instance_valid(lobby.synchronizer):
		if lobby.synchronizer.spawned.is_connected(cb):
			lobby.synchronizer.spawned.disconnect(cb)
	_hooked_lobbies.erase(lobby)


func _on_player_spawned(player: Node, lobby: Lobby) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var reporter := _reporter_ref.get_ref() as NetworkedDebugReporter
	if not mt or not reporter: return

	var token: CheckpointToken = _lobby_tokens.get(lobby, null)
	reporter._on_player_spawned_logic(player, mt, token)
	_decorate_player(player)


# ─── Node Observer (Decoration Only) ─────────────────────────────────────────

func _on_node_added(node: Node) -> void:
	# Only decoration — topology is driven by lobby.synchronizer.spawned.
	if node is ClientComponent:
		_on_client_added.call_deferred(node)


func _on_client_added(comp: ClientComponent) -> void:
	if not is_instance_valid(comp) or not comp.is_inside_tree(): return
	var mt := _mt_ref.get_ref() as MultiplayerTree
	if not mt or not mt.is_ancestor_of(comp): return
	var player := comp.owner
	if not is_instance_valid(player): return
	_decorate_player(player)
