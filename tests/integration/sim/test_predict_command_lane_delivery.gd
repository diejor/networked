## How much of the owner's command lane reaches authority, and in what shape.
##
## A rendered two-process session put 3431 transitions into 580 arrival events
## across 3527 authority frames: 84% of authority's frames received nothing and
## the rest received a mean of 5.9. The owner had written a command frame on
## every one of its 4259 frames. Authority never noticed, because the redundancy
## window healed the whole gap before any transition was needed.
##
## This drives the real lane instead of handing bytes over directly: the owner's
## clock flushes the aggregation buffer, the loopback session carries the
## datagram, and authority's poll dispatches it. If the shape survives here it is
## the engine's, and if it does not the loss is below the engine and the next
## capture needs the counters this suite reads.
##
## This is a probe, not a gate. It prints the ratio and the arrival shape.
class_name TestPredictCommandLaneDelivery
extends NetwTestSuite

const _FRAMES := 400


# Runs one physics frame on each peer with a real datagram in between.
#
# Authority runs a frame per frame, not per transition, so a throttled owner
# leaves it frames it cannot fill. The hold and quantum-fault counts printed here
# are that arithmetic and not the lane: sizing those belongs to the quantum-budget
# arms, and this arm speaks only to how much of the lane arrives and in what
# shape.
func _run_lane_arm(label: String, owner_spare_period: int) -> Dictionary:
	var scenario := PredictionScenario.new()
	await scenario.setup(self, PredictionScenario.TICKRATE, 3, false)
	var predicted := await scenario.add_predicted_entity()
	predicted.client_prediction.archetype(
		NetwLagCompensationInterface.PredictionHandle.Archetype.SOLVER_BODY,
	)
	predicted.client_prediction.schedule().frame()
	predicted.server_prediction.schedule().frame()
	predicted.server_prediction.replay_buffer_depth = 1
	predicted.client_root.motion = Vector2.RIGHT
	scenario.client_clock.tickrate = Engine.physics_ticks_per_second
	scenario.server_clock.tickrate = Engine.physics_ticks_per_second
	var period := 1000.0 / float(Engine.physics_ticks_per_second)

	var received := 0
	var arrivals: Dictionary = { }
	for index in _FRAMES:
		# The owner's frame. A spare frame emits no tick, exactly as a clock
		# throttled to a slower authority does.
		var ticks := 0 if owner_spare_period > 0 \
				and index % owner_spare_period == 0 else 1
		# The owner's frame. Opening it services the transport, the tick loop
		# runs, and closing it drives, buffers the command and flushes it.
		scenario.client_clock.before_tick_loop.emit()
		scenario.client_clock.force_step(ticks)
		scenario.client_clock.after_tick_loop.emit()

		scenario.inner.session().advance_time(period)

		# Authority's frame, at its own rate rather than the owner's. Opening it
		# is where the transport is serviced, so the sample belongs immediately
		# after: that call is the lane's unit.
		scenario.server_clock.before_tick_loop.emit()
		var now: int = predicted.server_prediction.command_frames_received
		var key := now - received
		arrivals[key] = int(arrivals.get(key, 0)) + 1
		received = now
		scenario.server_clock.force_step(1)
		scenario.server_clock.after_tick_loop.emit()

	var sent: int = predicted.client_prediction.command_frames_sent
	var keys: Array = arrivals.keys()
	keys.sort()
	var shape := PackedStringArray()
	for key: int in keys:
		shape.append("%d:%d" % [key, int(arrivals[key])])
	print(
		"[lane] %-10s owner frames=%d sent=%-4d | authority received=%-4d "
		% [label, _FRAMES, sent, received]
		+ "ratio=%.3f  consumed=%-4d hold=%-4d faults=%d"
		% [
			float(received) / maxf(1.0, float(sent)),
			int(predicted.server_prediction.consumed_count),
			int(predicted.server_prediction.held_count),
			int(predicted.server_prediction.quantum_fault_count),
		],
	)
	print("[lane]   frames received per authority frame: %s" % ", ".join(shape))
	await scenario.teardown()
	return { &"sent": sent, &"received": received, &"arrivals": arrivals }


# The straight question. Every command frame the owner writes should reach
# authority, because the lane re-sends its window and the loopback drops nothing.
func test_the_lane_delivers_every_frame_the_owner_writes() -> void:
	var arm := await _run_lane_arm("steady", 0)

	assert_int(int(arm[&"sent"])).override_failure_message(
		"the owner wrote no command frame, so the arm measures no lane",
	).is_greater(0)
	assert_float(float(arm[&"received"]) / maxf(1.0, float(arm[&"sent"]))) \
			.override_failure_message(
				(
						"authority decoded %d of the %d owner-lane frames "
						+ "written, so the engine's own path loses frames and a "
						+ "session's clumping starts here"
				) % [int(arm[&"received"]), int(arm[&"sent"])],
			).is_greater(0.99)


# A throttled owner is the rendered condition, and the frame it holds is where
# the lane's cadence used to break: a frame that emits no tick runs no tick-loop
# flush, so its command frame waited and left paired with the next tick's.
#
# The frame-end flush makes the lane one frame out per frame written, whatever
# the owner's tick pattern. Read this arm as a cadence law, not as a quantum one:
# the frame a throttled owner holds authors no transition, so the frame that used
# to arrive paired carried only redundant window and the queue never saw it.
# Fixing the cadence removes uplink latency and makes the two lane counters read
# one for one. It does not move a quantum fault, and must not be sold as though
# it does.
func test_a_held_frame_still_sends_its_commands_in_its_own_frame() -> void:
	var arm := await _run_lane_arm("spare6", 6)

	assert_int(int(arm[&"sent"])).is_greater(0)
	assert_float(float(arm[&"received"]) / maxf(1.0, float(arm[&"sent"]))) \
			.override_failure_message(
				(
						"a throttled owner lost frames rather than delivering "
						+ "them: %d written, %d decoded"
				) % [int(arm[&"sent"]), int(arm[&"received"])],
			).is_greater(0.99)
	var arrivals: Dictionary = arm[&"arrivals"]
	assert_int(int(arrivals.get(2, 0))).override_failure_message(
		(
				"%d authority frames decoded two owner-lane frames at once, so "
				+ "a held frame is still parking its commands behind a flush it "
				+ "already missed"
		) % int(arrivals.get(2, 0)),
	).is_equal(0)


# The law the render-cadence poll broke. A datagram is only sent or received
# where the transport is serviced, so whatever drives that call decides how often
# a peer's simulation can see the network. Riding the rendered frame made a
# peer's framerate govern its neighbour's input arrival, which reaches the
# simulation as a transition that spent the wrong amount of physics rather than
# as latency.
#
# This run contains no rendered frame at all: no idle callback, no await, no
# [method MultiplayerAPI.poll]. Only the clock's own frame boundary drives
# anything. Before the transport rode that boundary, nothing crossed the wire.
func test_the_transport_moves_a_frame_with_no_rendered_frame_in_it() -> void:
	var scenario := PredictionScenario.new()
	await scenario.setup(self, PredictionScenario.TICKRATE, 3, false)
	var predicted := await scenario.add_predicted_entity()
	predicted.client_prediction.schedule().frame()
	predicted.server_prediction.schedule().frame()
	predicted.server_prediction.replay_buffer_depth = 1
	predicted.client_root.motion = Vector2.RIGHT
	scenario.client_clock.tickrate = Engine.physics_ticks_per_second
	scenario.server_clock.tickrate = Engine.physics_ticks_per_second
	var period := 1000.0 / float(Engine.physics_ticks_per_second)

	for _i in 60:
		scenario.client_clock.before_tick_loop.emit()
		scenario.client_clock.force_step(1)
		scenario.client_clock.after_tick_loop.emit()
		scenario.inner.session().advance_time(period)
		scenario.server_clock.before_tick_loop.emit()
		scenario.server_clock.force_step(1)
		scenario.server_clock.after_tick_loop.emit()

	var received: int = predicted.server_prediction.command_frames_received
	var consumed: int = predicted.server_prediction.consumed_count
	await scenario.teardown()

	assert_int(received).override_failure_message(
		"authority decoded no owner-lane frame across 60 physics frames with no "
		+ "rendered frame among them, so the transport is riding the render "
		+ "cadence and a peer's framerate governs its neighbour's input",
	).is_greater(0)
	assert_int(consumed).override_failure_message(
		"authority received the lane but consumed nothing from it",
	).is_greater(0)
