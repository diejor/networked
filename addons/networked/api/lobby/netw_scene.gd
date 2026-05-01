## Scene-scoped facade providing player tracking, lifecycle signals, and
## server operations.
##
## Access via [method NetwComponent.get_context] or
## [method NetwScene.for_node]. Holds a [WeakRef] to the underlying
## [Scene] - check [method is_valid] before use.
## [codeblock]
## var ctx := get_context()
##
## # Wait for players then count down
## await ctx.scene.wait_for_players(4)
## var cd := ctx.scene.start_countdown(10)
## await cd.finished
## start_match()
##
## # React to a networked pause (get_tree().paused on all peers)
## ctx.scene.paused.connect(func(r): $PauseUI.show())
## ctx.scene.unpaused.connect(func(): $PauseUI.hide())
## [/codeblock]
class_name NetwScene
extends RefCounted

# ---------------------------------------------------------------------------
# Player lifecycle signals
# ---------------------------------------------------------------------------

## Emitted when a player enters this scene.
signal player_entered(player: Node)
## Emitted when a player leaves this scene.
signal player_left(player: Node)

# ---------------------------------------------------------------------------
# Pause / unpause signals  (hard, get_tree().paused, all peers)
# ---------------------------------------------------------------------------

## Emitted on every peer (including the server) after [method pause] sets
## [code]get_tree().paused = true[/code].
signal paused(reason: String)
## Emitted on every peer (including the server) after [method unpause] clears
## [code]get_tree().paused[/code].
signal unpaused()

# ---------------------------------------------------------------------------
# Suspend / resume signals  (soft, signal-only, game code decides)
# ---------------------------------------------------------------------------

## Emitted on all peers when the server calls [method suspend].
## Game code decides what to do (show a banner, disable input, …).
## Does not affect [code]get_tree().paused[/code] — use [method pause] for that.
signal suspended(reason: String)
## Emitted on all peers when the server calls [method resume].
signal resumed()
## Emitted on the server when a client calls [method request_suspend].
signal suspend_requested(peer_id: int, reason: String)

# ---------------------------------------------------------------------------
# Kick signals
# ---------------------------------------------------------------------------

## Emitted on the kicked peer just before the server closes their connection.
signal kicked(reason: String)
## Emitted on the server when a client calls [method request_kick].
signal kick_requested(requester_id: int, target_id: int, reason: String)

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
## Held strongly while the countdown is running so the timer stays alive.
var _active_countdown: NetwSceneCountdown


func _init(scene: MultiplayerScene) -> void:
	_scene_ref = weakref(scene)
	scene.synchronizer.spawned.connect(_on_spawned)
	scene.synchronizer.despawned.connect(_on_despawned)


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
# Pause / unpause  (hard, get_tree().paused, broadcast to all peers)
# ---------------------------------------------------------------------------

## Pauses the game on every peer via [code]get_tree().paused = true[/code].
##
## [b]Server-only.[/b] The pause is broadcast immediately — all peers execute
## it in the same network pass. [signal paused] fires on every peer so game
## code can show a pause UI or disable input. Nodes with
## [constant Node.PROCESS_MODE_ALWAYS] (e.g. pause menus) continue to run.
## [br][br]
## In multi-scene setups this pauses [i]all[/i] scenes on each peer, because
## it operates on the [SceneTree]. If you need a scene-scoped process disable
## without touching other scenes, use [method suspend] instead.
func pause(reason: String = "") -> void:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene):
		return
	assert(scene.multiplayer.is_server(),
		"NetwScene.pause() must be called on the server.")
	scene._rpc_receive_pause.rpc(reason)


## Unpauses the game on every peer via [code]get_tree().paused = false[/code].
##
## [b]Server-only.[/b]
func unpause() -> void:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene):
		return
	assert(scene.multiplayer.is_server(),
		"NetwScene.unpause() must be called on the server.")
	scene._rpc_receive_unpause.rpc()


# ---------------------------------------------------------------------------
# Soft suspend / resume  (signal-only, game code decides)
# ---------------------------------------------------------------------------

## Broadcasts a soft-suspend notification to all peers.
##
## [b]Server-only.[/b] Does [b]not[/b] touch [code]get_tree().paused[/code] —
## each peer receives [signal suspended] and game code decides the response
## (show a banner, lock input, wait for a cutscene, …).
## Call [method pause] afterwards if you also want to stop processing.
func suspend(reason: String = "") -> void:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene):
		return
	assert(scene.multiplayer.is_server(),
		"NetwScene.suspend() must be called on the server.")
	scene._rpc_receive_suspend.rpc(reason)
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


## Broadcasts a resume notification to all peers.
##
## [b]Server-only.[/b] Mirrors [method suspend] — does not touch
## [code]get_tree().paused[/code]. Call [method unpause] to also unfreeze
## the tree.
func resume() -> void:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene):
		return
	assert(scene.multiplayer.is_server(),
		"NetwScene.resume() must be called on the server.")
	scene._rpc_receive_resume.rpc()
	resumed.emit()


# ---------------------------------------------------------------------------
# Kick
# ---------------------------------------------------------------------------

## Disconnects [param peer_id] from the session.
##
## [b]Server-only.[/b] If [param reason] is non-empty, the peer receives
## [signal kicked] before the connection is closed. The normal
## [signal player_left] flow follows naturally via the disconnect.
func kick(peer_id: int, reason: String = "") -> void:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene):
		return
	assert(scene.multiplayer.is_server(),
		"NetwScene.kick() must be called on the server.")
	if not reason.is_empty():
		scene._rpc_receive_kicked.rpc_id(peer_id, reason)
	var mt := MultiplayerTree.for_node(scene)
	if mt and mt.multiplayer_peer:
		mt.multiplayer_peer.disconnect_peer(peer_id)


## Asks the server to kick [param peer_id].
##
## [b]Client-only.[/b] The server emits [signal kick_requested]; game code
## decides whether to honour it.
func request_kick(peer_id: int, reason: String = "") -> void:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene):
		return
	scene._rpc_request_kick.rpc_id(1, peer_id, reason)


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
	scene._rpc_receive_countdown_started.rpc(seconds)
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
		scene._rpc_receive_countdown_tick.rpc(seconds_left)


func _on_countdown_finished() -> void:
	countdown_finished.emit()
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if is_instance_valid(scene):
		scene._rpc_receive_countdown_finished.rpc()
	_active_countdown = null


func _on_countdown_cancelled() -> void:
	countdown_cancelled.emit()
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if is_instance_valid(scene):
		scene._rpc_receive_countdown_cancelled.rpc()
	_active_countdown = null
