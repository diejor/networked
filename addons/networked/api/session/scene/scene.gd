## Scene-scoped facade providing player tracking, lifecycle signals, and
## server operations.
##
## Access via [method NetwComponent.get_context] or
## [method NetwScene.for_node]. Holds a [WeakRef] to the underlying
## [MultiplayerScene] - check [method is_valid] before use.
## [codeblock]
## var ctx := get_context()
##
## # Wait for players then count down
## await ctx.scene.wait_for_players(4)
## var cd := ctx.scene.start_countdown(10)
## await cd.finished
## start_match()
## [/codeblock]
class_name NetwScene
extends RefCounted

# ---------------------------------------------------------------------------
# Player lifecycle signals
# ---------------------------------------------------------------------------

## Emitted when a player's node is spawned into this scene.
signal player_entered(player: Node)
## Emitted when a player's node is despawned from this scene.
signal player_left(player: Node)
## Emitted when a player toggles their ready state to [code]true[/code] via
## [NetwSceneReadiness].[br][br]This is a manual ready-state signal, not an
## automatic join event. See [signal player_entered] for spawn detection.
signal player_ready(join_payload: JoinPayload)

# ---------------------------------------------------------------------------
# Suspend / resume signals  (soft, signal-only, game code decides)
# ---------------------------------------------------------------------------

## Emitted on all peers when the server calls [method suspend].
## Game code decides what to do (show a banner, disable input, ...).
## Does not affect [code]get_tree().paused[/code] - use [method pause] for that.
signal suspended(reason: String)
## Emitted on all peers when the server calls [method resume].
signal resumed()
## Emitted on the server when a client calls [method request_suspend].
signal suspend_requested(peer_id: int, reason: String)

# ---------------------------------------------------------------------------
# Countdown signals
# ---------------------------------------------------------------------------

## Emitted on clients when the server starts a countdown (via [method start_countdown]).
## Use this to initialise client-side UI before the first tick arrives.
signal countdown_started(seconds: int)
## Emitted each second on all peers with the remaining second count.
signal countdown_tick(seconds_left: int)
## Emitted on all peers when the countdown reaches zero.
signal countdown_finished()
## Emitted on all peers when [method cancel_countdown] is called.
signal countdown_cancelled()

# ---------------------------------------------------------------------------
# Internal state
# ---------------------------------------------------------------------------

var _scene_ref: WeakRef
# Held strongly while the countdown is running so the timer stays alive.
var _active_countdown: NetwSceneCountdown
var _tree: NetwTree


func _init(scene: MultiplayerScene) -> void:
	_scene_ref = weakref(scene)
	scene.synchronizer.spawned.connect(_on_spawned)
	scene.synchronizer.despawned.connect(_on_despawned)
	scene.player_ready.connect(_on_player_ready)


# ---------------------------------------------------------------------------
# Validity / identity queries
# ---------------------------------------------------------------------------

## Returns [code]true[/code] while the underlying [Scene] is still alive.
func is_valid() -> bool:
	return is_instance_valid(_scene_ref.get_ref())


## Returns the underlying [MultiplayerScene], or [code]null[/code].
func unwrap() -> MultiplayerScene:
	return _scene_ref.get_ref() as MultiplayerScene


## Returns the scene level root, or [code]null[/code].
func get_level() -> Node:
	var scene := unwrap()
	if not is_instance_valid(scene):
		return null
	return scene.level


## Returns the level scene root name for this scene.
## Returns [code]""[/code] if the scene or its level is not valid.
func get_scene_name() -> StringName:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene) or not is_instance_valid(scene.level):
		return &""
	return StringName(scene.level.name)


## Returns the [NetwTree] that owns this scene, or [code]null[/code].
##
## Use this to access tree-level APIs (e.g., [method NetwTree.is_listen_server])
## from scene-scoped code.
func tree() -> NetwTree:
	if _tree == null or not _tree.is_valid():
		var scene := _scene_ref.get_ref() as MultiplayerScene
		if is_instance_valid(scene):
			var mt := MultiplayerTree.for_node(scene)
			if mt:
				_tree = NetwTree.new(mt)
	return _tree


## Returns the peer IDs currently connected to this scene.
##
## Use this to enumerate peers when sending custom broadcast RPCs:
## [codeblock]
## for peer_id in ctx.scene.get_peers():
##     _rpc_notify.rpc_id(peer_id, message)
## [/codeblock]
func get_peers() -> Array[int]:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene) or not is_instance_valid(scene.synchronizer):
		return []
	var result: Array[int] = []
	result.assign(scene.synchronizer.connected_peers.keys())
	return result


# ---------------------------------------------------------------------------
# Player queries
# ---------------------------------------------------------------------------

## Returns all player nodes currently in this scene.
func get_players() -> Array[Node]:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene):
		return []
	return scene.get_players()


## Returns the number of players currently in this scene.
func get_player_count() -> int:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene):
		return 0
	return scene.get_players().size()


## Returns the player node owned by the local peer, or [code]null[/code].
func get_local_player() -> Node:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene):
		return null
	var local_id := scene.multiplayer.get_unique_id()
	for player: Node in scene.get_players():
		if _get_peer_id(player) == local_id:
			return player
	return null


## Returns the player node owned by [param peer_id], or [code]null[/code].
func get_player_by_peer_id(peer_id: int) -> Node:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene):
		return null
	for player: Node in scene.get_players():
		if _get_peer_id(player) == peer_id:
			return player
	return null


## Suspends until at least [param n] players are present. Safe to [operator await].
func wait_for_players(n: int) -> void:
	while get_player_count() < n:
		await player_entered


# ---------------------------------------------------------------------------
# Static access
# ---------------------------------------------------------------------------

## Returns the [NetwScene] for [param node] by walking its ancestor chain.
##
## Returns [code]null[/code] if [param node] is not inside an active [Scene].
static func for_node(node: Node) -> NetwScene:
	var ctx := NetwContext.for_node(node)
	return ctx.scene if ctx and ctx.has_scene() else null


# ---------------------------------------------------------------------------
# Lifecycle cleanup
# ---------------------------------------------------------------------------

## Cleans up all internal state and signal connections.
## Called automatically when the underlying [MultiplayerScene] exits the tree.
func close() -> void:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if is_instance_valid(scene) and is_instance_valid(scene.synchronizer):
		if scene.synchronizer.spawned.is_connected(_on_spawned):
			scene.synchronizer.spawned.disconnect(_on_spawned)
		if scene.synchronizer.despawned.is_connected(_on_despawned):
			scene.synchronizer.despawned.disconnect(_on_despawned)
		if scene.player_ready.is_connected(_on_player_ready):
			scene.player_ready.disconnect(_on_player_ready)
	if _active_countdown:
		_active_countdown.cancel()
		_active_countdown = null
	_scene_ref = null


# ---------------------------------------------------------------------------
# Soft suspend / resume  (signal-only, game code decides)
# ---------------------------------------------------------------------------

## Broadcasts a soft-suspend notification to scene peers.
##
## [b]Server-only.[/b] Does [b]not[/b] touch [code]get_tree().paused[/code] -
## each peer receives [signal suspended] and game code decides the response
## (show a banner, lock input, wait for a cutscene, ...).
func suspend(reason: String = "") -> void:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene):
		return
	assert(scene.multiplayer.is_server(),
		"NetwScene.suspend() must be called on the server.")
	for peer_id: int in scene.synchronizer.connected_peers:
		if peer_id == scene.multiplayer.get_unique_id():
			continue
		scene._rpc_receive_suspend.rpc_id(peer_id, reason)
	suspended.emit(reason)


## Asks the server to suspend the scene.
##
## [b]Client-only.[/b] The server emits [signal suspend_requested]; game code
## decides whether to honour it.
func request_suspend(reason: String = "") -> void:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene):
		return
	scene._rpc_request_suspend.rpc_id(1, reason)


## Broadcasts a resume notification to scene peers.
##
## [b]Server-only.[/b] Mirrors [method suspend] - does not touch
## [code]get_tree().paused[/code].
func resume() -> void:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene):
		return
	assert(scene.multiplayer.is_server(),
		"NetwScene.resume() must be called on the server.")
	for peer_id: int in scene.synchronizer.connected_peers:
		if peer_id == scene.multiplayer.get_unique_id():
			continue
		scene._rpc_receive_resume.rpc_id(peer_id)
	resumed.emit()


# ---------------------------------------------------------------------------
# Countdown
# ---------------------------------------------------------------------------

## Starts a server-driven countdown of [param seconds] seconds.
##
## [b]Server-only.[/b] Returns a [NetwSceneCountdown] you can [code]await[/code].
## Clients receive [signal countdown_started] followed by [signal countdown_tick]
## each second, and finally [signal countdown_finished] (or
## [signal countdown_cancelled] if [method cancel_countdown] is called first).
## Any previously running countdown is cancelled automatically.
func start_countdown(seconds: int) -> NetwSceneCountdown:
	assert(seconds > 0, "NetwScene.start_countdown(): seconds must be > 0.")
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene):
		return null
	assert(scene.multiplayer.is_server(),
		"NetwScene.start_countdown() must be called on the server.")

	cancel_countdown()

	var cd := NetwSceneCountdown.new(scene, seconds)
	_active_countdown = cd

	cd.tick.connect(_on_countdown_tick)
	cd.finished.connect(_on_countdown_finished)
	cd.cancelled.connect(_on_countdown_cancelled)

	# Notify clients before the first tick so they can prepare UI
	for peer_id: int in scene.synchronizer.connected_peers:
		if peer_id == scene.multiplayer.get_unique_id():
			continue
		scene._rpc_receive_countdown_started.rpc_id(peer_id, seconds)
	countdown_started.emit(seconds)

	cd._start()
	return cd


## Cancels the currently running countdown, if any.
##
## [b]Server-only.[/b] Emits [signal countdown_cancelled] on all peers.
func cancel_countdown() -> void:
	if _active_countdown and _active_countdown.is_running():
		_active_countdown.cancel()
	_active_countdown = null


# ---------------------------------------------------------------------------
# Readiness gate
# ---------------------------------------------------------------------------

## Creates and returns a new [NetwSceneReadiness] gate for this scene.
##
## The gate is pre-populated with all currently connected players (all marked
## not-ready). Players that join or leave after creation are tracked
## automatically. Multiple independent gates can be active simultaneously.
func create_readiness_gate() -> NetwSceneReadiness:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene):
		return null
	var gate := NetwSceneReadiness.new(scene)
	scene._register_readiness_gate(gate)
	for player: Node in scene.get_players():
		gate._add_peer(_get_peer_id(player))
	return gate


# ---------------------------------------------------------------------------
# Internal signal handlers
# ---------------------------------------------------------------------------

func _on_spawned(player: Node) -> void:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene) or not (player in scene.get_players()):
		return
	player_entered.emit(player)
	scene._notify_gates_player_added(_get_peer_id(player))


func _on_despawned(player: Node) -> void:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene) or not (player in scene.get_players()):
		return
	player_left.emit(player)
	scene._notify_gates_player_removed(_get_peer_id(player))


func _on_countdown_tick(seconds_left: int) -> void:
	countdown_tick.emit(seconds_left)
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if is_instance_valid(scene):
		for peer_id: int in scene.synchronizer.connected_peers:
			if peer_id == scene.multiplayer.get_unique_id():
				continue
			scene._rpc_receive_countdown_tick.rpc_id(peer_id, seconds_left)


func _on_countdown_finished() -> void:
	countdown_finished.emit()
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if is_instance_valid(scene):
		for peer_id: int in scene.synchronizer.connected_peers:
			if peer_id == scene.multiplayer.get_unique_id():
				continue
			scene._rpc_receive_countdown_finished.rpc_id(peer_id)
	_active_countdown = null


func _on_countdown_cancelled() -> void:
	countdown_cancelled.emit()
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if is_instance_valid(scene):
		for peer_id: int in scene.synchronizer.connected_peers:
			if peer_id == scene.multiplayer.get_unique_id():
				continue
			scene._rpc_receive_countdown_cancelled.rpc_id(peer_id)
	_active_countdown = null


func _on_player_ready(join_payload: JoinPayload) -> void:
	player_ready.emit(join_payload)


func _get_peer_id(node: Node) -> int:
	var entity := NetwEntity.of(node)
	if entity and entity.peer_id != 0:
		return entity.peer_id
	return NetwEntity.parse_peer(node.name)
