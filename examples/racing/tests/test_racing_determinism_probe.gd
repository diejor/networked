extends NetwTestSuite
## Measures how far the predicted and authoritative car drift apart when every
## timing difference between the peers has been removed.
##
## The loopback harness runs both peers in one process on one clock, so tick and
## solver step align perfectly and each car receives the same input in the same
## order. Whatever divergence survives that is the physics itself failing to
## reproduce: same binary, same solver, same call sequence. Whatever divergence
## appears live but not here is timing, not physics.
##
## Divergence is read at matched ticks, off
## [signal NetwLagCompensationInterface.PredictionHandle.state_evaluated] and
## [member NetwLagCompensationInterface.PredictionHandle.last_field_divergence],
## never by differencing the two nodes where they stand. The predicted car runs
## ahead of the authoritative one by design, so a same-moment difference between
## the two bodies is that lead plus the divergence with no way to separate them,
## and the lead dominates. Only
## [member NetwLagCompensationInterface.PredictionHandle.last_compare_staleness]
## says whether a given sample was matched at all.
##
## This is a probe, not a gate. It prints a distribution and asserts only a very
## loose ceiling, so it records the number rather than freezing it.

const MAIN := preload("res://examples/racing/main.tscn")
const VEHICLE_CONTROLS := preload(
	"res://examples/racing/scripts/vehicle_controls.gd"
)
const SCHEDULE_FAULT_STEPPER := preload(
	"res://addons/networked_test/clock/schedule_fault_stepper.gd"
)

var game: NetwGameHarness


func before_test() -> void:
	game = make_game_harness(MAIN)
	await game.setup()


func after_test() -> void:
	VEHICLE_CONTROLS.probe_quantize_inputs = true
	var controls: Node = VEHICLE_CONTROLS.new()
	controls.free()
	if is_instance_valid(game):
		await game.teardown()
	await super.after_test()


func test_reports_same_process_divergence_at_matched_ticks() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await host.await_scene(&"Track", 2.0)
	await client.await_scene(&"Track", 2.0)

	var own := await client.await_player(&"luigi", 2.0)
	var mirror := await host.await_player(&"luigi", 2.0)
	await game.sync_ticks(8)

	var handle = own.entity.prediction
	var by_field: Dictionary[StringName, Array] = { }
	var staleness: Array[float] = []
	handle.state_evaluated.connect(
		func(_recv_tick: int, _ack: int, _divergence: float, _corrected: bool) -> void:
			staleness.append(float(handle.last_compare_staleness))
			for field: StringName in handle.last_field_divergence:
				if not by_field.has(field):
					by_field[field] = [] as Array[float]
				by_field[field].append(handle.last_field_divergence[field])
	)

	# A sustained turn is where the campaign's live captures put the worst
	# divergence, so the probe drives the same manoeuvre.
	client.simulate_action_press("forward")
	client.simulate_action_press("right")

	# The same-moment difference the earlier probe reported, kept only so the two
	# numbers print side by side: this one carries the prediction lead, the
	# matched-tick report below does not.
	var same_moment: Array[float] = []
	for i in range(60):
		await game.sync_ticks(2)
		same_moment.append(
			(own.sphere_angular_velocity - mirror.sphere_angular_velocity).length(),
		)

	client.simulate_action_release("forward")
	client.simulate_action_release("right")

	for field: StringName in by_field:
		_report("matched  %s" % field, by_field[field])
	_report("same-moment  sphere_angular_velocity", same_moment)
	_report("compare staleness (ticks)", staleness)

	var matched := staleness.filter(func(s: float) -> bool: return s == 0.0).size()
	print(
		"[probe] matched-tick samples=%d/%d  corrections=%d" % [
			matched,
			staleness.size(),
			handle.corrections,
		],
	)

	assert_int(staleness.size()) \
			.override_failure_message("the probe measured no state") \
			.is_greater(0)

	# Loose ceiling only. A one-process run that diverged as hard as the live
	# captures would mean the physics cannot reproduce itself at all, which is a
	# different and much larger problem than any timing fix addresses.
	var angular: Array[float] = by_field.get(&"sphere_angular_velocity", [] as Array[float])
	assert_float(_median(angular)) \
			.override_failure_message(
				"same-process matched-tick angular divergence median %.4f rad/s"
				% _median(angular),
			).is_less(5.0)


# Watches the predicted and authoritative bodies from the first tick they both
# exist, with no input ever applied. Position error in every capture so far is a
# standing offset rather than an accumulating drift, so this asks the question
# that shape poses: are the two bodies apart the moment they appear, or does
# something push them apart over the first few ticks. The per-axis split says
# which, since a settling difference is vertical and a placement difference is
# not. The other car's distance is recorded alongside because a predicted body
# that contacts a body it does not predict resolves that contact differently on
# each peer.
func test_reports_spawn_gap_from_first_tick() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await host.await_scene(&"Track", 2.0)
	await client.await_scene(&"Track", 2.0)

	var own := await client.await_player(&"luigi", 2.0)
	var mirror := await host.await_player(&"luigi", 2.0)
	var other_on_client := await client.await_player(&"mario", 2.0)
	var other_on_host := await host.await_player(&"mario", 2.0)

	print("[spawn] no input is applied at any point in this measurement")
	print("[spawn]   n   gap     dx      dy      dz     own.x   auth.x  d(luigi,mario) c/h  corr")
	for i in range(40):
		var delta: Vector3 = own.sphere_position - mirror.sphere_position
		print(
			"[spawn] %3d  %6.4f  %+6.3f  %+6.3f  %+6.3f  %7.3f  %7.3f   %6.3f %6.3f  %d" % [
				i,
				delta.length(),
				delta.x,
				delta.y,
				delta.z,
				own.sphere_position.x,
				mirror.sphere_position.x,
				own.sphere_position.distance_to(other_on_client.sphere_position),
				mirror.sphere_position.distance_to(other_on_host.sphere_position),
				own.entity.prediction.corrections,
			],
		)
		await game.sync_ticks(1)

	assert_bool(is_instance_valid(own)).is_true()


# Diffs the two peers' drive terms every tick. Propulsion derives its rolling
# axis from replicated heading projected onto each peer's local ground plane.
# Full model rotation remains in the output to prove pitch and roll no longer
# feed the impulse. A sustained turn exercises the drive.
func test_reports_drive_term_divergence_per_tick() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await host.await_scene(&"Track", 2.0)
	await client.await_scene(&"Track", 2.0)

	var own := await client.await_player(&"luigi", 2.0)
	var mirror := await host.await_player(&"luigi", 2.0)

	const TICK_DELTA := 1.0 / 60.0
	client.simulate_action_press("forward")
	client.simulate_action_press("right")
	print("[drive] sustained turn. o=own (predicted)  a=authoritative")
	print(
		"[drive]   n   spd_o  |dimp|   dnorm    dyaw  dpitch   droll   dangv  |"
		+ "   |v_o|   |v_a|    |dv|      gap",
	)
	for i in range(30):
		var rot_o: Vector3 = own.vehicle_model.rotation
		var rot_a: Vector3 = mirror.vehicle_model.rotation
		var basis_o := _propulsion_axis(own)
		var basis_a := _propulsion_axis(mirror)
		var imp_o: Vector3 = basis_o * (own.linear_speed * 100.0) * TICK_DELTA
		var imp_a: Vector3 = basis_a * (mirror.linear_speed * 100.0) * TICK_DELTA
		var norm_o: Vector3 = own.normal
		var norm_a: Vector3 = mirror.normal
		var vel_o: Vector3 = own.sphere_linear_velocity
		var vel_a: Vector3 = mirror.sphere_linear_velocity
		print(
			"[drive] %3d %7.4f %7.4f %7.4f %7.4f %7.4f %7.4f %7.4f  |  %7.3f %7.3f %7.4f  %7.4f" % [
				i,
				own.linear_speed,
				(imp_o - imp_a).length(),
				(norm_o - norm_a).length(),
				rot_o.y - rot_a.y,
				rot_o.x - rot_a.x,
				rot_o.z - rot_a.z,
				own.sphere_angular_velocity.distance_to(mirror.sphere_angular_velocity),
				vel_o.length(),
				vel_a.length(),
				(vel_o - vel_a).length(),
				own.sphere_position.distance_to(mirror.sphere_position),
			],
		)
		await game.sync_ticks(1)
	client.simulate_action_release("forward")
	client.simulate_action_release("right")

	assert_bool(is_instance_valid(own)).is_true()


# Checks whether the rig itself manufactures the divergence it measures. Both
# peers run in one process, so if their two copies of the same car share a
# physics space they are two solid bodies, and a correction that lands the
# predicted body on the authoritative pose puts them on top of each other. Two
# overlapping spheres repel symmetrically, which would produce divergence no
# amount of netcode could explain and which does not exist across two real
# processes.
func test_reports_whether_peers_share_a_physics_space() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await host.await_scene(&"Track", 2.0)
	await client.await_scene(&"Track", 2.0)

	var own := await client.await_player(&"luigi", 2.0)
	var mirror := await host.await_player(&"luigi", 2.0)
	await game.sync_ticks(4)

	var own_world: World3D = own.sphere.get_world_3d()
	var mirror_world: World3D = mirror.sphere.get_world_3d()
	print("[space] own world  =%s  space=%s" % [own_world, own_world.space])
	print("[space] auth world =%s  space=%s" % [mirror_world, mirror_world.space])
	print("[space] same World3D object = %s" % [own_world == mirror_world])
	print("[space] same physics space  = %s" % [own_world.space == mirror_world.space])
	print(
		"[space] global separation = %.4f  (two radius-0.5 spheres overlap below 1.0)"
		% own.sphere.global_position.distance_to(mirror.sphere.global_position),
	)
	print(
		"[space] own tree=%s  auth tree=%s" % [
			own.get_tree(),
			mirror.get_tree(),
		],
	)

	assert_bool(own_world.space == mirror_world.space) \
			.override_failure_message(
				"the two peers' bodies share a physics space, so each peer's copy of a"
				+ " car collides with the other peer's copy and the rig manufactures"
				+ " divergence that no real two-process session can produce",
			).is_false()


# Attributes every correction to the field that triggered it. The same-process
# rig should be the best case there is, so a correction rate near one-in-two
# says something is triggering that timing alignment cannot explain. This does
# not interpret the number, it only says which field crossed its threshold and
# how often, next to the threshold it crossed.
func test_reports_correction_trigger_attribution() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await host.await_scene(&"Track", 2.0)
	await client.await_scene(&"Track", 2.0)

	var own := await client.await_player(&"luigi", 2.0)
	await host.await_player(&"luigi", 2.0)
	await game.sync_ticks(8)

	var handle = own.entity.prediction
	# Counters live in reference types: a lambda captures a local by value, so an
	# int incremented inside the handler would only ever move a private copy.
	var corrected_flags: Array[bool] = []
	var exceeded: Dictionary[StringName, int] = { }
	var sole_trigger: Dictionary[StringName, int] = { }
	var binding: NetwSyncSetBinding = own.entity.state_binding
	handle.state_evaluated.connect(
		func(_recv_tick: int, _ack: int, _divergence: float, corrected: bool) -> void:
			corrected_flags.append(corrected)
			var over: Array[StringName] = []
			for field: StringName in handle.last_field_divergence:
				if binding.reconcile_only_of(field):
					continue
				var limit: float = binding.epsilon_override_of(field)
				if limit < 0.0:
					limit = handle.divergence_epsilon
				if handle.last_field_divergence[field] > limit:
					over.append(field)
					exceeded[field] = exceeded.get(field, 0) + 1
			if over.size() == 1:
				sole_trigger[over[0]] = sole_trigger.get(over[0], 0) + 1
	)

	client.simulate_action_press("forward")
	client.simulate_action_press("right")
	for i in range(60):
		await game.sync_ticks(2)
	client.simulate_action_release("forward")
	client.simulate_action_release("right")

	var evaluations := corrected_flags.size()
	var corrected_count := corrected_flags.filter(func(c: bool) -> bool: return c).size()
	print("[trigger] base epsilon=%.4f" % handle.divergence_epsilon)
	print(
		"[trigger] evaluations=%d  corrected=%d  corrections_total=%d" % [
			evaluations,
			corrected_count,
			handle.corrections,
		],
	)
	for field: StringName in handle.last_field_divergence:
		var limit: float = binding.epsilon_override_of(field)
		if limit < 0.0:
			limit = handle.divergence_epsilon
		print(
			"[trigger]   %-26s over threshold %3d/%d  (limit %.4f)  sole trigger %d" % [
				field,
				exceeded.get(field, 0),
				evaluations,
				limit,
				sole_trigger.get(field, 0),
			],
		)

	assert_int(evaluations) \
			.override_failure_message("the probe measured no state") \
			.is_greater(0)


# Follows position divergence across each correction to see whether a correction
# converges it or merely pushes it back below the trigger for a tick or two. A
# correction that is immediately re-earned is a policy oscillating around a
# threshold rather than a mechanism repairing an error.
func test_reports_position_recovery_across_corrections() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await host.await_scene(&"Track", 2.0)
	await client.await_scene(&"Track", 2.0)

	var own := await client.await_player(&"luigi", 2.0)
	await host.await_player(&"luigi", 2.0)
	await game.sync_ticks(8)

	var handle = own.entity.prediction
	var limit: float = own.entity.state_binding.epsilon_override_of(
		&"sphere_position",
	)
	if limit < 0.0:
		limit = handle.divergence_epsilon
	# Reference types only: a lambda captures locals by value.
	var divergence: Array[float] = []
	var corrected_flags: Array[bool] = []
	handle.state_evaluated.connect(
		func(_recv_tick: int, _ack: int, _divergence: float, corrected: bool) -> void:
			divergence.append(handle.last_field_divergence.get(&"sphere_position", 0.0))
			corrected_flags.append(corrected)
	)

	client.simulate_action_press("forward")
	client.simulate_action_press("right")
	for i in range(60):
		await game.sync_ticks(2)
	client.simulate_action_release("forward")
	client.simulate_action_release("right")

	print("[recovery] sphere_position trigger threshold %.4f" % limit)

	# The mean error at each offset after a correction. A converging policy walks
	# this down and keeps it down; a chattering one returns to the threshold.
	const HORIZON := 6
	for offset in range(HORIZON + 1):
		var samples: Array[float] = []
		for i in range(corrected_flags.size()):
			if corrected_flags[i] and i + offset < divergence.size():
				samples.append(divergence[i + offset])
		if not samples.is_empty():
			var over := samples.filter(func(d: float) -> bool: return d > limit).size()
			print(
				"[recovery]   +%d evals after a correction: mean=%7.4f  over threshold %d/%d" % [
					offset,
					_mean(samples),
					over,
					samples.size(),
				],
			)

	# How many evaluations a correction actually buys before the error is back
	# over the trigger.
	var recross: Array[float] = []
	for i in range(corrected_flags.size()):
		if not corrected_flags[i]:
			continue
		for d in range(1, divergence.size() - i):
			if divergence[i + d] > limit:
				recross.append(float(d))
				break
	if not recross.is_empty():
		recross.sort()
		print(
			"[recovery] evals until back over threshold: median=%.1f  min=%.1f  max=%.1f  n=%d" % [
				recross[recross.size() / 2],
				recross[0],
				recross[-1],
				recross.size(),
			],
		)

	assert_int(divergence.size()) \
			.override_failure_message("the probe measured no state") \
			.is_greater(0)


# The measurement that isolates the solver. Corrections are switched off by
# raising every divergence threshold out of reach, so nothing writes onto the
# predicted body and the two cars simulate the same inputs freely from the same
# start. The drift that accumulates is the physics failing to reproduce itself,
# with no correction machinery mixed in.
func test_reports_uncorrected_simulation_drift() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await host.await_scene(&"Track", 2.0)
	await client.await_scene(&"Track", 2.0)

	var own := await client.await_player(&"luigi", 2.0)
	var mirror := await host.await_player(&"luigi", 2.0)

	var handle = own.entity.prediction
	_silence_triggers(handle)
	await game.sync_ticks(8)
	# The wire map rebuilds on any rewire during warmup, so the silence is
	# re-applied once the link settles.
	_silence_triggers(handle)

	# The spawn-state audit. Two bodies that have received no input yet should be
	# standing in the same place, so any gap here is a start-state difference that
	# every later number inherits. The field list comes off the first evaluation
	# rather than being written out, so it covers the whole replicated set.
	print("[drift] spawn state, before any input:")
	for field: StringName in handle.last_field_divergence:
		var own_value: Variant = own.get(field)
		var mirror_value: Variant = mirror.get(field)
		print(
			"[drift]   %-26s own=%s  authority=%s  gap=%s" % [
				field,
				own_value,
				mirror_value,
				_gap(own_value, mirror_value),
			],
		)

	client.simulate_action_press("forward")
	client.simulate_action_press("right")
	for step in range(6):
		await game.sync_ticks(20)
		print(
			"[drift] t=%5.2fs  angular=%7.4f  linear=%7.4f  position=%7.4f  corrections=%d" % [
				(step + 1) * 20.0 / 60.0,
				(own.sphere_angular_velocity - mirror.sphere_angular_velocity).length(),
				(own.sphere_linear_velocity - mirror.sphere_linear_velocity).length(),
				own.sphere_position.distance_to(mirror.sphere_position),
				handle.corrections,
			],
		)
	client.simulate_action_release("forward")
	client.simulate_action_release("right")

	assert_int(handle.corrections) \
			.override_failure_message("the probe must not correct") \
			.is_equal(0)


# Injects paired zero and double client tick frames while corrections are off,
# then prints matched-tick divergence and consume counters around each fault.
func test_reports_divergence_step_response_at_schedule_faults() -> void:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await host.await_scene(&"Track", 2.0)
	await client.await_scene(&"Track", 2.0)

	var own := await client.await_player(&"luigi", 2.0)
	var mirror := await host.await_player(&"luigi", 2.0)
	var handle = own.entity.prediction
	var server_handle = mirror.entity.prediction
	_silence_triggers(handle)
	await game.sync_ticks(8)
	# The wire map rebuilds on any rewire during warmup, so the silence is
	# re-applied once the link settles.
	_silence_triggers(handle)

	var clocks: Array[NetwClockInterface] = [
		host.tree.api.clock,
		client.tree.api.clock,
	]
	var initial_tick_delta := clocks[1].tick - clocks[0].tick
	var stepper := SCHEDULE_FAULT_STEPPER.new(get_tree(), clocks)
	stepper.override_step_count(24, 1, 0)
	stepper.override_step_count(25, 1, 2)
	stepper.override_step_count(48, 1, 2)
	stepper.override_step_count(49, 1, 0)

	var evaluations: Array[int] = []
	handle.state_evaluated.connect(
		func(
				_recv_tick: int,
				_ack: int,
				_divergence: float,
				_corrected: bool,
		) -> void:
			evaluations.append(stepper.frame_index)
	)

	client.simulate_action_press("forward")
	client.simulate_action_press("right")
	print(
		"[fault] frame mark  ctick stick stale  angular position held consumed",
	)
	for frame_index in 72:
		await stepper.sync_frames(1)
		var frame := stepper.frame_index
		if not (frame in range(18, 32) or frame in range(42, 56)):
			continue
		var mark := "zero" if frame in [24, 49] else (
				"double" if frame in [25, 48] else ""
		)
		print(
			"[fault] %5d %-6s %5d %5d %5d %8.4f %8.4f %4d %8d" % [
				frame,
				mark,
				clocks[1].tick,
				clocks[0].tick,
				handle.last_compare_staleness,
				handle.last_field_divergence.get(
					&"sphere_angular_velocity",
					0.0,
				),
				handle.last_field_divergence.get(&"sphere_position", 0.0),
				server_handle.held_count,
				server_handle.consumed_count,
			],
		)
	client.simulate_action_release("forward")
	client.simulate_action_release("right")

	assert_int(evaluations.size()) \
			.override_failure_message("the probe measured no state") \
			.is_greater(0)
	assert_int(clocks[1].tick - clocks[0].tick).is_equal(initial_tick_delta)


# One fault pair is a step response. A rendered client emits them continuously:
# a session measured on this machine ran 458 zero-tick and 12 double-tick frames
# against a server that emitted exactly one tick on every frame, because the
# client renders faster than the tick rate and its clock redistributes the same
# ticks across more frames. This runs the same drive twice, once evenly and once
# at that ratio, so the cost of the redistribution is read against its own
# control rather than against a live capture with 25% run-to-run variance.
#
# Faults are paired so both clocks finish on the same tick count: the difference
# between the arms is when the ticks land, never how many.
const SCALED_FAULT_PERIOD := 8
const SCALED_FAULT_FRAMES := 200


# The control. One harness carries one session, so the two arms are two cases
# and the log is read as a pair.
func test_reports_divergence_with_evenly_stepped_clocks() -> void:
	var even := await _run_fault_ratio(0)
	assert_int(int(even["samples"])).override_failure_message(
		"the control arm measured no state",
	).is_greater(0)
	# The one assertion either arm earns: a control that already diverged would
	# make the uneven arm's number unreadable.
	assert_float(even["angular"]).override_failure_message(
		"evenly stepped clocks must reproduce the drive exactly",
	).is_less(0.01)


func test_reports_divergence_under_a_rendered_clients_fault_ratio() -> void:
	var uneven := await _run_fault_ratio(SCALED_FAULT_PERIOD)
	assert_int(int(uneven["samples"])).override_failure_message(
		"the uneven arm measured no state",
	).is_greater(0)


# The dense arm above is far harsher than a real client. A measured session ran
# 12 double-tick frames in 3099, so this arm carries the sparse ratio and says
# whether a handful of skipped transitions is enough on its own.
func test_reports_divergence_under_a_sparse_fault_ratio() -> void:
	var sparse := await _run_fault_ratio(SCALED_FAULT_FRAMES / 3)
	assert_int(int(sparse["samples"])).override_failure_message(
		"the sparse arm measured no state",
	).is_greater(0)


# Drives one arm and returns its worst matched divergence. A [param period] of
# zero steps both clocks evenly; otherwise the client drops a tick every
# [param period] frames and takes it back on the next frame.
func _run_fault_ratio(period: int) -> Dictionary:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await host.await_scene(&"Track", 2.0)
	await client.await_scene(&"Track", 2.0)

	var own := await client.await_player(&"luigi", 2.0)
	var handle = own.entity.prediction
	_silence_triggers(handle)
	await game.sync_ticks(8)
	# The wire map rebuilds on any rewire during warmup, so the silence is
	# re-applied once the link settles.
	_silence_triggers(handle)

	var clocks: Array[NetwClockInterface] = [
		host.tree.api.clock,
		client.tree.api.clock,
	]
	var initial_tick_delta := clocks[1].tick - clocks[0].tick
	var stepper := SCHEDULE_FAULT_STEPPER.new(get_tree(), clocks)
	var faults := 0
	if period > 0:
		var frame := period
		while frame + 1 <= SCALED_FAULT_FRAMES:
			stepper.override_step_count(frame, 1, 0)
			stepper.override_step_count(frame + 1, 1, 2)
			faults += 1
			frame += period

	var samples := 0
	var worst_angular := 0.0
	var worst_position := 0.0
	client.simulate_action_press("forward")
	client.simulate_action_press("right")
	for _i in SCALED_FAULT_FRAMES:
		await stepper.sync_frames(1)
		if handle.last_compare_staleness != 0:
			continue
		samples += 1
		worst_angular = maxf(
			worst_angular,
			handle.last_field_divergence.get(&"sphere_angular_velocity", 0.0),
		)
		worst_position = maxf(
			worst_position,
			handle.last_field_divergence.get(&"sphere_position", 0.0),
		)
	client.simulate_action_release("forward")
	client.simulate_action_release("right")

	print(
		"[ratio] %-8s zero-tick frames=%-4d samples=%-4d angular=%8.4f position=%8.4f"
				% [
					"even" if period == 0 else "uneven",
					faults,
					samples,
					worst_angular,
					worst_position,
				],
	)
	assert_int(clocks[1].tick - clocks[0].tick).override_failure_message(
		"paired faults must leave both clocks on the same tick count",
	).is_equal(initial_tick_delta)
	return {
		"faults": faults,
		"samples": samples,
		"angular": worst_angular,
		"position": worst_position,
	}


# The arms above vary when a tick lands. These vary how much physics a tick buys,
# which is a different quantity and the one a rendered session actually differs
# on: a measured client ran 3099 physics frames against 2653 ticks while its
# frame-starved host ran 2886 against 2886, so the client advanced its solver
# 1.168 times per tick against the host's 1.000. The car's body is a RigidBody3D,
# so it integrates on the physics frame while its state is compared on the
# transition. Identical impulses at identical transitions then close differently.
#
# Every arm gives both clocks the same zero-tick frames, so the tick counts
# finish equal and authority is never starved of input labels. What varies is
# whether a peer's physics space integrates on a frame that emitted no tick.
# [param gate] names the invariant under test: a frame that advances no
# simulated time advances no physics.
const INTEGRATION_SKIP_PERIOD := 7
const INTEGRATION_FRAMES := 210


# The control. No frame is skipped, so both peers integrate once per tick and
# the drive must reproduce exactly. An arm that diverged here would make the
# other two unreadable.
func test_reports_divergence_with_matched_integration_counts() -> void:
	var matched := await _run_integration_ratio(0, false)
	assert_int(int(matched["samples"])).override_failure_message(
		"the control arm measured no state",
	).is_greater(0)
	# A very loose ceiling. This harness is timing-marginal and the control draws
	# a non-zero number in a minority of runs, so freezing it tight would report
	# the rig rather than the drive.
	assert_float(matched["worst_angular"]).override_failure_message(
		(
				"matched integration counts must reproduce the drive, but angular"
				+ " divergence reached %.4f"
		) % [matched["worst_angular"]],
	).is_less(1.0)


# Both arms in one case, because the absolute numbers this harness draws move
# between runs while the ratio between two arms measured back to back does not.
#
# Ungated is the defect a rendered client produces: one frame in seven emits no
# tick on either peer, authority declines to integrate it, and the owner
# integrates it anyway, spending 1.167 integrations per tick against authority's
# 1.000. Gated holds the invariant on both peers instead, so each spends exactly
# one. Nothing else differs between them.
func test_reports_whether_gating_integration_on_the_tick_closes_the_gap() -> void:
	var ungated := await _run_integration_ratio(INTEGRATION_SKIP_PERIOD, false)
	await game.teardown()
	game = make_unmanaged_game_harness(MAIN)
	await game.setup()
	var gated := await _run_integration_ratio(INTEGRATION_SKIP_PERIOD, true)

	print(
		"[integrate] ungated worst=%.4f  gated worst=%.4f  closed %.0f%% of the gap"
				% [
					ungated["worst_angular"],
					gated["worst_angular"],
					100.0 * (1.0 - gated["worst_angular"] / maxf(
						ungated["worst_angular"],
						0.0001,
					)),
				],
	)

	assert_int(int(gated["samples"])).override_failure_message(
		"the gated arm measured no state",
	).is_greater(0)

	# The lever, before its result. A space that stayed inactive and integrated
	# anyway would leave every other number here describing a run that never
	# happened.
	assert_float(ungated["authority_travel"]).override_failure_message(
		(
				"deactivating the space did not stop the body integrating, so"
				+ " neither arm measured anything: authority travelled %.4f m"
				+ " against the owner's %.4f m"
		) % [ungated["authority_travel"], ungated["owner_travel"]],
	).is_less(ungated["owner_travel"])

	# The defect. An integration surplus on its own, with both clocks stepped
	# identically and every input matched, carries the car past the epsilon that
	# opens a divergence episode.
	var epsilon: float = ungated["angular_epsilon"]
	assert_float(ungated["worst_angular"]).override_failure_message(
		(
				"an unticked frame that still integrates must diverge past the"
				+ " %.4f epsilon that triggers a correction, but reached only %.4f"
		) % [epsilon, ungated["worst_angular"]],
	).is_greater(epsilon)

	# The repair, stated as a ratio rather than a ceiling. Gating does not
	# restore the control, so what this holds is that most of the divergence the
	# surplus bought is bought back.
	assert_float(gated["worst_angular"]).override_failure_message(
		(
				"gating integration on the tick must close most of the gap:"
				+ " ungated reached %.4f and gated still reached %.4f"
		) % [ungated["worst_angular"], gated["worst_angular"]],
	).is_less(0.5 * float(ungated["worst_angular"]))


# Whether the residual gating leaves is bounded or accumulating, which is the
# difference between an exact contract for a solver body and a declared-degraded
# one. The arm above is 3.5 s of fairly straight driving, short enough that a
# slow accumulation would read as a flat number. This one is three times longer
# and is read by fifths: a bounded residual holds its level, an accumulating one
# climbs, and the epsilon that opens an episode is the line either way.
const INTEGRATION_LONG_FRAMES := 630


func test_reports_whether_the_gated_residual_stays_bounded_over_a_long_drive() -> void:
	var gated := await _run_integration_ratio(
		INTEGRATION_SKIP_PERIOD,
		true,
		INTEGRATION_LONG_FRAMES,
	)

	assert_int(int(gated["samples"])).override_failure_message(
		"the long arm measured no state",
	).is_greater(0)
	# The contract claim, stated against the threshold it exists to stay under.
	# A residual that crossed here would mean gating bounds the divergence
	# without removing it, and the solver-body contract is degraded rather than
	# exact.
	assert_float(gated["last_fifth"]).override_failure_message(
		(
				"the gated residual must still be under the %.4f epsilon after "
				+ "%d frames, but the last fifth averaged %.4f against the "
				+ "first fifth's %.4f"
		) % [
			gated["angular_epsilon"],
			INTEGRATION_LONG_FRAMES,
			gated["last_fifth"],
			gated["first_fifth"],
		],
	).is_less(float(gated["angular_epsilon"]))


# Drives one arm and returns its divergence trajectory. A [param skip_period] of
# zero emits a tick on every frame. Otherwise both clocks skip one frame in
# [param skip_period], authority always declines to integrate a skipped frame,
# and the owner declines only when [param gate_owner] holds.
func _run_integration_ratio(
		skip_period: int,
		gate_owner: bool,
		frames: int = INTEGRATION_FRAMES,
) -> Dictionary:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await host.await_scene(&"Track", 2.0)
	await client.await_scene(&"Track", 2.0)

	var own := await client.await_player(&"luigi", 2.0)
	var mirror := await host.await_player(&"luigi", 2.0)
	var other_on_client := await client.await_player(&"mario", 2.0)
	var other_on_host := await host.await_player(&"mario", 2.0)
	var handle = own.entity.prediction
	# Read before the silence, which clears the per-field marks the engine
	# gathered at wire time.
	var angular_epsilon: float = own.entity.state_binding.epsilon_override_of(
		&"sphere_angular_velocity",
	)
	if angular_epsilon < 0.0:
		angular_epsilon = handle.divergence_epsilon
	_silence_triggers(handle)
	await game.sync_ticks(8)
	# The wire map rebuilds on any rewire during warmup, so the silence is
	# re-applied once the link settles.
	_silence_triggers(handle)

	var clocks: Array[NetwClockInterface] = [
		host.tree.api.clock,
		client.tree.api.clock,
	]
	var initial_tick_delta := clocks[1].tick - clocks[0].tick
	var stepper := SCHEDULE_FAULT_STEPPER.new(get_tree(), clocks)
	var authority_space: RID = mirror.sphere.get_world_3d().space
	var owner_space: RID = own.sphere.get_world_3d().space

	# Both clocks lose the same frames, so the run ends on matched tick counts
	# and authority never runs short of input labels.
	var skipped_ticks := 0
	if skip_period > 0:
		var skipped_frame := skip_period
		while skipped_frame <= frames:
			stepper.override_step_count(skipped_frame, 0, 0)
			stepper.override_step_count(skipped_frame, 1, 0)
			skipped_ticks += 1
			skipped_frame += skip_period

	# Distance each body covers, so the arm can say whether the space it stopped
	# actually stopped.
	var owner_travel := 0.0
	var authority_travel := 0.0
	var last_own: Vector3 = own.sphere_position
	var last_mirror: Vector3 = mirror.sphere_position

	# Whether stopping a space also stops the ray the drive reads its ground from,
	# and how close the cars come. A lever that blinds the raycast, or a pair that
	# meets in the broadphase, would both put something other than the
	# integration count in the result.
	var ray_hits := { "own_tick": 0, "own_skip": 0, "auth_tick": 0, "auth_skip": 0 }
	var ray_frames := { "own_tick": 0, "own_skip": 0, "auth_tick": 0, "auth_skip": 0 }
	var nearest_on_client := INF
	var nearest_on_host := INF

	var trajectory: Array[float] = []
	var worst_angular := 0.0
	var worst_position := 0.0
	client.simulate_action_press("forward")
	client.simulate_action_press("right")
	for frame in range(1, frames + 1):
		var ticks := skip_period == 0 or frame % skip_period != 0
		PhysicsServer3D.space_set_active(authority_space, ticks)
		PhysicsServer3D.space_set_active(owner_space, ticks or not gate_owner)
		await stepper.sync_frames(1)

		var own_slot: String = "own_tick" if ticks else "own_skip"
		var auth_slot: String = "auth_tick" if ticks else "auth_skip"
		ray_frames[own_slot] += 1
		ray_frames[auth_slot] += 1
		if own.raycast.is_colliding():
			ray_hits[own_slot] += 1
		if mirror.raycast.is_colliding():
			ray_hits[auth_slot] += 1
		nearest_on_client = minf(
			nearest_on_client,
			own.sphere_position.distance_to(other_on_client.sphere_position),
		)
		nearest_on_host = minf(
			nearest_on_host,
			mirror.sphere_position.distance_to(other_on_host.sphere_position),
		)

		owner_travel += last_own.distance_to(own.sphere_position)
		authority_travel += last_mirror.distance_to(mirror.sphere_position)
		last_own = own.sphere_position
		last_mirror = mirror.sphere_position

		if handle.last_compare_staleness != 0:
			continue
		var angular: float = handle.last_field_divergence.get(
			&"sphere_angular_velocity",
			0.0,
		)
		trajectory.append(angular)
		worst_angular = maxf(worst_angular, angular)
		worst_position = maxf(
			worst_position,
			handle.last_field_divergence.get(&"sphere_position", 0.0),
		)
	PhysicsServer3D.space_set_active(authority_space, true)
	PhysicsServer3D.space_set_active(owner_space, true)
	client.simulate_action_release("forward")
	client.simulate_action_release("right")

	var label := "control"
	if skip_period > 0:
		label = "gated" if gate_owner else "ungated"
	var fifth := maxi(1, trajectory.size() / 5)
	var first_fifth := _mean(trajectory.slice(0, fifth))
	var last_fifth := _mean(trajectory.slice(trajectory.size() - fifth))
	print(
		"[integrate] %-8s skipped=%-4d samples=%-4d travel own=%7.3f auth=%7.3f"
				% [
					label,
					skipped_ticks,
					trajectory.size(),
					owner_travel,
					authority_travel,
				],
	)
	print(
		"[integrate]   angular first fifth=%7.4f last fifth=%7.4f worst=%7.4f"
				% [first_fifth, last_fifth, worst_angular],
	)
	print(
		(
				"[integrate]   ray hit own %d/%d ticked %d/%d skipped |"
				+ " auth %d/%d ticked %d/%d skipped | nearest car c=%.2f h=%.2f"
		) % [
			ray_hits["own_tick"], ray_frames["own_tick"],
			ray_hits["own_skip"], ray_frames["own_skip"],
			ray_hits["auth_tick"], ray_frames["auth_tick"],
			ray_hits["auth_skip"], ray_frames["auth_skip"],
			nearest_on_client, nearest_on_host,
		],
	)
	_report_trajectory(trajectory)
	assert_int(clocks[1].tick - clocks[0].tick).override_failure_message(
		"both clocks must finish on the same tick count, so the arm varies the"
		+ " integration count alone",
	).is_equal(initial_tick_delta)
	return {
		"samples": trajectory.size(),
		"worst_angular": worst_angular,
		"worst_position": worst_position,
		"first_fifth": first_fifth,
		"last_fifth": last_fifth,
		"owner_travel": owner_travel,
		"authority_travel": authority_travel,
		"angular_epsilon": angular_epsilon,
	}


# Prints the divergence in fifths so a ramp is legible against a spike without
# reading the raw samples.
func _report_trajectory(trajectory: Array[float]) -> void:
	if trajectory.is_empty():
		print("[integrate]   trajectory: no matched samples")
		return
	var fifth := maxi(1, trajectory.size() / 5)
	var row := ""
	for bucket in range(0, trajectory.size(), fifth):
		row += "%7.4f " % _mean(trajectory.slice(bucket, bucket + fifth))
	print("[integrate]   trajectory by fifths: %s" % row)


func test_reports_input_quantization_delta() -> void:
	var quantized_runs: Array[float] = []
	var raw_runs: Array[float] = []
	var quantized_position_runs: Array[float] = []
	var raw_position_runs: Array[float] = []
	for repeat in 3:
		if repeat > 0:
			game = make_unmanaged_game_harness(MAIN)
			await game.setup()
		var quantized := await _measure_input_codec_mode(true)
		quantized_runs.append(quantized.angular)
		quantized_position_runs.append(quantized.position)
		await game.teardown()

		VEHICLE_CONTROLS.probe_quantize_inputs = false
		game = make_unmanaged_game_harness(MAIN)
		await game.setup()
		var raw := await _measure_input_codec_mode(false)
		raw_runs.append(raw.angular)
		raw_position_runs.append(raw.position)
		await game.teardown()
		VEHICLE_CONTROLS.probe_quantize_inputs = true

	var quantized_angular := _median(quantized_runs)
	var raw_angular := _median(raw_runs)
	var quantized_position := _median(quantized_position_runs)
	var raw_position := _median(raw_position_runs)

	print(
		"[quantization] angular runs quantized=%s raw=%s" % [
			quantized_runs,
			raw_runs,
		],
	)
	print(
		"[quantization] angular median quantized=%.6f raw=%.6f delta=%+.6f" % [
			quantized_angular,
			raw_angular,
			raw_angular - quantized_angular,
		],
	)
	print(
		"[quantization] position median quantized=%.6f raw=%.6f delta=%+.6f" % [
			quantized_position,
			raw_position,
			raw_position - quantized_position,
		],
	)
	assert_int(quantized_runs.size()).is_equal(3)
	assert_int(raw_runs.size()).is_equal(3)


func _measure_input_codec_mode(quantized: bool) -> Dictionary:
	VEHICLE_CONTROLS.probe_quantize_inputs = quantized
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await host.await_scene(&"Track", 2.0)
	await client.await_scene(&"Track", 2.0)
	var own := await client.await_player(&"luigi", 2.0)
	await host.await_player(&"luigi", 2.0)

	var handle = own.entity.prediction
	_silence_triggers(handle)
	await game.sync_ticks(8)
	# The wire map rebuilds on any rewire during warmup, so the silence is
	# re-applied once the link settles.
	_silence_triggers(handle)
	var angular: Array[float] = []
	var position: Array[float] = []
	handle.state_evaluated.connect(
		func(
				_recv_tick: int,
				_ack: int,
				_divergence: float,
				_corrected: bool,
		) -> void:
			if handle.last_compare_staleness != 0:
				return
			angular.append(
				handle.last_field_divergence.get(
					&"sphere_angular_velocity",
					0.0,
				),
			)
			position.append(
				handle.last_field_divergence.get(&"sphere_position", 0.0),
			)
	)
	client.simulate_action_press("forward")
	client.simulate_action_press("right")
	await game.sync_ticks(120)
	client.simulate_action_release("forward")
	client.simulate_action_release("right")
	return {
		"angular": _median(angular),
		"position": _median(position),
		"samples": angular.size(),
	}


# Switches every correction trigger out of reach, including the per-field
# epsilon marks the engine gathered at wire time, so the probe measures the
# bare solver with no correction machinery mixed in.
func _silence_triggers(handle) -> void:
	handle.divergence_epsilon = 1.0e9
	handle.teleport_threshold = 1.0e9
	var engine = handle._engine()
	if engine:
		engine._epsilon_overrides.clear()


func _gap(own_value: Variant, mirror_value: Variant) -> String:
	if own_value is Vector3 and mirror_value is Vector3:
		return "%.6f" % (own_value as Vector3).distance_to(mirror_value)
	if own_value is float and mirror_value is float:
		return "%.6f" % absf((own_value as float) - (mirror_value as float))
	return "same" if own_value == mirror_value else "differs"


func _propulsion_axis(body: Node) -> Vector3:
	var ground_normal := Vector3.UP
	if body.raycast.is_colliding():
		ground_normal = body.raycast.get_collision_normal().normalized()
	var heading_forward := Vector3.FORWARD.rotated(Vector3.UP, body.heading)
	var ground_forward := heading_forward.slide(ground_normal)
	if ground_forward.is_zero_approx():
		return Vector3.RIGHT.rotated(Vector3.UP, body.heading)
	return ground_forward.normalized().cross(ground_normal).normalized()


func _report(label: String, samples: Array[float]) -> void:
	if samples.is_empty():
		print("[probe] %-34s no samples" % label)
		return
	var sorted_samples := samples.duplicate()
	sorted_samples.sort()
	var last := sorted_samples.size() - 1
	print(
		"[probe] %-34s median=%7.4f  p90=%7.4f  max=%7.4f  n=%d" % [
			label,
			_median(samples),
			sorted_samples[mini(last, int(0.9 * sorted_samples.size()))],
			sorted_samples[last],
			samples.size(),
		],
	)


func _mean(samples: Array[float]) -> float:
	if samples.is_empty():
		return 0.0
	var total := 0.0
	for value in samples:
		total += value
	return total / samples.size()


func _median(samples: Array[float]) -> float:
	if samples.is_empty():
		return 0.0
	var sorted_samples := samples.duplicate()
	sorted_samples.sort()
	return sorted_samples[sorted_samples.size() / 2]
