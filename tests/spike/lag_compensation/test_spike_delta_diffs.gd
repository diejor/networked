## Spike: per-property diffs WITHOUT bundling, via the ON_CHANGE delta stream.
##
## The Godot source (scene_replication_interface.cpp) shows ALWAYS props go on an
## unreliable sync packet while ON_CHANGE props go on a reliable, ordered delta
## packet carrying an indexes bitmask of only the changed props. A.1 tore because
## the stamp was ALWAYS (unreliable) and the payload was ON_CHANGE (reliable):
## two different packets. Putting the stamp on the SAME delta stream as the
## payload keeps them coherent while preserving per-property diffing. This proves
## bundling is not required.
class_name TestSpikeDeltaDiffs
extends NetwTestSuite

var h: SpikeNetHarness


func _start() -> void:
	h = SpikeNetHarness.new()
	# delta=true: position, __tick, __ack, blink are all ON_CHANGE.
	await h.setup(self, 30, 3, false, true, true)
	h.server_clock.on_tick.connect(_drive)


func _drive(_d: float, t: int) -> void:
	h.server_sync.authored_tick = t
	h.server_sync.server_ack = t - 3
	h.server_node.position = _pos(t)
	if t % 10 == 0:
		h.server_sync.blink_value = t


func _pos(t: int) -> Vector2:
	return Vector2(t, -t)


func test_delta_stream_coherent_under_jitter() -> void:
	# The same jitter/loss preset that tore the split ALWAYS+watched shape in A.1.
	await _start()
	h.delay_server_to_client(4, 3, 6, 0.08)
	h.sync_ticks(150)

	var torn := 0
	var checked := 0
	for c: Dictionary in h.client_sync.captures:
		var tick: int = c["tick"]
		if tick < 0:
			continue
		checked += 1
		if not (c["position"] as Vector2).is_equal_approx(_pos(tick)):
			torn += 1
		elif int(c["ack"]) != tick - 3:
			torn += 1
	assert_that(checked).is_greater(0)
	assert_that(torn).is_equal(0)


func test_per_property_diffing_keeps_rare_field_sparse() -> void:
	# position changes every tick; blink only every 10. Per-property diffing means
	# blink is delivered far less often than position, unlike a bundle that would
	# resend everything whenever position moves.
	await _start()
	h.sync_ticks(120)
	var blink: int = h.client_sync.blink_recv_count
	var pos: int = h.client_sync.position_recv_count
	assert_that(pos).is_greater(40)
	assert_that(blink).is_greater(0)
	assert_that(blink * 4).is_less(pos)
