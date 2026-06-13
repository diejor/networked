## Tier A.1 spike: __tick/payload stamp coherence under impairment.
##
## The load-bearing transport assumption (architecture risk 9.1): a state
## packet's payload is the value authored at the same tick as its stamp, even
## across a reordering, jittering, lossy link.
##
## Finding: the SPLIT shape (ALWAYS stamps + watched payload) stays coherent
## only while the link preserves order. Under jitter the ALWAYS stamp and the
## watched payload are independent packets, independently delayed, and tear. The
## BUNDLED shape (one atomic __state dictionary) holds under all regimes. The
## verdict for the design: bundle the snapshot into one virtual property.
class_name TestSpikeStampCoherence
extends NetwTestSuite

var h: SpikeNetHarness


func _start(bundled: bool) -> void:
	h = SpikeNetHarness.new()
	await h.setup(self, 30, 3, bundled)
	h.server_clock.on_tick.connect(_drive_server)


# Stamp and payload set together: any torn capture is a transport fault.
func _drive_server(_delta: float, tick: int) -> void:
	h.server_sync.authored_tick = tick
	h.server_sync.server_ack = tick - 3
	h.server_node.position = _authored_pos(tick)


func test_a1_split_coherent_under_perfect_link() -> void:
	await _start(false)
	h.sync_ticks(60)
	assert_that(_torn_count()).is_equal(0)
	assert_that(_checked_count()).is_greater(0)


func test_a1_split_coherent_under_exact_delay() -> void:
	await _start(false)
	h.delay_server_to_client(4)
	h.sync_ticks(90)
	assert_that(_torn_count()).is_equal(0)
	assert_that(_checked_count()).is_greater(0)


func test_a1_split_tears_under_jitter() -> void:
	# Records the failure mode: jitter independently delays the ALWAYS stamp and
	# the watched payload, so they arrive misaligned. This is why the design
	# must NOT pair a watched payload with a separate stamp.
	await _start(false)
	h.delay_server_to_client(4, 3, 6, 0.08)
	h.sync_ticks(150)
	assert_that(_torn_count()).is_greater(0)


func test_a1_bundled_coherent_under_jitter_and_loss() -> void:
	await _start(true)
	h.delay_server_to_client(4, 3, 6, 0.08)
	h.sync_ticks(150)
	assert_that(_torn_count()).is_equal(0)
	assert_that(_checked_count()).is_greater(0)


func test_a1_bundled_coherent_under_satellite() -> void:
	await _start(true)
	h.delay_server_to_client(12, 4, 8, 0.04)
	h.sync_ticks(140)
	assert_that(_torn_count()).is_equal(0)
	assert_that(_checked_count()).is_greater(0)


func _authored_pos(tick: int) -> Vector2:
	return Vector2(tick, -tick)


# A capture is torn when its payload is not the value authored at its tick.
func _torn_count() -> int:
	var torn := 0
	for c: Dictionary in h.client_sync.captures:
		var tick: int = c["tick"]
		if tick < 0:
			continue
		if not (c["position"] as Vector2).is_equal_approx(_authored_pos(tick)):
			torn += 1
		elif int(c["ack"]) != tick - 3:
			torn += 1
	return torn


func _checked_count() -> int:
	var n := 0
	for c: Dictionary in h.client_sync.captures:
		if int(c["tick"]) >= 0:
			n += 1
	return n
