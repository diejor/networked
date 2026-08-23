extends NetwTestSuite
## Throwaway probe: drives the predicted car into the host's stationary car and
## profiles how recovery behaves at contact, to confirm or refute the contact
## limit-cycle diagnosis before any kernel or config change.
##
## Two runs of the same collision, one variable each:
##
##   baseline          the shipped racing config. The car declares no island,
##                     so every contact divergence is out of domain, every
##                     recovery re-bases the whole closure (withheld marks do
##                     not apply out of domain), and a non-shrinking train
##                     escalates to a teleport-tier full closure.
##   exact_restore     snap_restore = EXACT, so the restore stops projecting
##                     along the diverged replicated velocity and lands at the
##                     stale acknowledged pose instead.
##
## This is a probe, not a gate. It prints distributions and asserts only that
## the run actually reached the other car and produced comparisons.

const MAIN := preload("res://examples/racing/main.tscn")

const POSITION_EPSILON := 0.35
const CONTACT_DISTANCE := 2.0
const DRIVE_TICKS := 480
const SETTLE_TICKS := 600
const CONVERGED_RECEIVES := 30

var game: NetwGameHarness


func before_test() -> void:
	game = make_game_harness(MAIN)
	await game.setup()


func after_test() -> void:
	if is_instance_valid(game):
		await game.teardown()
	await super.after_test()


# Everything one run records, filled by signal connections and per-tick samples.
class Recording:
	extends RefCounted

	var clock: NetwClockHandle

	# recovered: one row per recovery.
	var recovery_ticks: Array[int] = []
	var recovery_deltas: Array[Vector3] = []
	var recovery_teleported: Array[bool] = []
	var recovery_attribution: Array[int] = []

	# state_evaluated: one row per receive.
	var eval_count: int = 0
	var eval_lin_divergence: Array[float] = []
	var eval_diverged: Array[bool] = []
	var converged_streak: int = 0

	# divergence_detected, whether or not recovered.
	var detected_attribution: Dictionary = { }

	# per-tick samples.
	var max_offset: float = 0.0
	var min_distance: float = INF

	func attach(handle) -> void:
		handle.recovered.connect(
			func(_entry: int, deltas: Dictionary, teleported: bool, attribution: int) -> void:
				recovery_ticks.append(clock.tick)
				recovery_deltas.append(deltas.get(&"sphere_position", Vector3.ZERO))
				recovery_teleported.append(teleported)
				recovery_attribution.append(attribution),
		)
		handle.state_evaluated.connect(
			func(_recv_tick: int, _ack: int, divergence: float, diverged: bool) -> void:
				eval_count += 1
				eval_lin_divergence.append(
					handle.last_field_divergence.get(&"sphere_linear_velocity", 0.0),
				)
				eval_diverged.append(diverged)
				if divergence < POSITION_EPSILON:
					converged_streak += 1
				else:
					converged_streak = 0,
		)
		handle.divergence_detected.connect(
			func(_entry: int, attribution: int) -> void:
				detected_attribution[attribution] = \
						int(detected_attribution.get(attribution, 0)) + 1,
		)

	func sample(own: Node, target: Node) -> void:
		# The render offset the declared chase carries: how far the displayed
		# pose sits from the live body it glides onto.
		max_offset = maxf(
			max_offset,
			(
					(own.display_position as Vector3)
					- (own.sphere_position as Vector3)
			).length(),
		)
		if is_instance_valid(target):
			min_distance = minf(
				min_distance,
				(own.sphere_position - target.sphere_position).length(),
			)


func _attribution_name(value: int) -> String:
	var keys := NetwPredictJournal.attribution_names().keys()
	return String(keys[value]) if value >= 0 and value < keys.size() else str(value)


# Drives own at target with the capture's bang-bang aim (the sphere rolls along
# +Z at heading zero, so the bearing is taken without negating the delta), then
# releases input and waits for convergence or the settle deadline.
func _run_collision(own: Node, target: Node, rec: Recording) -> void:
	var inputs: Node = own.inputs
	inputs.state[inputs.accelerate] = true
	for i in DRIVE_TICKS:
		var to_target: Vector3 = target.sphere_position - own.sphere_position
		var want := atan2(to_target.x, to_target.z)
		var error := angle_difference(own.heading, want)
		inputs.state[inputs.steer_left] = error > 0.05
		inputs.state[inputs.steer_right] = error < -0.05
		await game.sync_ticks(1)
		rec.sample(own, target)
	inputs.state[inputs.accelerate] = false
	inputs.state[inputs.steer_left] = false
	inputs.state[inputs.steer_right] = false
	var settle_start := rec.clock.tick
	for i in SETTLE_TICKS:
		if rec.converged_streak >= CONVERGED_RECEIVES:
			break
		await game.sync_ticks(1)
		rec.sample(own, target)
	print("[contact] settle: %d ticks to %d sub-epsilon receives (streak %d)" % [
		rec.clock.tick - settle_start,
		CONVERGED_RECEIVES,
		rec.converged_streak,
	])


func _report(label: String, rec: Recording, handle) -> void:
	var n := rec.recovery_ticks.size()
	var teleports := 0
	for t in rec.recovery_teleported:
		if t:
			teleports += 1

	# Cadence between recoveries, in ticks.
	var intervals: Array[int] = []
	for i in range(1, n):
		intervals.append(rec.recovery_ticks[i] - rec.recovery_ticks[i - 1])
	intervals.sort()
	var median_interval := intervals[intervals.size() / 2] if not intervals.is_empty() else -1

	# Sign pattern and magnitude of the position deltas.
	var alternating := 0
	var max_delta := 0.0
	for i in range(n):
		max_delta = maxf(max_delta, rec.recovery_deltas[i].length())
		if i > 0 and rec.recovery_deltas[i].dot(rec.recovery_deltas[i - 1]) < 0.0:
			alternating += 1
	var alternating_fraction := float(alternating) / maxf(1.0, float(n - 1))

	# How long the withheld linear velocity stays diverged across receives.
	var streak := 0
	var worst_streak := 0
	var lin_peak := 0.0
	for d in rec.eval_lin_divergence:
		lin_peak = maxf(lin_peak, d)
		streak = streak + 1 if d > 0.5 else 0
		worst_streak = maxi(worst_streak, streak)

	var recovered_hist: Dictionary = { }
	for a in rec.recovery_attribution:
		recovered_hist[a] = int(recovered_hist.get(a, 0)) + 1

	var domains: PackedByteArray = handle.journal().domains() if handle.journal() else PackedByteArray()
	var out_of_domain := 0
	for d in domains:
		if int(d) == NetwPredictJournal.Domain.OUT_OF_DOMAIN:
			out_of_domain += 1

	print("[contact:%s] recoveries=%d teleports=%d receives=%d min_dist=%.2f" % [
		label, n, teleports, rec.eval_count, rec.min_distance,
	])
	print("[contact:%s] cadence ticks median=%d min=%d max=%d" % [
		label,
		median_interval,
		intervals[0] if not intervals.is_empty() else -1,
		intervals[-1] if not intervals.is_empty() else -1,
	])
	print("[contact:%s] deltas max=%.3fm alternating=%.0f%%" % [
		label, max_delta, alternating_fraction * 100.0,
	])
	print("[contact:%s] offset max=%.3fm amplification=x%.1f" % [
		label, rec.max_offset, rec.max_offset / maxf(0.001, max_delta),
	])
	print("[contact:%s] lin_vel divergence peak=%.2f worst_streak=%d receives" % [
		label, lin_peak, worst_streak,
	])
	for a in recovered_hist:
		print("[contact:%s] recovered attribution %s=%d" % [
			label, _attribution_name(a), recovered_hist[a],
		])
	for a in rec.detected_attribution:
		print("[contact:%s] detected attribution %s=%d" % [
			label, _attribution_name(a), rec.detected_attribution[a],
		])
	print("[contact:%s] journal transitions=%d out_of_domain=%d" % [
		label, domains.size(), out_of_domain,
	])
	# Row-level charges: the acknowledgement lane's verdicts as the journal
	# retains them, independent of whether a correction read them in time.
	var journal = handle.journal()
	var row_charges: Dictionary = { }
	for t in journal.transitions():
		var a: int = journal.attribution_at(t)
		row_charges[a] = row_charges.get(a, 0) + 1
	for a in row_charges:
		print("[contact:%s] journal row charge %s=%d" % [
			label, _attribution_name(a), row_charges[a],
		])

	assert_float(rec.min_distance) \
			.override_failure_message(
				"the drive never reached the other car (min %.2fm), the profile "
				% rec.min_distance + "measures nothing",
			).is_less(CONTACT_DISTANCE)
	assert_int(rec.eval_count) \
			.override_failure_message("no authoritative comparisons arrived") \
			.is_greater(0)


func _setup_collision() -> Array:
	var host := await game.add_host("mario", false)
	var client := await game.add_client("luigi", false)
	await host.await_scene(&"Track", 2.0)
	await client.await_scene(&"Track", 2.0)
	var own := await client.await_player(&"luigi", 2.0)
	var target := await client.await_player(&"mario", 2.0)
	await game.sync_ticks(12)
	var rec := Recording.new()
	rec.clock = client.tree.api._native_core.clock_handle
	rec.attach(own.entity.prediction)
	return [own, target, rec]


func test_contact_baseline_profile() -> void:
	var parts := await _setup_collision()
	var own: Node = parts[0]
	var handle = own.entity.prediction
	var engine = handle._engine()
	print("[contact:baseline] withheld marks=%s" % [
		engine._wiring.withheld if engine else { },
	])
	await _run_collision(own, parts[1], parts[2])
	_report("baseline", parts[2], handle)


func test_contact_with_exact_restore() -> void:
	var parts := await _setup_collision()
	var own: Node = parts[0]
	var handle = own.entity.prediction
	handle.snap_restore = NetwPredict.RestoreMode.EXACT
	await _run_collision(own, parts[1], parts[2])
	_report("exact_restore", parts[2], handle)
