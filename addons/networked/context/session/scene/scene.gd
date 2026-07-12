## Scene-scoped facade providing player tracking, lifecycle signals, and
## server operations.
##
## Access via [method NetwScene.for_node] or [member MultiplayerScene.netw_scene].
## Holds a [WeakRef] to the underlying [MultiplayerScene] - check
## [method is_valid] before use.
## [codeblock]
## var scene := NetwScene.for_node(self)
##
## # Wait for players then count down
## await scene.wait_for_players(4)
## var cd := scene.start_countdown(10)
## await cd.finished
## start_match()
## [/codeblock]
class_name NetwScene
extends RefCounted

# ---------------------------------------------------------------------------
# Player lifecycle signals
# ---------------------------------------------------------------------------

## Emitted when a player's identity is spawned into this scene.
signal player_entered(player: NetwEntity)
## Emitted when a player's identity is despawned from this scene.
signal player_left(player: NetwEntity)
## Emitted when a participant is admitted to this scene.
signal participant_entered(participant: NetwParticipant)
## Emitted when a participant is released from this scene.
signal participant_left(participant: NetwParticipant)

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

## Emitted on clients when the server starts a countdown via
## [method start_countdown].
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
var _admission_layer: NetwInterestLayer
var _admission_tree: MultiplayerTree
var _pending_admitted_peers: Dictionary[int, bool] = { }


func _init(scene: MultiplayerScene) -> void:
	_scene_ref = weakref(scene)
	scene.spawned.connect(_on_spawned)
	scene.despawned.connect(_on_despawned)
	scene.suspended.connect(suspended.emit)
	scene.resumed.connect(resumed.emit)
	scene.suspend_requested.connect(suspend_requested.emit)
	scene.countdown_started.connect(countdown_started.emit)
	scene.countdown_tick.connect(countdown_tick.emit)
	scene.countdown_finished.connect(countdown_finished.emit)
	scene.countdown_cancelled.connect(countdown_cancelled.emit)
	if scene.multiplayer and scene.multiplayer.is_server():
		scene.peer_admitted.connect(_on_peer_admitted)
		scene.peer_released.connect(_on_peer_released)
	else:
		_bind_client_admission_layer(scene)

# ---------------------------------------------------------------------------
# Validity / identity queries
# ---------------------------------------------------------------------------


## Returns [code]true[/code] while the underlying [Scene] is still alive.
func is_valid() -> bool:
	if _scene_ref == null:
		return false
	return is_instance_valid(_scene_ref.get_ref())


## Returns the underlying [MultiplayerScene], or [code]null[/code].
func unwrap() -> MultiplayerScene:
	return _scene_ref.get_ref() as MultiplayerScene

## The scene level root, or [code]null[/code].
var level: Node:
	get:
		var scene := unwrap()
		if not is_instance_valid(scene):
			return null
		return scene.level

## The level scene root name for this scene.
## Returns [code]""[/code] if the scene or its level is not valid.
var scene_name: StringName:
	get:
		var scene := _scene_ref.get_ref() as MultiplayerScene
		if not is_instance_valid(scene) or not is_instance_valid(scene.level):
			return &""
		return StringName(scene.level.name)


## Returns the [NetwMultiplayer] session that owns this scene, or
## [code]null[/code].
##
## Use this to access session-level APIs (e.g.,
## [method NetwMultiplayer.is_listen_server]) from scene-scoped code.
func tree() -> NetwMultiplayer:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	return NetwMultiplayer.of(scene) if is_instance_valid(scene) else null

## The peer IDs currently connected to this scene.
##
## Use this to enumerate peers when sending custom broadcast RPCs:
## [codeblock]
## for peer_id in scene.peers:
##     _rpc_notify.rpc_id(peer_id, message)
## [/codeblock]
var peers: Array[int]:
	get:
		var scene := _scene_ref.get_ref() as MultiplayerScene
		if not is_instance_valid(scene):
			return []
		var result: Array[int] = []
		result.assign(scene.connected_peers.keys())
		return result

## Participants currently admitted to this scene.
var participants: Array[NetwParticipant]:
	get:
		var scene := _scene_ref.get_ref() as MultiplayerScene
		if not is_instance_valid(scene):
			return []
		return scene.participants

# ---------------------------------------------------------------------------
# Player queries
# ---------------------------------------------------------------------------

## All player identities currently in this scene.
var players: Array[NetwEntity]:
	get:
		var scene := _scene_ref.get_ref() as MultiplayerScene
		if not is_instance_valid(scene):
			return []
		return scene.get_players()

## The number of players currently in this scene.
var player_count: int:
	get:
		var scene := _scene_ref.get_ref() as MultiplayerScene
		if not is_instance_valid(scene):
			return 0
		return scene.get_players().size()

## The player identity owned by the local peer, or [code]null[/code].
var local_player: NetwEntity:
	get:
		var scene := _scene_ref.get_ref() as MultiplayerScene
		if not is_instance_valid(scene):
			return null
		var local_id := scene.multiplayer.get_unique_id()
		for player: NetwEntity in scene.get_players():
			if player.peer_id == local_id:
				return player
		return null


## Returns the player identity owned by [param peer_id], or [code]null[/code].
func get_player_by_peer_id(peer_id: int) -> NetwEntity:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene):
		return null
	for player: NetwEntity in scene.get_players():
		if player.peer_id == peer_id:
			return player
	return null


## Suspends until at least [param n] players are present.
## Safe to [operator await].
func wait_for_players(n: int) -> void:
	while player_count < n:
		await player_entered


## Suspends until at least [param n] participants are admitted.
## Safe to [operator await].
func wait_for_participants(n: int) -> void:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if is_instance_valid(scene):
		await scene.wait_for_participants(n)

# ---------------------------------------------------------------------------
# Static access
# ---------------------------------------------------------------------------


## Returns the [NetwScene] for [param node] by walking its ancestor chain.
##
## Returns [code]null[/code] if [param node] is not inside an active [Scene].
static func for_node(node: Node) -> NetwScene:
	var scene_node := MultiplayerTree.scene_for_node(node)
	return scene_node.netw_scene if is_instance_valid(scene_node) else null

# ---------------------------------------------------------------------------
# Lifecycle cleanup
# ---------------------------------------------------------------------------


## Cleans up all internal state and signal connections.
## Called automatically when the underlying [MultiplayerScene] exits the tree.
func close() -> void:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if is_instance_valid(scene):
		if scene.spawned.is_connected(_on_spawned):
			scene.spawned.disconnect(_on_spawned)
		if scene.despawned.is_connected(_on_despawned):
			scene.despawned.disconnect(_on_despawned)
		if scene.suspended.is_connected(suspended.emit):
			scene.suspended.disconnect(suspended.emit)
		if scene.resumed.is_connected(resumed.emit):
			scene.resumed.disconnect(resumed.emit)
		if scene.suspend_requested.is_connected(suspend_requested.emit):
			scene.suspend_requested.disconnect(suspend_requested.emit)
		if scene.countdown_started.is_connected(countdown_started.emit):
			scene.countdown_started.disconnect(countdown_started.emit)
		if scene.countdown_tick.is_connected(countdown_tick.emit):
			scene.countdown_tick.disconnect(countdown_tick.emit)
		if scene.countdown_finished.is_connected(countdown_finished.emit):
			scene.countdown_finished.disconnect(countdown_finished.emit)
		if scene.countdown_cancelled.is_connected(countdown_cancelled.emit):
			scene.countdown_cancelled.disconnect(countdown_cancelled.emit)
		if scene.peer_admitted.is_connected(_on_peer_admitted):
			scene.peer_admitted.disconnect(_on_peer_admitted)
		if scene.peer_released.is_connected(_on_peer_released):
			scene.peer_released.disconnect(_on_peer_released)
	if _admission_layer:
		if _admission_layer.viewer_added.is_connected(_on_peer_admitted):
			_admission_layer.viewer_added.disconnect(_on_peer_admitted)
		if _admission_layer.viewer_removed.is_connected(_on_peer_released):
			_admission_layer.viewer_removed.disconnect(_on_peer_released)
	if _admission_tree \
			and _admission_tree.participant_joined.is_connected(
				_on_tree_participant_joined,
			):
		_admission_tree.participant_joined.disconnect(
			_on_tree_participant_joined,
		)
	_scene_ref = weakref(null)

# ---------------------------------------------------------------------------
# Soft suspend / resume  (signal-only, game code decides)
# ---------------------------------------------------------------------------


## Admits [param participant] to this scene without spawning a player node.
##
## [br][br][b]Server Only.[/b]
func admit(participant: NetwParticipant) -> void:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if is_instance_valid(scene):
		scene.admit(participant)


## Releases [param participant] from this scene without touching player nodes.
##
## [br][br][b]Server Only.[/b]
func release(participant: NetwParticipant) -> void:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if is_instance_valid(scene):
		scene.release(participant)


## Moves [param moving_participants] into this scene.
##
## Returns a [NetwScene.MoveBatch] that emits
## [signal NetwScene.MoveBatch.participant_arrived] for each participant and
## [signal NetwScene.MoveBatch.completed] once all moves have been applied.
## [br][br][b]Server Only.[/b]
func move_participants(
		moving_participants: Array[NetwParticipant],
) -> MoveBatch:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	return scene.move_participants(moving_participants) \
	if is_instance_valid(scene) else null


## Broadcasts a soft-suspend notification to scene peers.
##
## Does [b]not[/b] touch [code]get_tree().paused[/code]. Each peer
## receives [signal suspended] and game code decides the response
## (show a banner, lock input, wait for a cutscene, ...).
## [br][br][b]Server Only.[/b]
func suspend(reason: String = "") -> void:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if is_instance_valid(scene):
		scene.suspend(reason)


## Asks the server to suspend the scene.
##
## The server emits [signal suspend_requested] and decides whether to honor it.
## [br][br][b]Player request.[/b]
func request_suspend(reason: String = "") -> void:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if is_instance_valid(scene):
		scene.request_suspend(reason)


## Broadcasts a resume notification to scene peers.
##
## Mirrors [method suspend]. Does not touch [code]get_tree().paused[/code].
## [br][br][b]Server Only.[/b]
func resume() -> void:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if is_instance_valid(scene):
		scene.resume()

# ---------------------------------------------------------------------------
# Countdown
# ---------------------------------------------------------------------------


## Starts a server-driven countdown of [param seconds] seconds.
##
## Returns a [NetwScene.Countdown] you can [code]await[/code]. Clients receive
## [signal countdown_started] followed by [signal countdown_tick] each second,
## and finally [signal countdown_finished] (or [signal countdown_cancelled] if
## [method cancel_countdown] is called first). Any previously running
## countdown is cancelled automatically. [param tick_interval] defaults to
## one second.
## [br][br][b]Server Only.[/b]
func start_countdown(
		seconds: int,
		tick_interval: float = 1.0,
) -> Countdown:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	return scene.start_countdown(seconds, tick_interval) \
	if is_instance_valid(scene) else null


## Cancels the currently running countdown, if any.
##
## Emits [signal countdown_cancelled] on all peers.
## [br][br][b]Server Only.[/b]
func cancel_countdown() -> void:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if is_instance_valid(scene):
		scene.cancel_countdown()

# ---------------------------------------------------------------------------
# Readiness gate
# ---------------------------------------------------------------------------


## Creates and returns a new [NetwScene.Readiness] gate for this scene.
##
## The gate is pre-populated with all currently connected players (all marked
## not-ready). Players that join or leave after creation are tracked
## automatically. Multiple independent gates can be active simultaneously.
func create_readiness_gate() -> Readiness:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	return scene.create_readiness_gate() if is_instance_valid(scene) else null

# ---------------------------------------------------------------------------
# Internal signal handlers
# ---------------------------------------------------------------------------


func _on_spawned(player: Node) -> void:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	var entity := NetwEntity.of(player)
	if not is_instance_valid(scene) or not (entity in scene.get_players()):
		return
	player_entered.emit(entity)


func _on_despawned(player: Node) -> void:
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if not is_instance_valid(scene):
		return
	var peer_id := _get_peer_id(player)
	if peer_id == 0:
		return
	var entity := NetwEntity.of(player)
	player_left.emit(entity)


func _on_peer_admitted(peer_id: int) -> void:
	var netw_tree := tree()
	if netw_tree == null:
		return
	var participant := netw_tree.participant(peer_id)
	if participant == null:
		_pending_admitted_peers[peer_id] = true
		return
	_pending_admitted_peers.erase(peer_id)
	participant.current_scene = self
	participant_entered.emit(participant)
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if is_instance_valid(scene):
		scene._notify_gates_player_added(peer_id)


func _on_peer_released(peer_id: int) -> void:
	var netw_tree := tree()
	if netw_tree == null:
		return
	var participant := netw_tree.participant(peer_id)
	if participant == null:
		return
	if _is_current_scene(participant):
		_clear_current_scene_if_still_current.call_deferred(
			participant,
			unwrap().get_instance_id(),
		)
	participant_left.emit(participant)
	var scene := _scene_ref.get_ref() as MultiplayerScene
	if is_instance_valid(scene):
		scene._notify_gates_player_removed(peer_id)


func _is_current_scene(participant: NetwParticipant) -> bool:
	var current := participant.current_scene
	if current == null:
		return false
	return current.unwrap() == unwrap()


func _clear_current_scene_if_still_current(
		participant: NetwParticipant,
		scene_instance_id: int,
) -> void:
	var current := participant.current_scene
	if current and current.unwrap() \
			and current.unwrap().get_instance_id() == scene_instance_id:
		participant.current_scene = null


func _bind_client_admission_layer(scene: MultiplayerScene) -> void:
	var mt := MultiplayerTree.for_node(scene)
	if mt == null or mt.api == null:
		return
	_admission_tree = mt
	if not mt.participant_joined.is_connected(_on_tree_participant_joined):
		mt.participant_joined.connect(_on_tree_participant_joined)
	var l := mt.api.interest.layer(scene.scene_layer_id())
	if l == null:
		return
	_admission_layer = l
	if not l.viewer_added.is_connected(_on_peer_admitted):
		l.viewer_added.connect(_on_peer_admitted)
	if not l.viewer_removed.is_connected(_on_peer_released):
		l.viewer_removed.connect(_on_peer_released)
	for peer_id: int in l.viewers:
		_on_peer_admitted(peer_id)


func _on_tree_participant_joined(participant: NetwParticipant) -> void:
	if _admission_layer == null:
		return
	if not _admission_layer.viewers.has(participant.peer_id):
		return
	_on_peer_admitted(participant.peer_id)


func _get_peer_id(node: Node) -> int:
	var entity := NetwEntity.of(node)
	if entity and entity.peer_id != 0:
		return entity.peer_id
	return NetwEntity.parse_peer(node.name)


## Server-driven countdown that ticks once per second.
##
## Obtain via [method NetwScene.start_countdown] - do not construct directly.
## Clients do not receive a return value; they listen to
## [signal NetwScene.countdown_started] and the subsequent
## [signal NetwScene.countdown_tick] / [signal NetwScene.countdown_finished]
## signals, which are broadcast automatically.
## [codeblock]
## # Server:
## var cd := scene.start_countdown(10)
## await cd.finished
## start_match()
##
## # Client (connect before the server starts the countdown):
## scene.countdown_started.connect(func(n): $Timer.text = str(n))
## scene.countdown_tick.connect(func(n): $Timer.text = str(n))
## scene.countdown_finished.connect(start_match)
## [/codeblock]
class Countdown:
	extends RefCounted

	## Emitted each second with the remaining seconds (including 0 at the very end).
	signal tick(seconds_left: int)
	## Emitted when the countdown reaches zero.
	signal finished()
	## Emitted when [method cancel] is called before the countdown reaches zero.
	signal cancelled()

	var _scene_ref: WeakRef
	var _seconds_left: int
	var _tick_interval: float
	var _running: bool = false


	func _init(
			scene: MultiplayerScene,
			seconds: int,
			tick_interval: float = 1.0,
	) -> void:
		_scene_ref = weakref(scene)
		_seconds_left = seconds
		_tick_interval = tick_interval


	## Returns [code]true[/code] if the countdown is actively ticking.
	func is_running() -> bool:
		return _running

	## The number of seconds remaining.
	var seconds_left: int:
		get:
			return _seconds_left


	## Cancels the countdown and emits [signal cancelled].
	## Does nothing if the countdown is not running.
	func cancel() -> void:
		if not _running:
			return
		_running = false
		cancelled.emit()


	## Starts ticking. Called internally by [method NetwScene.start_countdown].
	func _start() -> void:
		_running = true
		_schedule_tick()


	func _schedule_tick() -> void:
		var scene := _scene_ref.get_ref() as MultiplayerScene
		if not is_instance_valid(scene) or not scene.is_inside_tree():
			_running = false
			return
		scene.get_tree().create_timer(_tick_interval).timeout.connect(
			_on_tick,
			CONNECT_ONE_SHOT,
		)


	func _on_tick() -> void:
		if not _running:
			return
		_seconds_left -= 1
		tick.emit(_seconds_left)
		if _seconds_left <= 0:
			_running = false
			finished.emit()
		else:
			_schedule_tick()


## Server-side handle for a participant scene move.
##
## Obtain via [method NetwScene.move_participants].
class MoveBatch:
	extends RefCounted

	## Emitted as each participant is admitted to the destination scene.
	signal participant_arrived(participant: NetwParticipant)
	## Emitted after every participant has been admitted.
	signal completed()

	var _pending: int = 0
	var _arrivals: Array[NetwParticipant] = []


	func _init(participants: Array[NetwParticipant]) -> void:
		_pending = participants.size()


	func _queue_arrival(participant: NetwParticipant) -> void:
		_pending = maxi(0, _pending - 1)
		_arrivals.append(participant)


	func _flush() -> void:
		for participant: NetwParticipant in _arrivals:
			participant_arrived.emit(participant)
		_arrivals.clear()
		completed.emit()


## Per-scene readiness gate tracks which participants have confirmed ready.
##
## Obtain via [method NetwScene.create_readiness_gate].
## Clients call [method set_ready]. The server broadcasts the change to all peers.
## [codeblock]
## # Game scene screen (runs on all peers):
## var gate := scene.create_readiness_gate()
## gate.participant_ready_changed.connect(_refresh_ready_ui)
## gate.all_ready.connect(_on_everyone_ready)
##
## # Player clicks "Ready":
## gate.set_ready(true)
## [/codeblock]
class Readiness:
	extends RefCounted

	## Emitted on all peers when a participant's readiness state changes.
	signal participant_ready_changed(participant: NetwParticipant, is_ready: bool)
	## Emitted when every tracked participant is ready.
	##
	## This also emits when a not-ready participant leaves, if the remaining
	## participants are all ready.
	signal all_ready()

	var _scene_ref: WeakRef
	# Peer ID -> ready state. Populated as participants enter or leave.
	var _readiness: Dictionary[int, bool] = { }


	func _init(scene: MultiplayerScene) -> void:
		_scene_ref = weakref(scene)


	## Returns [code]true[/code] while the underlying [Scene] is still alive.
	func is_valid() -> bool:
		return is_instance_valid(_scene_ref.get_ref())


	## Returns [code]true[/code] if [param participant] has confirmed ready.
	func is_ready(participant: NetwParticipant) -> bool:
		if participant == null:
			return false
		return _readiness.get(participant.peer_id, false)


	## Returns [code]true[/code] if [param participant] is tracked.
	func tracks(participant: NetwParticipant) -> bool:
		if participant == null:
			return false
		return _readiness.has(participant.peer_id)


	## Returns all participants that have confirmed ready.
	func get_ready_participants() -> Array[NetwParticipant]:
		var result: Array[NetwParticipant] = []
		var tree := _tree()
		if tree == null:
			return result
		for id: int in _readiness:
			if _readiness[id]:
				var participant := tree.participant(id)
				if participant:
					result.append(participant)
		return result


	## Returns [code]true[/code] when every tracked participant is ready and
	## there is at least one participant.
	func are_all_ready() -> bool:
		if _readiness.is_empty():
			return false
		for v: bool in _readiness.values():
			if not v:
				return false
		return true


	## Marks the local participant as ready or not ready.
	##
	## On a client this sends an RPC to the server. On the server/host it applies
	## the change directly. The update is broadcast to all peers automatically.
	func set_ready(ready: bool = true) -> void:
		var scene := _scene_ref.get_ref() as MultiplayerScene
		if not is_instance_valid(scene):
			return
		if scene.multiplayer.is_server():
			scene._handle_set_ready(scene.multiplayer.get_unique_id(), ready)
		else:
			scene._rpc_request_set_ready.rpc_id(1, ready)


	# Called internally by [Scene] when the server broadcasts a readiness update.
	func _receive_ready_changed(peer_id: int, is_ready: bool) -> void:
		_readiness[peer_id] = is_ready
		var tree := _tree()
		var participant := tree.participant(peer_id) if tree else null
		if participant:
			participant_ready_changed.emit(participant, is_ready)
		if are_all_ready():
			all_ready.emit()


	# Called internally when a participant enters the scene.
	func _add_peer(peer_id: int) -> void:
		if peer_id not in _readiness:
			_readiness[peer_id] = false


	# Called internally when a participant leaves the scene.
	func _remove_peer(peer_id: int) -> void:
		if _readiness.erase(peer_id) and are_all_ready():
			all_ready.emit()


	func _tree() -> NetwMultiplayer:
		var scene := _scene_ref.get_ref() as MultiplayerScene
		if not is_instance_valid(scene):
			return null
		return NetwMultiplayer.of(scene)
