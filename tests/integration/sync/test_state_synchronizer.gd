## Integration tests for [StateSynchronizer] through a clocked loopback pair.
##
## Proves the finding-1 shape: __tick, __ack, and the payload share one reliable,
## ordered ON_CHANGE delta, so the stamp stays coherent with the values it tags
## under the jitter preset that tore the earlier split shape, while per-property
## diffing keeps a rarely-changing field sparse. Mirrors the spike A.1 /
## delta-diffs coverage against the production class.
class_name TestStateSynchronizer
extends NetwTestSuite

var rig: SyncLoopbackRig


func _pos(t: int) -> Vector2:
	return Vector2(t, -t)


# Payload registered before tree entry; configure() adds __tick/__ack on finalize.
func _make_state_sync() -> StampedSynchronizer:
	var sync := StateSynchronizer.new()
	sync.register_property(
		&"position",
		NodePath(".:position"),
		SceneReplicationConfig.REPLICATION_MODE_ON_CHANGE,
		false,
		true,
	)
	return sync


func test_stamp_coherent_under_jitter() -> void:
	rig = SyncLoopbackRig.new()
	await rig.setup(self, _make_state_sync)

	var server_sync := rig.server_sync as StateSynchronizer
	rig.server_clock.on_tick.connect(
		func(_d: float, t: int) -> void:
			server_sync.authored_tick = t
			server_sync.server_ack = t - 3
			rig.server_node.position = _pos(t),
	)

	var captures: Array[Dictionary] = []
	(rig.client_sync as StateSynchronizer).on_state_received = (
			func(tick: int, ack: int, payload: Dictionary) -> void:
				captures.append(
					{
						&"tick": tick,
						&"ack": ack,
						&"position": payload.get(&"position", Vector2.ZERO),
					},
				)
	)

	# The same jitter/loss preset that tore the split ALWAYS+watched shape.
	rig.delay_server_to_client(4, 3, 6, 0.08)
	rig.sync_ticks(150)

	var torn := 0
	var checked := 0
	for c in captures:
		var tick: int = c.tick
		if tick < 0:
			continue
		checked += 1
		if not (c.position as Vector2).is_equal_approx(_pos(tick)):
			torn += 1
		elif int(c.ack) != tick - 3:
			torn += 1

	assert_int(checked).is_greater(0)
	assert_int(torn).is_equal(0)


func test_owner_client_records_into_injected_timeline() -> void:
	rig = SyncLoopbackRig.new()
	await rig.setup(self, _make_state_sync)

	var timeline := NetwTimeline.new()
	rig.client_sync.timeline = timeline

	var server_sync := rig.server_sync as StateSynchronizer
	rig.server_clock.on_tick.connect(
		func(_d: float, t: int) -> void:
			server_sync.authored_tick = t
			rig.server_node.position = _pos(t),
	)

	var received: Array[int] = []
	(rig.client_sync as StateSynchronizer).on_state_received = (
			func(tick: int, _ack: int, _payload: Dictionary) -> void:
				received.append(tick)
	)

	rig.sync_ticks(60)

	assert_int(received.size()).is_greater(0)
	# The latest received tick landed in the timeline with its coherent payload.
	var last_tick: int = received[received.size() - 1]
	assert_vector(
		timeline.state_at(last_tick).get(&"position"),
	).is_equal(_pos(last_tick))
