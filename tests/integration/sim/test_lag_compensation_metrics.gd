## Verifies [method LagCompensation.metrics] exposes the occupancy keys the
## [LagCompensationMonitor] reads, alongside the long-standing counters.
class_name TestLagCompensationMetrics
extends NetwTestSuite

func test_metrics_exposes_monitor_keys() -> void:
	var r := RewindScenario.new()
	await r.setup(self)

	var metrics := r.sim.metrics()
	for key in [
		&"entities",
		&"timelines",
		&"corrections",
		&"max_replay_depth",
		&"consumed",
		&"missing",
		&"pending_actions",
		&"effects_armed",
		&"gate_fallbacks",
	]:
		assert_that(metrics.has(key)).override_failure_message(
			"metrics() missing key '%s'" % key,
		).is_true()


func test_timelines_tracks_recorded_entities() -> void:
	var r := RewindScenario.new()
	await r.setup(self)
	assert_that(int(r.sim.metrics()[&"timelines"])).is_equal(0)

	await r.spawn_state_entity("Target")
	r.run(2)

	assert_that(int(r.sim.metrics()[&"timelines"])).is_equal(1)
