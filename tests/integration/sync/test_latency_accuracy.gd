## Deterministic, reproducible latency under lockstep (ports the lag-comp spike
## latency tier onto the production [StateSynchronizer]).
##
## The loopback delay clock advances in milliseconds, one tick period per tick,
## driven by [LockstepStepper] rather than real frames. Server and client clocks
## are stepped together, so the [MultiplayerClock] tick is a shared, exact clock
## with no calibration noise. Latency is measured in ticks as
## [code]recv_tick - authored_tick[/code] off the real state stream: an exact
## delay produces a flat, reproducible in-flight tick count. This proves the
## transport and lockstep determinism, not lag-comp logic.
class_name TestLatencyAccuracy
extends NetwTestSuite

const TICKRATE := 30
const DISPLAY_OFFSET := 3


func _pos(t: int) -> Vector2:
	return Vector2(t, -t)


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


# Builds an unmanaged rig, drives the server state stream, and returns the
# per-packet in-flight tick counts measured after a warmup window.
func _latencies(delay_polls: int, warmup: int, measure: int) -> Array:
	var rig := SyncLoopbackRig.new()
	await rig.setup(self, _make_state_sync, TICKRATE, DISPLAY_OFFSET, false)

	var server_sync := rig.server_sync as StateSynchronizer
	rig.server_clock.on_tick.connect(
		func(_d: float, t: int) -> void:
			server_sync.authored_tick = t
			rig.server_node.position = _pos(t),
	)

	# Capture each received packet's authoring tick paired with the client tick it
	# landed on, so recv - authored is the in-flight tick count.
	var captures: Array[Dictionary] = []
	(rig.client_sync as StateSynchronizer).on_state_received = (
		func(tick: int, _ack: int, _payload: Dictionary) -> void:
			captures.append({ &"tick": tick, &"recv_tick": rig.client_clock.tick })
	)

	rig.delay_server_to_client(delay_polls)
	rig.sync_ticks(warmup)
	var base: int = captures.size()
	rig.sync_ticks(measure)

	var lat: Array = []
	for i in range(base, captures.size()):
		var sent: int = captures[i].tick
		var recv: int = captures[i].recv_tick
		if sent < 0 or recv < 0:
			continue
		lat.append(recv - sent)
	await rig.teardown()
	return lat


func _mean(a: Array) -> float:
	if a.is_empty():
		return 0.0
	var s := 0
	for v: int in a:
		s += v
	return float(s) / float(a.size())


func _spread(a: Array) -> int:
	if a.is_empty():
		return 0
	var lo: int = a[0]
	var hi: int = a[0]
	for v: int in a:
		lo = mini(lo, v)
		hi = maxi(hi, v)
	return hi - lo


func test_latency_is_stable_under_exact_delay() -> void:
	# Exact delay, no jitter: every packet experiences the same in-flight delay,
	# so the spread collapses to zero within a run. The absolute offset can shift
	# a tick between runs because setup still syncs the clocks over real frames.
	var lat := await _latencies(4, 30, 60)
	assert_that(lat.size()).is_greater(10)
	assert_that(_spread(lat)).is_less_equal(1)
	assert_that(_mean(lat)).is_between(3.0, 5.0)


func test_latency_is_reproducible_across_runs() -> void:
	var a := await _latencies(4, 30, 60)
	var b := await _latencies(4, 30, 60)
	assert_that(absf(_mean(a) - _mean(b))).is_less_equal(1.0)


func test_latency_scales_with_configured_delay() -> void:
	# A configured delay maps predictably to in-flight ticks: +8 polls is +4 tick
	# periods, the property fine tuning depends on.
	var small := await _latencies(4, 30, 50)
	var big := await _latencies(12, 30, 50)
	var delta := _mean(big) - _mean(small)
	# small and big are independent setups, so each carries its own one-tick phase
	# offset, widening the band around +4.
	assert_that(delta).is_between(2.5, 5.5)
