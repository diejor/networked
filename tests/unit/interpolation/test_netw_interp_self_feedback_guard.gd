## The self-feedback guard (RC1): a predicted or authority pump never writes a
## channel back onto its own sampled property.
##
## When a property is interpolated in place — no visual_root, no .to() redirect —
## the output writes the very property it samples. On a frozen REMOTE display that
## is the display itself and is fine. But on a peer that simulates the entity
## locally (a PREDICTED chase or an AUTHORITY bracketed pump) it feeds the smoothed
## display value back into the simulation, the drag behind the racing rubber
## banding. The kernels skip the write for such a channel; this drives the real
## kernels node-free and proves the skip.
class_name TestNetwInterpSelfFeedbackGuard
extends NetwTestSuite

class _Body:
	extends RefCounted
	var value: float = 0.0


func _chase_runtime(self_feedback: bool) -> Dictionary:
	var rt := NetwDisplayRuntime.new()
	rt.config = NetwDisplayDecl.new()
	rt.playhead = NetwDisplayPlayhead.new()
	rt.pump_mode = NetwDisplayDecl.PUMP_CHASE

	var body := _Body.new()
	body.value = 5.0

	var state := NetwDisplayChannel.new()
	state.name = &"value"
	state.spec = NetwInterpolate.new().lerp().smooth(0.05)
	state.source_obj = body
	state.source_prop = &"value"
	state.target_prop = &"value"
	state.history = NetwDisplayHistory.new()
	state.history.mode = state.spec.mode
	state.self_feedback = self_feedback
	var writer := NetwInterpRecordingWriter.new()
	state.output = writer.write
	state.last_written = 0.0

	rt.states.append(state)
	return { rt = rt, writer = writer, body = body }


func _history_runtime(self_feedback: bool, pump_mode: int) -> Dictionary:
	var rt := NetwDisplayRuntime.new()
	rt.config = NetwDisplayDecl.new()
	rt.playhead = NetwDisplayPlayhead.new()
	rt.playhead.expected_interval_ticks = 1
	rt.pump_mode = pump_mode

	var state := NetwDisplayChannel.new()
	state.name = &"value"
	state.spec = NetwInterpolate.new().lerp().smooth(0.05)
	state.source_prop = &"value"
	state.target_prop = &"value"
	state.history = NetwDisplayHistory.new()
	state.history.mode = state.spec.mode
	state.history.record(0, 0.0, false)
	state.history.record(1, 1.0, false)
	state.history.record(2, 2.0, false)
	state.self_feedback = self_feedback
	var writer := NetwInterpRecordingWriter.new()
	state.output = writer.write
	state.last_written = 0.0

	rt.states.append(state)
	return { rt = rt, writer = writer }


func _timing() -> NetwDisplayTiming:
	var t := NetwDisplayTiming.new()
	t.tick = 2
	t.display_tick = 2
	t.tick_factor = 0.0
	t.ticktime = 1.0 / 60.0
	t.frame_delta = 1.0 / 60.0
	t.frame_ticks = 1.0
	return t


func test_chase_skips_a_self_feedback_channel() -> void:
	var core := NetwMultiplayerCore.new()
	var scene := _chase_runtime(true)
	core.display_pump_runtime(scene.rt, _timing(), NetwPumpStats.new())
	assert_array((scene.writer as NetwInterpRecordingWriter).samples) \
			.override_failure_message("a self-feedback CHASE channel must not write") \
			.is_empty()


func test_chase_writes_a_redirected_channel() -> void:
	var core := NetwMultiplayerCore.new()
	var scene := _chase_runtime(false)
	core.display_pump_runtime(scene.rt, _timing(), NetwPumpStats.new())
	assert_array((scene.writer as NetwInterpRecordingWriter).samples) \
			.override_failure_message("a redirected CHASE channel must still write") \
			.is_not_empty()


func test_bracketed_skips_a_self_feedback_channel() -> void:
	var core := NetwMultiplayerCore.new()
	var scene := _history_runtime(true, NetwDisplayDecl.PUMP_BRACKETED)
	core.display_pump_runtime(scene.rt, _timing(), NetwPumpStats.new())
	assert_array((scene.writer as NetwInterpRecordingWriter).samples) \
			.override_failure_message("a self-feedback BRACKETED (authority) channel must not write") \
			.is_empty()


func test_bracketed_writes_a_redirected_channel() -> void:
	var core := NetwMultiplayerCore.new()
	var scene := _history_runtime(false, NetwDisplayDecl.PUMP_BRACKETED)
	core.display_pump_runtime(scene.rt, _timing(), NetwPumpStats.new())
	assert_array((scene.writer as NetwInterpRecordingWriter).samples) \
			.override_failure_message("a redirected BRACKETED channel must still write") \
			.is_not_empty()


# The frozen REMOTE display legitimately writes the sampled property: there is no
# live simulation to corrupt, so self-feedback is harmless and must not be skipped.
func test_remote_writes_even_a_self_feedback_channel() -> void:
	var core := NetwMultiplayerCore.new()
	var scene := _history_runtime(true, NetwDisplayDecl.PUMP_REMOTE)
	core.display_pump_runtime(scene.rt, _timing(), NetwPumpStats.new())
	assert_array((scene.writer as NetwInterpRecordingWriter).samples) \
			.override_failure_message("a REMOTE display writes the sampled property, frozen body") \
			.is_not_empty()
