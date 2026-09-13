## Session coordinator for slot based game integration tests.
##
## [method add_host], [method add_client], and [method sync_ticks] drive real
## game scenes through one [LocalLoopbackSession]. Per peer input lives on the
## returned [NetwSceneRunner].
##
## A game scene may author its own [MultiplayerTree] or carry none. When the
## instantiated scene holds no tree, [method add_host] and [method add_client]
## wrap it in a harness-constructed [MultiplayerTree], one per participant
## window, so a scene the game natively swaps needs no authored session node.
class_name NetwGameHarness
extends Node

const DEFAULT_TIMEOUT := 1.0
const DEFAULT_TICKRATE := 30
## Wall-clock ceiling for one test's cumulative stepping, a runaway guard. The
## harness steps deterministically, so a budget is a hang catcher, not a pace
## limit. Override with the NETW_TEST_WALL_CLOCK_MS environment variable for
## slower CI.
const DEFAULT_WALL_CLOCK_BUDGET_MS := 60_000

var reporter: Callable = _default_reporter

var _main_scene: PackedScene
var _loopback: NetwHarnessSession
var _waiter: NetwWaiter
var _runners: Array[NetwSceneRunner] = []
var _display_viewport: ParticipantViewport

## The listen server host participant.
var host: NetwSceneRunner

var _torn_down := false
var _wall_deadline_ms := 0


func _init(scene: PackedScene = null) -> void:
	_main_scene = scene


## Creates the shared [LocalLoopbackSession].
func setup() -> void:
	assert(_main_scene != null, "NetwGameHarness.setup: scene is required.")
	_loopback = NetwHarnessSession.new()

	# Skip noisy resource tracking in test session hook.
	# Game harnesses trigger Godot's resource cache.
	NetwTestSessionHook.game_harness_used_in_test = true

	_waiter = NetwWaiter.new(get_tree(), reporter)
	await get_tree().process_frame


## Adds a listen server host participant.
##
## [param spawn] may be an [Array] of typed join args, or [code]null[/code]
## for a join that asks for nothing.
func add_host(
		username: String = "host",
		wait_for_player: bool = true,
		spawn: Variant = null,
) -> NetwSceneRunner:
	assert(host == null, "NetwGameHarness.add_host: host already exists.")
	var runner := _create_runner(
		username,
		NetwMultiplayer.ROLE_LISTEN_SERVER,
	)
	host = runner

	var err: Error = await _loopback.connect_tree(
		runner.tree,
		NetwHarnessSession.Entry.HOST,
		StringName(username),
		_loopback.build_join_args(spawn),
	)
	assert(err == OK, "host() failed: %s" % error_string(err))

	_finish_online_runner(runner)
	await _wait_for_roster(runner)
	if wait_for_player:
		await _wait_for_local_player(runner)
	return runner


## Adds a client participant connected to [method add_host].
##
## [param spawn] may be an [Array] of typed join args, or [code]null[/code]
## for a join that asks for nothing.
func add_client(
		username: String,
		wait_for_player: bool = true,
		spawn: Variant = null,
) -> NetwSceneRunner:
	assert(host != null, "NetwGameHarness.add_client: add host first.")
	var runner := _create_runner(username, NetwMultiplayer.ROLE_CLIENT)

	var err: Error = await _loopback.connect_tree(
		runner.tree,
		NetwHarnessSession.Entry.JOIN,
		StringName(username),
		_loopback.build_join_args(spawn),
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
	if peer_id == 0 or not host or runner == host:
		return
	var timed_out := await _wait_until(
		func() -> bool:
			for participant: NetwParticipant in host.tree.api.participants:
				if participant.peer_id == peer_id:
					return false
			return true,
		"server to drop peer %d" % peer_id,
	)
	assert(not timed_out, "Timed out waiting for server to drop peer.")


## Advances every participant by exactly [param n] network ticks through a
## [FrameLockstepStepper]. The tick count is exact with no dependence on
## wall-clock accumulation or [member Engine.time_scale].
func sync_ticks(n: int) -> void:
	assert(n >= 0, "NetwGameHarness.sync_ticks: n must be non-negative.")
	if n == 0:
		return
	_guard_wall_clock()

	var clocked: Array[NetwMultiplayer] = []
	for runner in _runners:
		if not runner or not runner.tree:
			continue
		var api := runner.tree.api
		if api and api.clock_is_configured():
			clocked.append(api)

	if clocked.is_empty():
		for i in n:
			await get_tree().process_frame
		return

	var stepper := FrameLockstepStepper.new(get_tree(), clocked)
	await stepper.sync_ticks(n)


## Game ticks spanning [param game_seconds] of game time at the host clock's
## [constant NetwMultiplayer.CLOCK_PARAM_TICKRATE].
##
## Stepping is deterministic, so a budget can only be expressed in ticks, never
## in real seconds. Sizing the budget from game seconds keeps a test's intent
## legible and portable across games whose clock runs a different tickrate.
## [codeblock]
## # Run roughly eight seconds of game time, tickrate-agnostic.
## await run_until(ais, game.seconds_to_ticks(8.0))
## [/codeblock]
func seconds_to_ticks(game_seconds: float) -> int:
	return maxi(1, ceili(game_seconds * float(_tickrate())))


func _tickrate() -> int:
	if host and host.tree:
		var api := host.tree.api
		if api and api.clock_is_configured():
			return int(api.clock_get_param(
				NetwMultiplayer.CLOCK_PARAM_TICKRATE
			))
	return DEFAULT_TICKRATE


# Fails fast when a test's cumulative stepping blows past the wall-clock ceiling
# so a runaway sim or a never-settling early-exit predicate surfaces as a legible
# failure instead of a silent hang. The deadline starts on the first step after
# setup, so connection and roster waits do not count against it.
func _guard_wall_clock() -> void:
	var now := Time.get_ticks_msec()
	if _wall_deadline_ms == 0:
		_wall_deadline_ms = now + _wall_clock_budget_ms()
		return
	assert(
		now < _wall_deadline_ms,
		(
				"NetwGameHarness: test exceeded %d ms of stepping. Likely a " \
						% _wall_clock_budget_ms()
		) + (
				"non-settling early-exit predicate or an oversized tick budget. " +
				"Bound the run with seconds_to_ticks() and an early-exit " +
				"predicate, or raise NETW_TEST_WALL_CLOCK_MS."
		),
	)


func _wall_clock_budget_ms() -> int:
	var raw := OS.get_environment("NETW_TEST_WALL_CLOCK_MS")
	return int(raw) if raw.is_valid_int() else DEFAULT_WALL_CLOCK_BUDGET_MS


## Every runner this harness has admitted, in the order they joined.
##
## A game's own presentation is the game's to wait on: a suite that must let
## an overlay or an animation settle iterates these and polls the node it
## installed, because the kit cannot name a type that lives in a game.
var runners: Array[NetwSceneRunner]:
	get:
		return _runners.duplicate()


## Advances ordinary frames without asserting network tick progress.
##
## Use this for temporary visual pauses after [method show_views].
func watch_frames(n: int) -> void:
	assert(n >= 0, "NetwGameHarness.watch_frames: n must be non-negative.")
	for i in n:
		await get_tree().process_frame


## Displays every participant window in one window.
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
	assert(host != null, "NetwGameHarness.degrade: add host first.")
	assert(
		runner != host,
		"NetwGameHarness.degrade: host has no remote player link.",
	)
	var inbound := path(host, runner)
	var outbound := path(runner, host)
	return NetwLink.NetwLinkMulti.new(inbound, outbound)


## Applies [param profile] to every runner except the host.
func degrade_clients(profile: NetwLink.Profile) -> void:
	for runner in _runners:
		if runner != host:
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


## Frees all participant windows and resets global harness state.
func teardown() -> void:
	if _torn_down:
		return
	_torn_down = true

	if is_instance_valid(_display_viewport):
		_display_viewport.queue_free()
		_display_viewport = null

	for runner in _runners.duplicate():
		if is_instance_valid(runner.slot):
			runner.slot.queue_free()

	if get_tree():
		await NetwTestSuite.drain_frames(get_tree(), 2)

	_runners.clear()
	host = null

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
		role: NetwMultiplayer.Role,
) -> NetwSceneRunner:
	var slot := ParticipantWindow.new()
	slot.name = "Window_%s" % username
	slot.own_world_3d = true
	slot.world_3d = World3D.new()
	slot.world_2d = World2D.new()
	add_child(slot)

	var scene := _main_scene.instantiate()
	var tree := _find_single_multiplayer_tree(scene)
	var runner_root: Node = scene
	if tree == null:
		tree = MultiplayerTree.new()
		tree.name = &"MultiplayerTree"
		tree.add_child(scene)
		runner_root = tree
	_adopt_tree(tree, role)

	var runner := NetwSceneRunner.new(runner_root, slot, StringName(username))
	runner.tree = tree
	runner.slot.mounted_tree = tree
	runner.slot.username = StringName(username)
	runner.username = StringName(username)
	runner.waiter = NetwWaiter.new(get_tree(), reporter)
	_runners.append(runner)
	return runner


func _adopt_tree(tree: MultiplayerTree, role: NetwMultiplayer.Role) -> void:
	_loopback.adopt_tree(tree, role)


func _finish_online_runner(runner: NetwSceneRunner) -> void:
	runner.peer_id = runner.tree.api.multiplayer_peer.get_unique_id()
	runner.slot.peer_id = runner.peer_id
	if is_instance_valid(_display_viewport):
		_display_viewport.add_slot(runner.slot)
		_show_display_window()


func _show_display_window() -> void:
	if host:
		host.move_window_to_foreground()


func _loopback_peer_for(
		runner: NetwSceneRunner,
		method_name: String,
) -> LocalMultiplayerPeer:
	assert(
		runner != null and runner.tree != null,
		"NetwGameHarness.%s: runner is not connected." % method_name,
	)
	var peer := runner.tree.api.multiplayer_peer as LocalMultiplayerPeer
	assert(
		peer != null,
		(
				"NetwGameHarness.%s: link simulation requires "
				+ "the local transport scheme."
		) % method_name,
	)
	return peer


func _find_single_multiplayer_tree(scene: Node) -> MultiplayerTree:
	var found: Array[MultiplayerTree] = []
	for node in _collect_nodes(scene):
		if node is MultiplayerTree:
			found.append(node)
	# Zero is the tree-less on-ramp: the caller wraps the scene in a
	# harness-constructed tree. More than one is always an authoring error.
	assert(
		found.size() <= 1,
		"NetwGameHarness: expected at most one MultiplayerTree. Found %d." %
		found.size(),
	)
	return found[0] if found.size() == 1 else null


func _collect_nodes(root: Node) -> Array[Node]:
	var nodes: Array[Node] = [root]
	for child in root.get_children():
		nodes.append_array(_collect_nodes(child))
	return nodes


# Waits until the server roster admits runner's peer. The client side join
# future resolves before the host finishes registering the peer, so blocking
# on the roster keeps add_host and add_client fully settled on return.
func _wait_for_roster(runner: NetwSceneRunner) -> void:
	if not host:
		return
	var timed_out := await _wait_until(
		func() -> bool:
			for participant: NetwParticipant in host.tree.api.participants:
				if participant.peer_id == runner.peer_id:
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
