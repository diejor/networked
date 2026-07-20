class_name TestDisplaySmoothnessProbe
extends NetwTestSuite

const MAIN := preload("res://examples/bomber/main.tscn")

var game: NetwGameHarness


func before_test() -> void:
	game = make_game_harness(MAIN)
	await game.setup()
	game.show_views()


# Reads a local player's visual and body every rendered frame, after the
# interpolation pump wrote (process_priority above the interface's 100).
class FrameProbe:
	extends Node

	var visual: Node2D
	var body: Node2D
	var clock: NetwClockInterface
	var iface: NetwInterpolationInterface
	var handle: RefCounted
	var label := ""
	var visual_xs := PackedFloat64Array()
	var body_xs := PackedFloat64Array()
	var display_times := PackedFloat64Array()
	var ticks := PackedInt32Array()
	var pump_modes := PackedInt32Array()
	var newest_ticks := PackedInt32Array()


	func _ready() -> void:
		process_priority = 1000


	func _process(_delta: float) -> void:
		if not is_instance_valid(visual) or not is_instance_valid(body):
			return
		visual_xs.append(visual.global_position.x)
		body_xs.append(body.global_position.x)
		display_times.append(float(clock.display_tick) + clock.tick_factor)
		ticks.append(clock.tick)
		var runtime: RefCounted = iface._runtime_for_handle(handle)
		pump_modes.append(runtime.pump_mode if runtime else -1)
		var newest := -1
		if runtime:
			for state in runtime.states:
				if state.name == &"position":
					newest = state.history.newest_tick()
		newest_ticks.append(newest)


	func report(warmup_frames: int) -> Dictionary:
		var regressions := 0
		var worst := 0.0
		var stalls := 0
		var max_step := 0.0
		var moved := 0.0
		var first := mini(warmup_frames, maxi(visual_xs.size() - 1, 0))
		for i in range(first + 1, visual_xs.size()):
			var d := visual_xs[i] - visual_xs[i - 1]
			if d < -0.01:
				regressions += 1
				worst = minf(worst, d)
			elif d < 0.01:
				stalls += 1
			max_step = maxf(max_step, d)
			moved += maxf(d, 0.0)
		var time_backwards := 0
		for i in range(first + 1, display_times.size()):
			if display_times[i] < display_times[i - 1] - 0.001:
				time_backwards += 1
		var body_moved := 0.0
		var body_max_step := 0.0
		for i in range(first + 1, body_xs.size()):
			var bd := body_xs[i] - body_xs[i - 1]
			body_moved += maxf(bd, 0.0)
			body_max_step = maxf(body_max_step, bd)
		var modes := { }
		var empty_history := 0
		var behind := 0
		for i in range(first, pump_modes.size()):
			modes[pump_modes[i]] = int(modes.get(pump_modes[i], 0)) + 1
			if newest_ticks[i] < 0:
				empty_history += 1
			elif float(newest_ticks[i]) < display_times[i]:
				behind += 1
		return {
			"label": label,
			"frames": visual_xs.size() - first,
			"regressions": regressions,
			"worst_regression": worst,
			"stalls": stalls,
			"max_step": max_step,
			"moved": moved,
			"body_moved": body_moved,
			"body_max_step": body_max_step,
			"time_backwards": time_backwards,
			"pump_modes": modes,
			"empty_history": empty_history,
			"newest_behind_display": behind,
		}


# Every participant's LOCAL player visual must advance monotonically while its
# move input is held. A visual that steps backwards is the on-screen jitter:
# the pump resampled an earlier time or snapped across mixed history.
func test_bracketed_local_visuals_move_monotonically() -> void:
	var valeria := await game.add_host("valeria", false)
	var jose := await game.add_client("jose", false)
	var ana := await game.add_client("ana", false)
	await _begin_game(valeria)

	var runners: Array = [valeria, jose, ana]
	var probes: Array = []
	for runner: NetwSceneRunner in runners:
		await runner.await_scene(&"World", 2.0)
		var player := await runner.await_player(runner.username, 2.0) as Node2D
		probes.append(_make_probe(str(runner.username), player))
	# The host simulates every player, so its view of the remote players is a
	# separate display path from both the local predicted and the client
	# remote paths. Probe them on the host's tree too.
	for remote: NetwSceneRunner in [jose, ana]:
		var seen := await valeria.await_player(remote.username, 2.0) as Node2D
		probes.append(_make_probe("host_view_of_%s" % remote.username, seen))

	await game.sync_ticks(10)
	for probe: FrameProbe in probes:
		var entity := NetwEntity.of(probe.body)
		var syncs := []
		for sync in entity.synchronizers():
			syncs.append(
				"%s(auth=%d local=%s pub=%s)" % [
					sync.name,
					sync.get_multiplayer_authority(),
					sync.is_multiplayer_authority(),
					sync.public_visibility,
				],
			)
		print(
			"SYNCS %s owner_auth=%d %s" % [
				probe.label,
				probe.body.get_multiplayer_authority(),
				", ".join(syncs),
			],
		)
		print(
			"DIAG %s liveness_connected=%s runtimes=%s route=%s wants=%s predicted_mode=%s" % [
				probe.label,
				probe.iface._liveness_connected,
				probe.iface._runtimes.keys(),
				probe.iface._route_of(entity),
				probe.iface._entity_wants_runtime(entity),
				probe.handle.predicted_mode,
			],
		)
	for runner: NetwSceneRunner in runners:
		runner.simulate_action_press("move_right")
	await game.sync_ticks(40)
	for runner: NetwSceneRunner in runners:
		runner.simulate_action_release("move_right")

	for probe: FrameProbe in probes:
		var r := probe.report(20)
		print("PROBE %s" % [r])
		assert_int(r["frames"]).override_failure_message(
			"probe '%s' captured no frames" % probe.label,
		).is_greater(10)
		# The host never receives prediction corrections, so its own player and
		# its authored view of every client must hold strict monotonicity. A
		# client's predicted local player may replay a server correction
		# through the display today, so its regressions are reported but not
		# gated until predicted correction smoothing lands. Client input
		# delivery can flake under load and leave a body parked, so movement
		# is only demanded where the body itself moved. The host's own player
		# always moves (its input never crosses the network), so the run can
		# never pass vacuously.
		if probe.label == "valeria" or probe.label.begins_with("host_view"):
			if probe.label == "valeria" or r["body_moved"] > 1.0:
				assert_float(r["moved"]).override_failure_message(
					"probe '%s' visual never moved" % probe.label,
				).is_greater(1.0)
			assert_int(r["regressions"]).override_failure_message(
				"probe '%s' visual moved backwards %d times (worst %.3f px): %s"
				% [probe.label, r["regressions"], r["worst_regression"], r],
			).is_equal(0)
		probe.queue_free()


func _make_probe(label: String, player: Node2D) -> FrameProbe:
	var probe := FrameProbe.new()
	probe.label = label
	probe.body = player
	probe.visual = player.get_node("sprite")
	probe.clock = NetwMultiplayer.of(player).clock
	probe.iface = NetwInterpolationInterface.for_node(player)
	probe.handle = NetwEntity.of(player).interpolation
	probe.handle.predicted_mode = (
			NetwInterpolationInterface.PredictedMode.BRACKETED
	)
	add_child(probe)
	return probe


func _begin_game(host: NetwSceneRunner) -> void:
	await host.await_scene(&"Lobby", 2.0)
	var gamestate := host.tree.get_service(BomberGamestate) as BomberGamestate
	assert_that(gamestate).is_not_null()
	gamestate.begin_game()
