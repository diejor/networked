## Integration tests for [InputSynchronizer] through a clocked loopback pair.
##
## Proves the input direction: the controlling client stamps and sends on the
## volatile ALWAYS sync lane, and the server records each received (tick, input)
## into the entity timeline. Because the stamp and payload ride one atomic sync
## packet, jitter and loss drop whole samples but never tear a tick from its
## input. The virtual name "motion" is backed by the node position so the rig
## needs no input-source node.
class_name TestInputSynchronizer
extends NetwTestSuite

var rig: SyncLoopbackRig


func _motion(t: int) -> Vector2:
	return Vector2(t, -t)


func _make_input_sync() -> StampedSynchronizer:
	var sync := InputSynchronizer.new()
	sync.register_property(
		&"motion",
		NodePath(".:position"),
		SceneReplicationConfig.REPLICATION_MODE_ALWAYS,
		false,
		false,
	)
	return sync


# Authority is the client (the controller); the server receives and records.
func _pin_authority_to_client() -> void:
	var cpid := rig.client.multiplayer_peer.get_unique_id()
	rig.server_sync.set_multiplayer_authority(cpid)
	rig.client_sync.set_multiplayer_authority(cpid)


func test_server_records_received_input_coherently() -> void:
	rig = SyncLoopbackRig.new()
	await rig.setup(self, _make_input_sync)
	_pin_authority_to_client()

	var timeline := NetwTimeline.new()
	rig.server_sync.timeline = timeline

	var client_sync := rig.client_sync as InputSynchronizer
	rig.client_clock.on_tick.connect(
		func(_d: float, t: int) -> void:
			client_sync.authored_tick = t
			rig.client_node.position = _motion(t),
	)

	var captures: Array[Dictionary] = []
	(rig.server_sync as InputSynchronizer).on_input_received = (
			func(tick: int, payload: Dictionary) -> void:
				captures.append(
					{
						&"tick": tick,
						&"motion": payload.get(&"motion", Vector2.ZERO),
					},
				)
	)

	rig.delay_client_to_server(4, 2, 6, 0.08)
	rig.sync_ticks(150)

	# ALWAYS drops whole samples under loss, so we assert on what arrived: every
	# received packet pairs the right motion with its tick (no tearing).
	var torn := 0
	var checked := 0
	for c in captures:
		var tick: int = c.tick
		if tick < 0:
			continue
		checked += 1
		if not (c.motion as Vector2).is_equal_approx(_motion(tick)):
			torn += 1

	assert_int(checked).is_greater(0)
	assert_int(torn).is_equal(0)

	# A recent received tick is recorded into the timeline with its coherent
	# value (the oldest would have been evicted from the 64-tick ring).
	var sample_tick: int = captures[captures.size() - 1].tick
	assert_vector(
		timeline.input_at(sample_tick).get(&"motion"),
	).is_equal(_motion(sample_tick))


func test_server_only_audience_limits_input_visibility() -> void:
	var sync := InputSynchronizer.new()
	add_child(sync)
	auto_free(sync)

	assert_bool(sync.public_visibility).is_false()
	assert_bool(sync.get_visibility_for(MultiplayerPeer.TARGET_PEER_SERVER)) \
			.is_true()
	assert_bool(sync.get_visibility_for(42)).is_false()


func test_public_audience_keeps_default_input_visibility() -> void:
	var sync := InputSynchronizer.new()
	sync.audience = InputSynchronizer.Audience.PUBLIC
	add_child(sync)
	auto_free(sync)

	assert_bool(sync.public_visibility).is_true()
