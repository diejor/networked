## Where the transition-to-physics correspondence actually breaks, counted.
##
## A transition is a fixed quantum of simulated time, and six places in the FRAME
## tier can spend a different amount: the owner's authoring clamp and speculation
## throttle, a multi-tick frame, and authority's hold, starve, and catch-up
## drain. Each is a frame that integrated without a transition, or a transition
## that ran without its integration.
##
## This is a probe, not a gate. It prints which sites fire and how often under a
## clean lane and under arrival jitter, so a repair is sized against the sites
## that actually fire rather than against the list of the ones that could.
class_name TestPredictQuantumBudget
extends NetwTestSuite

const FRAME := NetwLagCompensationInterface.PredictionHandle.Schedule.FRAME
const _FRAMES := 240

# Wall milliseconds between the owner's physics frames. The owner is the peer
# that keeps its own rate, so this one is fixed.
const _OWNER_PERIOD_MS := 1000.0 / 60.0

# Authority frame periods in whole wall milliseconds, cycled rather than drawn so
# the arm is deterministic. This one is an authority that cannot hold its own
# declared rate: mean 20.25 ms against a 16.67 ms budget, spread 14 to 27.
const _SLOW_AUTHORITY_PERIODS: Array[int] = [
	17, 20, 23, 19, 25, 16, 21, 18, 27, 20, 14, 22, 19, 24, 18, 21,
]

# The same authority keeping up, as the control the slow arm is read against.
const _PROMPT_AUTHORITY_PERIODS: Array[int] = [
	16, 17, 17, 16, 17, 17, 16, 17,
]


func _configure_frame(predicted: PredictedEntity) -> void:
	predicted.client_prediction.schedule().frame()
	predicted.server_prediction.schedule().frame()
	predicted.server_prediction.replay_buffer_depth = 1
	predicted.server_prediction.missing_policy = \
			NetwLagCompensationInterface.PredictionHandle.MissingInput.REPEAT_LAST


func _quantize_motion(predicted: PredictedEntity) -> void:
	for binding: NetwSyncSetBinding in [
		predicted.client_input,
		predicted.server_input,
	]:
		for field in binding.set.fields:
			if field.key != &"motion":
				continue
			field.quantizer = NetwQuantizeBits.new().bits(16).limits(-1.0, 1.0)


func _emit_client_frame(clock: NetwClockInterface, ticks: int) -> void:
	clock.before_tick_loop.emit()
	# Always stepped, even for a frame that runs no tick, because a gated clock
	# owes a decision on every frame and a frame it never heard about would keep
	# the previous one's answer.
	clock.force_step(ticks)
	clock.after_tick_loop.emit()


func _build_command(predicted: PredictedEntity) -> PackedByteArray:
	return predicted.client_prediction._engine().build_command_frame()


func _deliver(predicted: PredictedEntity, bytes: PackedByteArray) -> void:
	if not bytes.is_empty():
		predicted.server_prediction._engine().receive_command_frame(bytes)


func _deliver_ack(predicted: PredictedEntity) -> void:
	var bytes: PackedByteArray = \
			predicted.server_prediction._engine().build_ack_frame()
	if not bytes.is_empty():
		predicted.client_prediction._engine().receive_ack_frame(bytes)


# Drives both peers one physics frame each, so the two spend the same wall time
# and only the transition bookkeeping can differ. [param delay_period] holds each
# nth command back by one frame, which is the arrival jitter the standing buffer
# exists to absorb.
func _run_arm(
		label: String,
		delay_period: int,
		client_spare_period: int = 0,
		client_double_period: int = 0,
		gated: bool = false,
) -> Dictionary:
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	if gated:
		# The archetype that says the physics server integrates this body is the
		# declaration that arms the gate. Nothing else does, which is why every
		# other arm here is unaffected.
		predicted.client_prediction.archetype(
			NetwLagCompensationInterface.PredictionHandle.Archetype.SOLVER_BODY,
		)
	_configure_frame(predicted)
	_quantize_motion(predicted)
	# The pump emits one frame per call on both peers, so both clocks declare one
	# physics step per transition and the harness matches what it claims. A peer
	# left at the default 30 against 60 Hz physics declares two steps per
	# transition and spends one, which is a fault in the harness rather than in
	# the run it is supposed to measure.
	scenario.client_clock.tickrate = Engine.physics_ticks_per_second
	scenario.server_clock.tickrate = Engine.physics_ticks_per_second
	predicted.client_root.motion = Vector2.RIGHT

	var deferred := PackedByteArray()
	var faults := 0
	var fault_log := PackedStringArray()
	for index in _FRAMES:
		# Both peers run the same wall time, so both run the same number of
		# physics frames. Stepping authority once per loop iteration instead of
		# once per owner frame is what made an earlier version of this probe
		# report a consume side that never held: it silently gave authority one
		# frame for every owner transition, which is the one thing a throttled
		# owner cannot supply.
		var owner_ticks: Array[int] = []
		if client_spare_period > 0 and index % client_spare_period == 0:
			# A spare frame is the rendered regime: the owner's physics keeps its
			# own rate while its clock is throttled to the authority it tracks,
			# so the frame runs and no transition accounts for it.
			owner_ticks.append(0)
		# A double-tick frame is the other direction: two transitions labelled
		# against one solve, and a surplus the authority's drain then works off
		# by running two transitions against one solve of its own.
		owner_ticks.append(
			2 if client_double_period > 0 and index % client_double_period == 0
			else 1
		)
		for ticks: int in owner_ticks:
			_emit_client_frame(scenario.client_clock, ticks)
			var bytes := _build_command(predicted)
			if not deferred.is_empty():
				_deliver(predicted, deferred)
				deferred = PackedByteArray()
			if delay_period > 0 and index % delay_period == 0:
				deferred = bytes
			else:
				_deliver(predicted, bytes)
			scenario.server_sim.before_frame_step()
			predicted.server_prediction.simulate_frame(scenario.dt())
			_deliver_ack(predicted)
		var now: int = predicted.client_prediction.quantum_fault_count
		if now != faults and fault_log.size() < 6:
			fault_log.append(
				"iter %d steps %d declared %d gated %s" % [
					index,
					predicted.client_prediction.quantum_steps,
					predicted.client_prediction.quantum_declared,
					scenario.client_clock.is_gated(),
				],
			)
		faults = now

	var client: Dictionary = predicted.client_prediction.stats()
	var server: Dictionary = predicted.server_prediction.stats()
	print(
		"[quantum] %-8s owner: clamped=%-4d held=%-4d faults=%-4d | "
		% [
			label,
			int(client[&"authoring_clamped"]),
			int(client[&"speculation_held"]),
			int(client[&"quantum_faults"]),
		]
		+ "authority: hold=%-4d starve=%-4d faults=%-4d"
		% [
			int(server[&"held"]),
			int(server[&"starved"]),
			int(server[&"quantum_faults"]),
		],
	)
	for line in fault_log:
		print("[quantum]   fault at %s" % line)
	var armed: bool = scenario.client_clock.is_gated()
	await scenario.teardown()
	return { &"client": client, &"server": server, &"gated": armed }


func test_reports_where_the_quantum_breaks_on_a_clean_lane() -> void:
	var arm := await _run_arm("clean", 0)
	assert_int(int(arm[&"server"][&"consumed"])).override_failure_message(
		"the arm consumed nothing, so its counters describe no run",
	).is_greater(0)
	# The arming condition, asserted rather than assumed. Only the archetype
	# that says the physics server integrates this body holds a frame, so a
	# kinematic game never sees one and needs no vocabulary for it.
	assert_bool(arm[&"gated"]).override_failure_message(
		"a body the engine does not integrate must arm no gate",
	).is_false()


func test_reports_where_the_quantum_breaks_under_arrival_jitter() -> void:
	var arm := await _run_arm("jitter8", 8)
	assert_int(int(arm[&"server"][&"consumed"])).override_failure_message(
		"the arm consumed nothing, so its counters describe no run",
	).is_greater(0)


func test_reports_where_the_quantum_breaks_under_heavy_jitter() -> void:
	var arm := await _run_arm("jitter3", 3)
	assert_int(int(arm[&"server"][&"consumed"])).override_failure_message(
		"the arm consumed nothing, so its counters describe no run",
	).is_greater(0)


# The rendered regime, reproduced: an owner whose physics runs at its own rate
# while its clock is throttled to a slower authority. The measured session ran
# 3099 physics frames against 2653 ticks, one spare frame in seven, so this arm
# carries the same ratio.
func test_reports_where_the_quantum_breaks_with_owner_spare_frames() -> void:
	var arm := await _run_arm("spare7", 0, 7)
	assert_int(int(arm[&"server"][&"consumed"])).override_failure_message(
		"the arm consumed nothing, so its counters describe no run",
	).is_greater(0)
	# The point of the arm. A spare frame is invisible to every compared column
	# except the one this campaign added, so it must not read as healthy.
	assert_int(int(arm[&"client"][&"quantum_faults"])).override_failure_message(
		"an owner spending physics on frames no transition claims must count "
		+ "every one of them",
	).is_greater(0)


# The other direction. A frame that emitted two ticks labels two transitions
# against one solve, and the surplus it hands the authority is what the consume
# drain exists to work off, by running two transitions against one solve of its
# own.
func test_reports_where_the_quantum_breaks_with_owner_double_tick_frames() -> void:
	var arm := await _run_arm("double8", 0, 0, 8)
	assert_int(int(arm[&"server"][&"consumed"])).override_failure_message(
		"the arm consumed nothing, so its counters describe no run",
	).is_greater(0)


# The repair, against the arm that measured the defect. A gated owner declines
# the solve on a frame no transition claims, so its spare frames stop costing
# simulated time and the fault they used to produce becomes unreachable.
#
# Clamped frames do not go away, and should not: the owner still has spare
# frames and still declines to author on them. What changes is that it no longer
# spends physics on them.
func test_the_gate_closes_the_owner_spare_frame_fault() -> void:
	var gated := await _run_arm("gated7", 0, 7, 0, true)

	assert_bool(gated[&"gated"]).override_failure_message(
		"the arm never armed a gate, so it measures the defect and not the "
		+ "repair",
	).is_true()
	assert_int(int(gated[&"client"][&"authoring_clamped"])) \
			.override_failure_message(
				"the gated arm must still have spare frames, or it is a "
				+ "different run rather than a repaired one",
			).is_greater(0)
	assert_int(int(gated[&"client"][&"quantum_faults"])) \
			.override_failure_message(
				(
						"a gated owner must spend no physics on a frame no "
						+ "transition claims, but counted %d faults across %d "
						+ "clamped frames"
				) % [
					int(gated[&"client"][&"quantum_faults"]),
					int(gated[&"client"][&"authoring_clamped"]),
				],
			).is_equal(0)
	# And the half the gate does not reach. An owner that holds a frame authors
	# no transition for it, while authority still steps its own world once per
	# frame, so the frame authority cannot fill is charged to authority instead.
	# The invariant is not held until it is held on both peers.
	assert_int(int(gated[&"server"][&"quantum_faults"])) \
			.override_failure_message(
				"the arm no longer reproduces the consume-side half of the "
				+ "defect, so it cannot judge a repair for it",
			).is_greater(0)


# The same repair at a much denser spare-frame ratio. A residual that stays at
# one whatever the density is a startup transient; one that scales with the
# spare frames would mean the gate is only partly holding.
func test_the_gate_holds_at_a_dense_spare_frame_ratio() -> void:
	var gated := await _run_arm("gated3", 0, 3, 0, true)

	assert_int(int(gated[&"client"][&"authoring_clamped"])).is_greater(60)
	assert_int(int(gated[&"client"][&"quantum_faults"])) \
			.override_failure_message(
				"a denser spare ratio must not produce more faults, or the gate "
				+ "is holding only some of the frames it should",
			).is_less(2)


# Runs the two peers on one wall-time axis at their own frame rates, and lands
# their commands in clumps rather than one per frame.
#
# Every arm above steps authority once per owner frame and hands over each
# command inside the frame that wrote it. A standing buffer measured that way
# never empties and never overflows, so those arms report a consume side that
# barely holds, which is why the consume-side sites were sized as negligible and
# retired.
#
# [param arrival_period] is the measured shape instead: commands accumulate and
# reach authority every nth of its frames. A rendered two-process session put
# 3431 transitions into 580 arrival events over 3527 authority frames, so 84% of
# authority's frames received nothing and the rest received a mean of 5.9. That
# is the arrival a standing buffer has to cover, and no arm reached it before.
#
# The owner emits a tick only where authority's slower clock has bought one,
# which is what the live throttle does: the owner keeps its physics rate and
# gives up transitions, and the gate holds the frames it gave up.
func _run_rate_arm(
		label: String,
		authority_periods: Array[int],
		buffer: int,
		arrival_period: int = 1,
		seconds: float = 20.0,
) -> Dictionary:
	var scenario := PredictionScenario.new()
	# Unmanaged so an arm can run beside another one in the same case: the suite
	# builds exactly one managed harness and a comparison needs two.
	await scenario.setup(self, PredictionScenario.TICKRATE, 3, false)
	var predicted := await scenario.add_predicted_entity()
	predicted.client_prediction.archetype(
		NetwLagCompensationInterface.PredictionHandle.Archetype.SOLVER_BODY,
	)
	_configure_frame(predicted)
	predicted.server_prediction.replay_buffer_depth = buffer
	_quantize_motion(predicted)
	scenario.client_clock.tickrate = Engine.physics_ticks_per_second
	scenario.server_clock.tickrate = Engine.physics_ticks_per_second
	predicted.client_root.motion = Vector2.RIGHT

	var authority_mean := 0.0
	for period: int in authority_periods:
		authority_mean += float(period)
	authority_mean /= float(authority_periods.size())

	var horizon := seconds * 1000.0
	var owner_at := 0.0
	var authority_at := 0.0
	var authority_index := 0
	var owner_ticks := 0
	var owner_frames := 0
	var authority_frames := 0
	var previous_consumed := 0
	# One row per authority frame, keyed the way the netlog keys it: how many
	# transitions the frame ran against how much simulated time the last of them
	# measured.
	var shape: Dictionary = { }
	var in_flight: Array[PackedByteArray] = []
	while owner_at < horizon and authority_at < horizon:
		if owner_at <= authority_at:
			var owed := int(owner_at / authority_mean)
			var ticks := 1 if owed > owner_ticks else 0
			owner_ticks += ticks
			owner_frames += 1
			_emit_client_frame(scenario.client_clock, ticks)
			in_flight.append(_build_command(predicted))
			owner_at += _OWNER_PERIOD_MS
			continue
		# The lane hands its accumulated frames over together. Order is preserved,
		# so nothing is lost: only the moment authority can see it moves.
		if authority_index % maxi(1, arrival_period) == 0:
			for bytes: PackedByteArray in in_flight:
				_deliver(predicted, bytes)
			in_flight.clear()
		scenario.server_sim.before_frame_step()
		predicted.server_prediction.simulate_frame(scenario.dt())
		_deliver_ack(predicted)
		authority_frames += 1
		var consumed: int = predicted.server_prediction.consumed_count
		var key := "(%d,%d)" % [
			consumed - previous_consumed,
			predicted.server_prediction.quantum_steps,
		]
		shape[key] = int(shape.get(key, 0)) + 1
		previous_consumed = consumed
		authority_at += float(
			authority_periods[authority_index % authority_periods.size()],
		)
		authority_index += 1

	var client: Dictionary = predicted.client_prediction.stats()
	var server: Dictionary = predicted.server_prediction.stats()
	var keys: Array = shape.keys()
	keys.sort()
	var shape_text := PackedStringArray()
	for key: String in keys:
		shape_text.append("%s x%d" % [key, int(shape[key])])
	print(
		"[rate] %-14s arrival=1/%-2d buffer=%d owner: frames=%-5d ticks=%-5d clamped=%-5d "
		% [
			label,
			arrival_period,
			buffer,
			owner_frames,
			owner_ticks,
			int(client[&"authoring_clamped"]),
		]
		+ "faults=%-4d | authority: frames=%-5d consumed=%-5d hold=%-5d "
		% [
			int(client[&"quantum_faults"]),
			authority_frames,
			int(server[&"consumed"]),
			int(server[&"held"]),
		]
		+ "starve=%-4d faults=%-5d"
		% [
			int(server[&"starved"]),
			int(server[&"quantum_faults"]),
		],
	)
	print("[rate]   (consumed,steps) per authority frame: %s" % ", ".join(
		shape_text,
	))
	await scenario.teardown()
	return {
		&"client": client,
		&"server": server,
		&"owner_frames": owner_frames,
		&"authority_frames": authority_frames,
	}


# The arrival shape a rendered two-process session delivered: one clump every
# sixth authority frame. Consuming one transition per frame absorbs it, because
# a clump is depth and depth is what a one-per-frame consumer spends down at its
# own pace. What used to break here was the catch-up drain answering that depth
# by running two transitions against one solve.
#
# The arm asserts its own regime first. A clumped lane that stopped clumping
# would otherwise report a repair it never tested.
func test_a_batched_command_lane_no_longer_breaks_the_quantum() -> void:
	var arm := await _run_rate_arm("batched", _SLOW_AUTHORITY_PERIODS, 1, 6)
	var server: Dictionary = arm[&"server"]
	var arrivals: Dictionary = server[&"arrivals"]
	var frames := int(arm[&"authority_frames"])

	assert_int(int(server[&"consumed"])).override_failure_message(
		"the arm consumed nothing, so its counters describe no run",
	).is_greater(0)
	assert_int(int(arm[&"client"][&"quantum_faults"])) \
			.override_failure_message(
				"the gate must still hold the owner's every frame, or this arm "
				+ "measures the owner's half rather than authority's",
			).is_equal(0)
	# The regime, asserted: most frames receive nothing and the rest arrive
	# several at a time. Without this the run below proves nothing.
	assert_int(int(arrivals.get(0, 0))).override_failure_message(
		"a lane that feeds every frame is not the clumped regime this arm "
		+ "exists to consume, so it cannot judge the consume rule: %s" % arrivals,
	).is_greater(frames / 4)

	assert_int(int(server[&"quantum_faults"])).override_failure_message(
		"a clumped lane must cost authority no simulated time it cannot "
		+ "attribute, but %d of %d frames spent some: %s"
		% [int(server[&"quantum_faults"]), frames, server[&"consume_shape"]],
	).is_less(frames / 20)
	assert_int(int(server[&"consume_shape"].get("1,1", 0))) \
			.override_failure_message(
				"one transition against one solve must be the ordinary frame, "
				+ "got %s" % server[&"consume_shape"],
			).is_greater(frames * 9 / 10)


# What the arrival shape costs, isolated. The same peers and the same rates, with
# the lane handing its frames over as it writes them, so the only thing that
# moved is when authority could see them.
func test_a_per_frame_command_lane_holds_the_quantum_on_both_peers() -> void:
	var batched := await _run_rate_arm("batched", _SLOW_AUTHORITY_PERIODS, 1, 6)
	var prompt := await _run_rate_arm("prompt", _SLOW_AUTHORITY_PERIODS, 1, 1)

	assert_int(int(prompt[&"server"][&"quantum_faults"])) \
			.override_failure_message(
				(
						"a lane that delivers as it writes must spend far fewer "
						+ "transitions off their own simulated time, but counted "
						+ "%d against the batched lane's %d"
				) % [
					int(prompt[&"server"][&"quantum_faults"]),
					int(batched[&"server"][&"quantum_faults"]),
				],
			).is_less(int(batched[&"server"][&"quantum_faults"]))


# The standing buffer is a constant offset on queue depth and nothing else.
#
# Absorbing jitter means draining faster than the stream fills. Authority
# replays at most one transition per frame, so it never can, and the reserve a
# deeper buffer holds is a reserve it can never spend. Raising the buffer
# therefore moves every arrival further from the frame that runs it and leaves
# the consume pattern identical, which is why the default is zero.
#
# Pinned so the cheap lever is refused once rather than re-proposed. It is also
# the arm that would catch a future change quietly reintroducing a drain: the
# only way an eightfold buffer can change the fault count is if something
# started consuming more than one transition on a solve.
func test_a_deeper_standing_buffer_only_adds_latency() -> void:
	var shallow := await _run_rate_arm("batched", _SLOW_AUTHORITY_PERIODS, 1, 6)
	var deep := await _run_rate_arm("batched-deep", _SLOW_AUTHORITY_PERIODS, 8, 6)

	var shallow_faults := int(shallow[&"server"][&"quantum_faults"])
	var deep_faults := int(deep[&"server"][&"quantum_faults"])
	assert_int(deep_faults).override_failure_message(
		(
				"an eightfold buffer moved the fault count from %d to %d, so it "
				+ "reaches the consume pattern after all and something is "
				+ "spending the reserve"
		) % [shallow_faults, deep_faults],
	).is_equal(shallow_faults)

	# What it does buy is exactly the latency, standing in the queue.
	var shallow_depth := _mean_depth(shallow[&"server"])
	var deep_depth := _mean_depth(deep[&"server"])
	assert_float(deep_depth - shallow_depth).override_failure_message(
		(
				"seven extra ticks of buffer must show up as seven extra ticks "
				+ "of standing depth, got %.2f against %.2f"
		) % [deep_depth, shallow_depth],
	).is_greater(5.0)


# Mean standing queue depth over the run, from the engine's own histogram.
func _mean_depth(server: Dictionary) -> float:
	var histogram: Dictionary = server[&"replay_depth"]
	var frames := 0
	var total := 0
	for depth: int in histogram:
		frames += int(histogram[depth])
		total += depth * int(histogram[depth])
	return float(total) / maxf(1.0, float(frames))
