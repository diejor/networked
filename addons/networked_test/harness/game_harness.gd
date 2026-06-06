## Session coordinator for slot based game integration tests.
##
## [method add_host], [method add_client], and [method sync_ticks] drive real
## game scenes through one [LocalLoopbackSession]. Per peer input lives on the
## returned [NetwSceneRunner].
class_name NetwGameHarness
extends Node

const DEFAULT_TIMEOUT := 1.0
const DEFAULT_TICKRATE := 30

var reporter: Callable = _default_reporter

var _main_scene: PackedScene
var _session: LocalLoopbackSession
var _waiter: NetwWaiter
var _runners: Array[NetwSceneRunner] = []
var _host: NetwSceneRunner
var _display_viewport: ParticipantViewport
var _saved_time_scale := 1.0
var _saved_physics_ticks := 60
var _torn_down := false


func _init(scene: PackedScene = null) -> void:
	_main_scene = scene


## Creates the shared [LocalLoopbackSession].
func setup() -> void:
	assert(_main_scene != null, "NetwGameHarness.setup: scene is required.")
	_session = LocalLoopbackSession.new()
	_saved_time_scale = Engine.time_scale
	_saved_physics_ticks = Engine.get_physics_ticks_per_second()
	_waiter = NetwWaiter.new(get_tree(), reporter)
	await get_tree().process_frame


## Adds a listen server host participant.
##
## [param spawn] may be a [SceneNodePath], [JoinPayload], [Dictionary], or
## [code]null[/code]. When [param spawn] is a [JoinPayload], only
## [member JoinPayload.spawn] is used. [param username] stays authoritative.
func add_host(
		username: String = "host",
		wait_for_player: bool = true,
		spawn: Variant = null,
) -> NetwSceneRunner:
	assert(_host == null, "NetwGameHarness.add_host: host already exists.")
	var runner := _create_runner(
		username,
		MultiplayerTree.Role.LISTEN_SERVER,
	)
	_host = runner

	var err: Error = await runner.tree.host_player(
		_make_join_payload(username, spawn),
	)
	assert(err == OK, "host_player() failed: %s" % error_string(err))

	_finish_online_runner(runner)
	if wait_for_player:
		await _wait_for_local_player(runner)
	return runner


## Adds a client participant connected to [method add_host].
##
## [param spawn] may be a [SceneNodePath], [JoinPayload], [Dictionary], or
## [code]null[/code]. When [param spawn] is a [JoinPayload], only
## [member JoinPayload.spawn] is used. [param username] stays authoritative.
func add_client(
		username: String,
		wait_for_player: bool = true,
		spawn: Variant = null,
) -> NetwSceneRunner:
	assert(_host != null, "NetwGameHarness.add_client: add host first.")
	var runner := _create_runner(username, MultiplayerTree.Role.CLIENT)

	var target := JoinTarget.new()
	target.backend = runner.tree.backend
	target.address = "localhost"
	var err: Error = await runner.tree.join(
		target,
		_make_join_payload(username, spawn),
	)
	assert(err == OK, "join() failed: %s" % error_string(err))

	_finish_online_runner(runner)
	if wait_for_player:
		await _wait_for_local_player(runner)
	return runner


## Advances the shared scene tree by [param n] network ticks.
func sync_ticks(n: int) -> void:
	assert(n >= 0, "NetwGameHarness.sync_ticks: n must be non-negative.")
	if n == 0:
		return

	var clock := _host.tree.get_service(NetworkClock) as NetworkClock \
	if _host else null
	if not clock:
		for i in n:
			await get_tree().process_frame
		return

	var stepper := NetworkClockStepper.new(
		get_tree(),
		clock,
		_saved_physics_ticks,
		DEFAULT_TICKRATE,
	)
	await stepper.sync_ticks(n)


## Advances ordinary frames without asserting network tick progress.
##
## Use this for temporary visual pauses after [method show_views].
func watch_frames(n: int) -> void:
	assert(n >= 0, "NetwGameHarness.watch_frames: n must be non-negative.")
	for i in n:
		await get_tree().process_frame


## Sets global simulation speed for this harness session.
func set_time_factor(factor: float) -> void:
	assert(factor > 0.0, "NetwGameHarness.set_time_factor: factor > 0.")
	Engine.time_scale = factor
	Engine.set_physics_ticks_per_second(int(_saved_physics_ticks * factor))


## Displays every participant slot in one window.
##
## Tests remain headless unless this method is called.
func show_views() -> ParticipantViewport:
	if is_instance_valid(_display_viewport):
		_show_display_window()
		return _display_viewport

	_display_viewport = ParticipantViewport.new()
	_display_viewport.name = &"ParticipantViewport"
	add_child(_display_viewport)
	for runner in _runners:
		_display_viewport.add_slot(runner.slot)
	_show_display_window()
	return _display_viewport


## Frees all participant slots and resets global harness state.
func teardown() -> void:
	if _torn_down:
		return
	_torn_down = true

	Engine.time_scale = _saved_time_scale
	Engine.set_physics_ticks_per_second(_saved_physics_ticks)

	if is_instance_valid(_display_viewport):
		_display_viewport.queue_free()
		_display_viewport = null

	for runner in _runners.duplicate():
		if is_instance_valid(runner.slot):
			runner.slot.queue_free()

	if get_tree():
		await NetwTestSuite.drain_frames(get_tree(), 2)

	_runners.clear()
	_host = null

	if _session:
		_session.reset()
	_session = null
	_waiter = null
	reporter = Callable()

	if is_inside_tree():
		get_parent().remove_child(self)
	queue_free()


func _create_runner(
		username: String,
		role: MultiplayerTree.Role,
) -> NetwSceneRunner:
	var slot := ParticipantSlot.new()
	slot.name = "Slot_%s" % username
	add_child(slot)

	var scene := _main_scene.instantiate()
	var tree := _find_single_multiplayer_tree(scene)
	_adopt_tree(tree, role)

	var runner := NetwSceneRunner.new(scene, slot, StringName(username))
	runner.tree = tree
	runner.slot.tree = tree
	runner.slot.username = StringName(username)
	runner.username = StringName(username)
	runner.waiter = NetwWaiter.new(get_tree(), reporter)
	_runners.append(runner)
	return runner


func _adopt_tree(tree: MultiplayerTree, role: MultiplayerTree.Role) -> void:
	tree.desired_role = role
	tree.auto_host_headless = false
	tree.debug_join = null
	var backend := LocalLoopbackBackend.new()
	backend.session = _session
	tree.backend = backend


func _finish_online_runner(runner: NetwSceneRunner) -> void:
	runner.peer_id = runner.tree.multiplayer_peer.get_unique_id()
	runner.slot.peer_id = runner.peer_id
	if is_instance_valid(_display_viewport):
		_display_viewport.add_slot(runner.slot)
		_show_display_window()


func _show_display_window() -> void:
	if _host:
		_host.move_window_to_foreground()


func _make_join_payload(username: String, spawn: Variant = null) -> JoinPayload:
	var payload := JoinPayload.new()
	payload.username = username
	payload.spawn = _resolve_spawn_dict(spawn, username)
	return payload


func _resolve_spawn_dict(spawn: Variant, username: String) -> Dictionary:
	if spawn == null:
		return { }
	if spawn is JoinPayload:
		return spawn.spawn
	if spawn is Dictionary:
		return spawn
	if spawn is SceneNodePath:
		return SpawnerComponentPolicy.from_scene_node_path(spawn).to_dict()

	assert(
		false,
		(
			"NetwGameHarness: spawn for '%s' must be SceneNodePath, "
			+ "JoinPayload, Dictionary, or null."
		) % username,
	)
	return { }


func _find_single_multiplayer_tree(scene: Node) -> MultiplayerTree:
	var found: Array[MultiplayerTree] = []
	for node in _collect_nodes(scene):
		if node is MultiplayerTree:
			found.append(node)
	assert(
		found.size() == 1,
		"NetwGameHarness: expected exactly one MultiplayerTree. Found %d." %
		found.size(),
	)
	return found[0]


func _collect_nodes(root: Node) -> Array[Node]:
	var nodes: Array[Node] = [root]
	for child in root.get_children():
		nodes.append_array(_collect_nodes(child))
	return nodes


func _wait_for_local_player(runner: NetwSceneRunner) -> void:
	if is_instance_valid(runner.local_player):
		return
	var timed_out := await _wait_until(
		func() -> bool:
			return is_instance_valid(runner.local_player),
		"local player for %s" % runner.username,
	)
	assert(not timed_out, "Timed out waiting for local player.")


func _wait_until(
		cond: Callable,
		label: String,
		timeout: float = DEFAULT_TIMEOUT,
) -> bool:
	return await _waiter.until(cond, label, timeout)


func _default_reporter(label: String, timeout: float) -> void:
	push_error("Timed out waiting for '%s' after %.2fs." % [label, timeout])
