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
var _loopback: NetwHarnessSession
var _waiter: NetwWaiter
var _runners: Array[NetwSceneRunner] = []
var _host: NetwSceneRunner
var _display_viewport: ParticipantViewport

## The listen server host participant.
var host: NetwSceneRunner:
	get:
		return _host

var _saved_time_scale := 1.0
var _saved_physics_ticks := 60
var _torn_down := false


func _init(scene: PackedScene = null) -> void:
	_main_scene = scene


## Creates the shared [LocalLoopbackSession].
func setup() -> void:
	assert(_main_scene != null, "NetwGameHarness.setup: scene is required.")
	_loopback = NetwHarnessSession.new()
	_saved_time_scale = Engine.time_scale
	_saved_physics_ticks = Engine.get_physics_ticks_per_second()

	if DisplayServer.get_name() == "headless":
		Engine.time_scale = 10.0
		Engine.set_physics_ticks_per_second(_saved_physics_ticks * 10)

	# Skip noisy resource tracking in test session hook.
	# Game harnesses trigger Godot's resource cache.
	NetwTestSessionHook.game_harness_used_in_test = true

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

	var err: Error = await _loopback.connect_tree(
		runner.tree,
		NetwHarnessSession.Entry.HOST_PLAYER,
		_make_join_payload(username, spawn),
	)
	assert(err == OK, "host_player() failed: %s" % error_string(err))

	_finish_online_runner(runner)
	await _wait_for_roster(runner)
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

	var err: Error = await _loopback.connect_tree(
		runner.tree,
		NetwHarnessSession.Entry.JOIN,
		_make_join_payload(username, spawn),
	)
	assert(err == OK, "join() failed: %s" % error_string(err))

	_finish_online_runner(runner)
	await _wait_for_roster(runner)
	if wait_for_player:
		await _wait_for_local_player(runner)
	return runner


## Disconnects [param runner] and waits for the server to drop its peer.
##
## Mirrors [method add_client]: the call settles before returning, so the host
## roster and any disconnect driven game logic have run by the time it resolves.
func disconnect_runner(runner: NetwSceneRunner) -> void:
	var peer_id := _loopback.disconnect_tree(runner.tree)
	if peer_id == 0 or not _host or runner == _host:
		return
	var timed_out := await _wait_until(
		func() -> bool:
			for rj: ResolvedJoin in _host.tree.get_joined_players():
				if rj.peer_id == peer_id:
					return false
			return true,
		"server to drop peer %d" % peer_id,
	)
	assert(not timed_out, "Timed out waiting for server to drop peer.")


## Advances the shared scene tree by [param n] network ticks.
func sync_ticks(n: int) -> void:
	assert(n >= 0, "NetwGameHarness.sync_ticks: n must be non-negative.")
	if n == 0:
		return

	var clock := _host.tree.get_service(MultiplayerClock) as MultiplayerClock \
	if _host else null
	if not clock:
		for i in n:
			await get_tree().process_frame
		return

	var stepper := MultiplayerClockStepper.new(
		get_tree(),
		clock,
		_saved_physics_ticks,
		DEFAULT_TICKRATE,
	)
	await stepper.sync_ticks(n)


## Waits for a transition animation ([TPLayerAPI]) to finish on a specific
## [param runner].
func wait_for_transition(runner: NetwSceneRunner) -> void:
	var tp_layer := runner.tree.get_service(TPLayerAPI) as TPLayerAPI
	while tp_layer and tp_layer.transition_anim.is_playing():
		await sync_ticks(1)


## Waits for transition animations ([TPLayerAPI]) to finish on a list of
## [param runners].
## [br]If the list is empty, it waits for transitions on all active runners
## registered in this harness.
func wait_for_transitions(runners: Array[NetwSceneRunner] = []) -> void:
	var list := runners if not runners.is_empty() else _runners
	var active_transitions := true
	while active_transitions:
		active_transitions = false
		for runner in list:
			var tp_layer := (
					runner.tree.get_service(TPLayerAPI) as TPLayerAPI
			)
			if tp_layer and tp_layer.transition_anim.is_playing():
				active_transitions = true
				break
		if active_transitions:
			await sync_ticks(1)



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


## Degrades both network directions for [param runner].
##
## [method NetwLink.NetwLinkMulti.inbound] narrows to server to player
## traffic. [method NetwLink.NetwLinkMulti.outbound] narrows to player to
## server traffic.
func degrade(runner: NetwSceneRunner) -> NetwLink.NetwLinkMulti:
	assert(_host != null, "NetwGameHarness.degrade: add host first.")
	assert(
		runner != _host,
		"NetwGameHarness.degrade: host has no remote player link.",
	)
	var inbound := path(_host, runner)
	var outbound := path(runner, _host)
	return NetwLink.NetwLinkMulti.new(inbound, outbound)


## Applies [param profile] to every runner except the host.
func degrade_clients(profile: NetwLink.Profile) -> void:
	for runner in _runners:
		if runner != _host:
			degrade(runner).profile(profile)


## Clears all link simulation in this harness session.
func clear_links() -> void:
	_loopback.session().clear_all_link_conditions()


## Returns fluent path control for packets from [param from_runner] to
## [param to_runner].
func path(
		from_runner: NetwSceneRunner,
		to_runner: NetwSceneRunner,
) -> NetwLink:
	var peer := _loopback_peer_for(to_runner, "path")
	return NetwLink.new(_loopback.session(), peer, from_runner.peer_id)


## Returns fluent inbound link control for [param runner]'s loopback peer.
##
## Prefer [method degrade] or [method path]. This method preserves the old
## receiver keyed API used by existing tests.
func link(
		runner: NetwSceneRunner,
		from_runner: NetwSceneRunner = null,
) -> NetwLink:
	var peer := _loopback_peer_for(runner, "link")
	var sender_id := from_runner.peer_id if from_runner else 0
	return NetwLink.new(_loopback.session(), peer, sender_id)


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

	if _loopback:
		_loopback.reset()
	_loopback = null
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
	_loopback.adopt_tree(tree, role)


func _finish_online_runner(runner: NetwSceneRunner) -> void:
	runner.peer_id = runner.tree.multiplayer_peer.get_unique_id()
	runner.slot.peer_id = runner.peer_id
	if is_instance_valid(_display_viewport):
		_display_viewport.add_slot(runner.slot)
		_show_display_window()


func _show_display_window() -> void:
	if _host:
		_host.move_window_to_foreground()


func _loopback_peer_for(
		runner: NetwSceneRunner,
		method_name: String,
) -> LocalMultiplayerPeer:
	assert(
		runner != null and runner.tree != null,
		"NetwGameHarness.%s: runner is not connected." % method_name,
	)
	var peer := runner.tree.multiplayer_peer as LocalMultiplayerPeer
	assert(
		peer != null,
		(
				"NetwGameHarness.%s: link simulation requires "
				+ "LocalLoopbackBackend."
		) % method_name,
	)
	return peer


func _make_join_payload(username: String, spawn: Variant = null) -> JoinPayload:
	return _loopback.build_join_payload(username, spawn)


func _resolve_spawn_dict(spawn: Variant, username: String) -> Dictionary:
	return _loopback.resolve_spawn_dict(spawn, username)


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


# Waits until the server roster admits runner's peer. The client side join
# future resolves before the host finishes registering the peer, so blocking
# on the roster keeps add_host and add_client fully settled on return.
func _wait_for_roster(runner: NetwSceneRunner) -> void:
	if not _host:
		return
	var timed_out := await _wait_until(
		func() -> bool:
			for rj: ResolvedJoin in _host.tree.get_joined_players():
				if rj.peer_id == runner.peer_id:
					return true
			return false,
		"server roster to admit %s" % runner.username,
	)
	assert(not timed_out, "Timed out waiting for server to admit peer.")


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
