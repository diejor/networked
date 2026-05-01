## Composite context providing both session-level and lobby-level access via
## [member session] and [member lobby].
##
## Obtain via [method NetwComponent.get_context] or [method for_node].
## [codeblock]
## var ctx := get_context()
## ctx.session.get_service(MyService)
## if ctx.has_lobby():
##     await ctx.lobby.wait_for_players(4)
## [/codeblock]
class_name NetwContext
extends RefCounted

# ---------------------------------------------------------------------------
# Lobby-layer signals — forwarded from [member lobby]
# ---------------------------------------------------------------------------

signal player_entered(player: Node)
signal player_left(player: Node)

signal paused(reason: String)
signal unpaused()

signal suspended(reason: String)
signal resumed()
signal suspend_requested(peer_id: int, reason: String)

signal kicked(reason: String)
signal kick_requested(requester_id: int, target_id: int, reason: String)

signal countdown_started(seconds: int)
signal countdown_tick(seconds_left: int)
signal countdown_finished()
signal countdown_cancelled()

# ---------------------------------------------------------------------------
# Sub-contexts
# ---------------------------------------------------------------------------

var session: NetwSessionContext
var lobby: NetwLobbyContext


func _init(tree: MultiplayerTree, lobby_ctx: NetwLobbyContext = null) -> void:
	session = NetwSessionContext.new(tree)
	if lobby_ctx:
		lobby = lobby_ctx
		_forward_lobby_signals(lobby_ctx)


func _forward_lobby_signals(lc: NetwLobbyContext) -> void:
	lc.player_entered.connect(player_entered.emit)
	lc.player_left.connect(player_left.emit)
	lc.paused.connect(paused.emit)
	lc.unpaused.connect(unpaused.emit)
	lc.suspended.connect(suspended.emit)
	lc.resumed.connect(resumed.emit)
	lc.suspend_requested.connect(suspend_requested.emit)
	lc.kicked.connect(kicked.emit)
	lc.kick_requested.connect(kick_requested.emit)
	lc.countdown_started.connect(countdown_started.emit)
	lc.countdown_tick.connect(countdown_tick.emit)
	lc.countdown_finished.connect(countdown_finished.emit)
	lc.countdown_cancelled.connect(countdown_cancelled.emit)


# ---------------------------------------------------------------------------
# Validity
# ---------------------------------------------------------------------------

func is_valid() -> bool:
	return session != null and session.is_valid()


func has_lobby() -> bool:
	return lobby != null and lobby.is_valid()


# ---------------------------------------------------------------------------
# Session-layer pass-throughs
# ---------------------------------------------------------------------------

func is_server() -> bool:
	return session.is_server()


func get_unique_id() -> int:
	if not session:
		return 0
	return session.get_unique_id()


func get_tree_name() -> String:
	return session.get_tree_name()


func get_all_players() -> Array[Node]:
	return session.get_all_players()


func get_peer_context() -> NetwPeerContext:
	if not session:
		return null
	return session.get_peer_context(get_unique_id())


func get_service(type: Script) -> Node:
	return session.get_service(type)


func get_spawn_slot(spawner_path: SceneNodePath) -> MultiplayerTree.SpawnSlot:
	return session.get_spawn_slot(spawner_path)


func get_lobby_manager() -> MultiplayerLobbyManager:
	return session.get_lobby_manager()


func get_clock() -> NetworkClock:
	return session.get_clock()


func begin_span(
	label: String,
	meta: Dictionary = {},
	follows_from: CheckpointToken = null
) -> NetSpan:
	return session.begin_span(label, meta, follows_from)


func begin_peer_span(
	label: String,
	peers: Array = [],
	meta: Dictionary = {},
	follows_from: CheckpointToken = null
) -> NetPeerSpan:
	return session.begin_peer_span(label, peers, meta, follows_from)


# ---------------------------------------------------------------------------
# Lobby-layer pass-throughs (guard with has_lobby())
# ---------------------------------------------------------------------------

func get_lobby_name() -> StringName:
	if not has_lobby():
		return &""
	return lobby.get_lobby_name()


func get_players() -> Array[Node]:
	if not has_lobby():
		return []
	return lobby.get_players()


func get_player_count() -> int:
	if not has_lobby():
		return 0
	return lobby.get_player_count()


func get_local_player() -> Node:
	if not has_lobby():
		return null
	return lobby.get_local_player()


func get_player_by_peer_id(peer_id: int) -> Node:
	if not has_lobby():
		return null
	return lobby.get_player_by_peer_id(peer_id)


func wait_for_players(n: int) -> void:
	if not has_lobby():
		return
	await lobby.wait_for_players(n)


func pause(reason: String = "") -> void:
	if not has_lobby():
		return
	lobby.pause(reason)


func unpause() -> void:
	if not has_lobby():
		return
	lobby.unpause()


func suspend(reason: String = "") -> void:
	if not has_lobby():
		return
	lobby.suspend(reason)


func request_suspend(reason: String = "") -> void:
	if not has_lobby():
		return
	lobby.request_suspend(reason)


func resume() -> void:
	if not has_lobby():
		return
	lobby.resume()


func kick(peer_id: int, reason: String = "") -> void:
	if not has_lobby():
		return
	lobby.kick(peer_id, reason)


func request_kick(peer_id: int, reason: String = "") -> void:
	if not has_lobby():
		return
	lobby.request_kick(peer_id, reason)


func start_countdown(seconds: int) -> NetwLobbyCountdown:
	if not has_lobby():
		return null
	return lobby.start_countdown(seconds)


func cancel_countdown() -> void:
	if not has_lobby():
		return
	lobby.cancel_countdown()


func create_readiness_gate() -> NetwLobbyReadiness:
	if not has_lobby():
		return null
	return lobby.create_readiness_gate()


# ---------------------------------------------------------------------------
# Static access
# ---------------------------------------------------------------------------

static func for_node(node: Node) -> NetwContext:
	var lobby := MultiplayerTree.lobby_for_node(node)
	return lobby.get_context() if is_instance_valid(lobby) else null
