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
signal player_ready(client_data: MultiplayerClientData)

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


## Returns the level scene root name for this scene.
## Returns [code]""[/code] if the scene or its level is not valid.
func get_scene_name() -> StringName:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene) or not is_instance_valid(scene.level):
		return &""
	return StringName(scene.level.name)


# ---------------------------------------------------------------------------
# Player queries
# ---------------------------------------------------------------------------

## Returns all player nodes currently in this scene.
func get_players() -> Array[Node]:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene):
		return []
	var result: Array[Node] = []
	result.assign(scene.synchronizer.tracked_nodes.keys())
	return result


## Returns the number of players currently in this scene.
func get_player_count() -> int:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene):
		return 0
	return scene.synchronizer.tracked_nodes.size()


## Returns the player node owned by the local peer, or [code]null[/code].
func get_local_player() -> Node:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene):
		return null
	var local_id := scene.multiplayer.get_unique_id()
	for player: Node in scene.synchronizer.tracked_nodes:
		if player.get_multiplayer_authority() == local_id:
			return player
	return null


## Returns the player node owned by [param peer_id], or [code]null[/code].
func get_player_by_peer_id(peer_id: int) -> Node:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene):
		return null
	for player: Node in scene.synchronizer.tracked_nodes:
		if player.get_multiplayer_authority() == peer_id:
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
	for node: Node in scene.synchronizer.tracked_nodes:
		var peer_id := node.get_multiplayer_authority()
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
	for node: Node in scene.synchronizer.tracked_nodes:
		var peer_id := node.get_multiplayer_authority()
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
	for node: Node in scene.synchronizer.tracked_nodes:
		var peer_id := node.get_multiplayer_authority()
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
	for player: Node in scene.synchronizer.tracked_nodes:
		gate._add_peer(player.get_multiplayer_authority())
	return gate


# ---------------------------------------------------------------------------
# Internal signal handlers
# ---------------------------------------------------------------------------

func _on_spawned(player: Node) -> void:
	player_entered.emit(player)
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if is_instance_valid(scene):
		scene._notify_gates_player_added(player.get_multiplayer_authority())


func _on_despawned(player: Node) -> void:
	player_left.emit(player)
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if is_instance_valid(scene):
		scene._notify_gates_player_removed(player.get_multiplayer_authority())


func _on_countdown_tick(seconds_left: int) -> void:
	countdown_tick.emit(seconds_left)
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if is_instance_valid(scene):
		for node: Node in scene.synchronizer.tracked_nodes:
			var peer_id := node.get_multiplayer_authority()
			scene._rpc_receive_countdown_tick.rpc_id(peer_id, seconds_left)


func _on_countdown_finished() -> void:
	countdown_finished.emit()
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if is_instance_valid(scene):
		for node: Node in scene.synchronizer.tracked_nodes:
			var peer_id := node.get_multiplayer_authority()
			scene._rpc_receive_countdown_finished.rpc_id(peer_id)
	_active_countdown = null


func _on_countdown_cancelled() -> void:
	countdown_cancelled.emit()
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if is_instance_valid(scene):
		for node: Node in scene.synchronizer.tracked_nodes:
			var peer_id := node.get_multiplayer_authority()
			scene._rpc_receive_countdown_cancelled.rpc_id(peer_id)
	_active_countdown = null


func _on_player_ready(client_data: MultiplayerClientData) -> void:
	player_ready.emit(client_data)
