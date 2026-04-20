## Per-tree signal wiring for [NetworkedDebugReporter].
##
## Created by the reporter in [method NetworkedDebugReporter.register_tree] and
## added as a child of the [MultiplayerTree] being instrumented. Owns all signal
## connections for that one tree. When the tree is freed this node is freed with
## it, and Godot automatically drops every connection whose callable object is
## this node — no manual disconnection needed for the MT / clock / lobby_manager
## signals.
##
## Spawner and lobby-synchronizer connections are tracked explicitly because those
## objects live inside lobby levels, not inside the MT hierarchy, and may outlive
## this node in certain shutdown orderings.
##
## Business logic lives entirely in [NetworkedDebugReporter]; this class is a
## pure signal-wiring and lifetime-management layer.
extends Node
class_name NetDebugTreeContext

var _mt_ref: WeakRef
var _reporter_ref: WeakRef

# spawner → Callable (stored for reliable disconnect)
var _hooked_spawners: Dictionary = {}

# LobbySynchronizer → {token: CheckpointToken, cb: Callable}
var _hooked_lobby_syncs: Dictionary = {}


const _NAMEPLATE_SCENE := "uid://dui4l6oylk8ju"

var _visualizers: Dictionary = {}


func _init(mt: MultiplayerTree, reporter: NetworkedDebugReporter) -> void:
	_mt_ref = weakref(mt)
	_reporter_ref = weakref(reporter)


func set_visualizer(viz_name: String, enabled: bool) -> void:
	_visualizers[viz_name] = enabled


func is_visualizer_enabled(viz_name: String) -> bool:
	return _visualizers.get(viz_name, false)


func apply_visualizer_command(d: Dictionary) -> void:
	var viz_name: String = d.get("viz_name", "")
	var enabled: bool = d.get("enabled", false)
	set_visualizer(viz_name, enabled)
	
	if viz_name == "nameplate":
		_refresh_all_nameplates()


func _refresh_all_nameplates() -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	if not mt: return
	
	for player in _find_all_players(mt):
		_decorate_player(player)


func _decorate_player(player: Node) -> void:
	var enabled := is_visualizer_enabled("nameplate")
	var existing := player.get_node_or_null("NetDebugNameplate")
	
	if enabled and not existing:
		# Use the unique name shortcut %ClientComponent for direct lookup
		var client := player.get_node_or_null("%ClientComponent") as ClientComponent
		if client:
			var scene := load(_NAMEPLATE_SCENE) as PackedScene
			var nameplate := scene.instantiate() as DebugClient
			nameplate.name = "NetDebugNameplate"
			nameplate.follow_client(client)
			player.add_child(nameplate)
	elif not enabled and existing:
		existing.queue_free()


func _find_all_players(mt: MultiplayerTree) -> Array[Node]:
	var players: Array[Node] = []
	if mt.lobby_manager:
		for lobby in mt.lobby_manager.active_lobbies.values():
			if is_instance_valid(lobby) and is_instance_valid(lobby.level):
				# Search for any node with a ClientComponent using find_children
				var components: Array[Node] = lobby.level.find_children("*", "ClientComponent", true, false)
				for child in components:
					players.append(child.owner)
	return players


func _ready() -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	if not mt:
		return
	mt.peer_connected.connect(_on_peer_connected)
	mt.peer_disconnected.connect(_on_peer_disconnected)
	mt.configured.connect(_on_configured)


func _exit_tree() -> void:
	# Explicit cleanup for connections to objects that may outlive this node.
	# MT's own signals and clock / lobby_manager signals auto-disconnect because
	# their callables are bound to self (freed here).
	for sync in _hooked_lobby_syncs.keys():
		if is_instance_valid(sync):
			var cb: Callable = _hooked_lobby_syncs[sync].get("cb", Callable())
			if cb.is_valid() and sync.spawned.is_connected(cb):
				sync.spawned.disconnect(cb)
	_hooked_lobby_syncs.clear()

	for spawner in _hooked_spawners.keys():
		if is_instance_valid(spawner):
			var cb: Callable = _hooked_spawners[spawner]
			if spawner.spawned.is_connected(cb):
				spawner.spawned.disconnect(cb)
	_hooked_spawners.clear()


# ─── MT Signal Forwarding ─────────────────────────────────────────────────────

func _on_peer_connected(peer_id: int) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var reporter := _reporter_ref.get_ref() as NetworkedDebugReporter
	if mt and reporter:
		reporter._on_peer_connected(peer_id, mt)


func _on_peer_disconnected(peer_id: int) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var reporter := _reporter_ref.get_ref() as NetworkedDebugReporter
	if mt and reporter:
		reporter._on_peer_disconnected(peer_id, mt)


func _on_configured() -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	if not mt:
		return
	if mt.clock:
		mt.clock.pong_received.connect(_on_clock_pong)
	if mt.lobby_manager:
		mt.lobby_manager.lobby_spawned.connect(_on_lobby_spawned)
		mt.lobby_manager.lobby_despawned.connect(_on_lobby_despawned)


func _on_clock_pong(data: Dictionary) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var reporter := _reporter_ref.get_ref() as NetworkedDebugReporter
	if mt and reporter:
		reporter._on_clock_pong(data, mt)


# ─── Lobby Signal Handling ────────────────────────────────────────────────────

func _on_lobby_spawned(lobby: Lobby) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var reporter := _reporter_ref.get_ref() as NetworkedDebugReporter
	if not mt or not reporter:
		return
	if not is_instance_valid(lobby) or not is_instance_valid(lobby.level):
		return

	hook_spawners_in(lobby.level)

	var lobby_token := reporter._on_lobby_spawned_logic(lobby, mt)

	if is_instance_valid(lobby.synchronizer) and lobby.synchronizer not in _hooked_lobby_syncs:
		var cb := _on_player_spawned.bind(lobby.synchronizer, mt)
		lobby.synchronizer.spawned.connect(cb)
		_hooked_lobby_syncs[lobby.synchronizer] = {"token": lobby_token, "cb": cb}


func _on_lobby_despawned(lobby: Lobby) -> void:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	var reporter := _reporter_ref.get_ref() as NetworkedDebugReporter
	if not mt or not reporter:
		return
	if not is_instance_valid(lobby) or not is_instance_valid(lobby.level):
		return

	unhook_spawners_in(lobby.level)

	if is_instance_valid(lobby.synchronizer) and lobby.synchronizer in _hooked_lobby_syncs:
		var cb: Callable = _hooked_lobby_syncs[lobby.synchronizer].get("cb", Callable())
		if cb.is_valid() and lobby.synchronizer.spawned.is_connected(cb):
			lobby.synchronizer.spawned.disconnect(cb)
		_hooked_lobby_syncs.erase(lobby.synchronizer)

	reporter._on_lobby_despawned_logic(lobby, mt)


func _on_player_spawned(player: Node, synchronizer: Node, mt: MultiplayerTree) -> void:
	var reporter := _reporter_ref.get_ref() as NetworkedDebugReporter
	if not reporter:
		return
	var entry: Dictionary = _hooked_lobby_syncs.get(synchronizer, {})
	var lobby_token: CheckpointToken = entry.get("token")
	reporter._on_player_spawned_logic(player, mt, lobby_token)
	_decorate_player(player)


func _on_spawner_native_confirmed(node: Node, _spawner: MultiplayerSpawner) -> void:
	var active := NetTrace.active_span()
	if active and active.label == "player_spawn":
		active.step("spawner_native_confirmed", {"node_path": str(node.get_path())})


# ─── Spawner Hooks ────────────────────────────────────────────────────────────

func hook_spawners_in(root: Node) -> void:
	for spawner: MultiplayerSpawner in root.find_children("*", "MultiplayerSpawner", true, false):
		if spawner in _hooked_spawners:
			continue
		var cb := _on_spawner_native_confirmed.bind(spawner)
		spawner.spawned.connect(cb)
		_hooked_spawners[spawner] = cb


func unhook_spawners_in(root: Node) -> void:
	for spawner: MultiplayerSpawner in root.find_children("*", "MultiplayerSpawner", true, false):
		if spawner not in _hooked_spawners:
			continue
		var cb: Callable = _hooked_spawners[spawner]
		if is_instance_valid(spawner) and spawner.spawned.is_connected(cb):
			spawner.spawned.disconnect(cb)
		_hooked_spawners.erase(spawner)
