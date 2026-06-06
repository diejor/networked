## Session coordinator for slot based game integration tests.
##
## [method add_host], [method add_client], and [method sync_ticks] drive real
## game scenes through one [LocalLoopbackSession]. Per peer input lives on the
## returned [NetwSceneRunner].
class_name NetwGameHarness
extends Node

const DEFAULT_TIMEOUT := 1.0
const DEFAULT_TICKRATE := 30

var awaiter: Callable = _default_awaiter

var _main_scene: PackedScene
var _session: LocalLoopbackSession
var _runners: Array[NetwSceneRunner] = []
var _host: NetwSceneRunner
var _saved_time_scale := 1.0
var _saved_physics_ticks := 60
var _torn_down := false

signal _wait_satisfied()


func _init(scene: PackedScene = null) -> void:
	_main_scene = scene


## Creates the shared [LocalLoopbackSession].
func setup() -> void:
	assert(_main_scene != null, "NetwGameHarness.setup: scene is required.")
	_session = LocalLoopbackSession.new()
	_saved_time_scale = Engine.time_scale
	_saved_physics_ticks = Engine.get_physics_ticks_per_second()
	await get_tree().process_frame


## Adds a listen server host participant.
func add_host(username: String = "host") -> NetwSceneRunner:
	assert(_host == null, "NetwGameHarness.add_host: host already exists.")
	var runner := _create_runner(
		username,
		MultiplayerTree.Role.LISTEN_SERVER,
	)
	_host = runner

	var err: Error = await runner.tree.host_player(
		_make_join_payload(runner.scene(), username),
	)
	assert(err == OK, "host_player() failed: %s" % error_string(err))

	_finish_online_runner(runner)
	await _wait_for_local_player(runner)
	return runner


## Adds a client participant connected to [method add_host].
func add_client(username: String) -> NetwSceneRunner:
	assert(_host != null, "NetwGameHarness.add_client: add host first.")
	var runner := _create_runner(username, MultiplayerTree.Role.CLIENT)

	var target := JoinTarget.new()
	target.backend = runner.tree.backend
	target.address = "localhost"
	var err: Error = await runner.tree.join(
		target,
		_make_join_payload(runner.scene(), username),
	)
	assert(err == OK, "join() failed: %s" % error_string(err))

	_finish_online_runner(runner)
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


## Sets global simulation speed for this harness session.
func set_time_factor(factor: float) -> void:
	assert(factor > 0.0, "NetwGameHarness.set_time_factor: factor > 0.")
	Engine.time_scale = factor
	Engine.set_physics_ticks_per_second(int(_saved_physics_ticks * factor))


## Frees all participant slots and resets global harness state.
func teardown() -> void:
	if _torn_down:
		return
	_torn_down = true

	Engine.time_scale = _saved_time_scale
	Engine.set_physics_ticks_per_second(_saved_physics_ticks)

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
	awaiter = Callable()

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
	_runners.append(runner)
	return runner


func _adopt_tree(tree: MultiplayerTree, role: MultiplayerTree.Role) -> void:
	tree.desired_role = role
	tree.auto_host_headless = false
	var backend := LocalLoopbackBackend.new()
	backend.session = _session
	tree.backend = backend


func _finish_online_runner(runner: NetwSceneRunner) -> void:
	runner.peer_id = runner.tree.multiplayer_peer.get_unique_id()
	runner.slot.peer_id = runner.peer_id


func _make_join_payload(scene: Node, username: String) -> JoinPayload:
	var payload := JoinPayload.new()
	payload.username = username

	var spawner := _find_default_spawner(scene)
	if spawner:
		payload.spawn = SpawnerComponentPolicy.from_scene_node_path(
			spawner,
		).to_dict()
	return payload


func _find_default_spawner(scene: Node) -> SceneNodePath:
	for node in _collect_nodes(scene):
		var value: Variant = node.get("spawner_options")
		if value is Array and not value.is_empty():
			return value[0] as SceneNodePath
	return null


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
	if cond.call():
		return false
	_poll_until(cond)
	var timed_out: bool = await awaiter.call(_wait_satisfied, timeout, label)
	return timed_out


func _poll_until(cond: Callable) -> void:
	while is_inside_tree():
		await get_tree().process_frame
		if cond.call():
			_wait_satisfied.emit()
			return


func _default_awaiter(sig: Signal, timeout: float, label: String) -> bool:
	var timer := get_tree().create_timer(timeout)
	var timed_out: bool = await Async.timeout(sig, timer)
	if timed_out:
		push_error("Timed out waiting for '%s' after %.2fs." % [label, timeout])
	return timed_out
