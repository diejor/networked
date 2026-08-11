## The two-process regime tier: real sockets, real wall clocks, scripted laps.
##
## Every arm here spawns two headless racing processes that speak ENet over
## the loopback, drives both with scripted gestures, and reads the
## summary.json each peer leaves behind. This is the rendered regime the
## in-process suites cannot reach, reproduced on demand: independent physics
## pumps, real arrival jitter, and a deliberately starved main loop.
##
## Every arm asserts its condition block first, because a rig that cannot
## prove it reached the regime reports the absence of a defect rather than
## its absence. The consume-side numbers each arm prints are the band-setting
## record for the repairs this rig exists to judge.
##
## The tier is opt-in: it costs minutes of wall clock and two processes per
## arm, so the default sweep skips it. Arm it with NETW_REGIME=1, and shorten
## a smoke run with NETW_REGIME_SECONDS.
class_name TestRacingRegime
extends NetwTestSuite

const SCENE := "res://examples/racing/main.tscn"

# Wall seconds the host runs before the client is spawned, covering the
# host's own boot and listen. The client's ENet join retries inside its own
# timeout, so this only needs to beat process startup.
const STARTUP_LEAD := 6.0

# Wall headroom beyond both horizons for connect, spawn, quiesce, and the
# children's own watchdogs. An arm that outlives this leaks no processes:
# await_exit kills stragglers.
const EXIT_MARGIN := 45.0


@warning_ignore("unused_parameter")
func before(
		do_skip = OS.get_environment("NETW_REGIME").is_empty(),
		skip_reason = "Two-process regime tier. Set NETW_REGIME=1 to run it.",
) -> void:
	pass


func test_healthy_lane_reaches_the_consume_regime() -> void:
	var run := await _run_arm(
		"healthy",
		{ "regime-gesture": "laps" },
		{ "regime-gesture": "laps" },
	)
	var host: Dictionary = run[&"host"]
	var client: Dictionary = run[&"client"]

	_require_complete(host, "host")
	_require_complete(client, "client")
	_require_full_rate(host, "host")
	_require_full_rate(client, "client")

	var authority := _authority_car(host)
	assert_int(_stat(authority, "consumed")).override_failure_message(
		"authority consumed almost nothing, so the arm never reached the "
		+ "consume regime and its counters describe no run",
	).is_greater(int((_seconds() - STARTUP_LEAD) * 40.0))
	var arrivals := _arrival_mean(authority)
	assert_float(arrivals).override_failure_message(
		(
				"the lane delivered %.3f transitions per authority frame "
				+ "against the ~1:1 a healthy loopback carries"
		) % arrivals,
	).is_between(0.8, 1.2)
	assert_int(_stat(_local_car(client), "quantum_faults")) \
			.override_failure_message(
				"a gated owner at full rate must spend no physics on frames "
				+ "no transition claims",
			).is_less_equal(5)
	_require_contact_free(client, "healthy")

	_print_consume_record("healthy", authority, client)


func test_a_starved_host_still_delivers_the_lane() -> void:
	var starved_seconds := maxf(4.0, _seconds() - 8.0)
	var run := await _run_arm(
		"starved-host",
		{
			"regime-gesture": "laps",
			"regime-throttle": "0:4,6:%s,0:4" % starved_seconds,
		},
		{ "regime-gesture": "laps" },
	)
	var host: Dictionary = run[&"host"]
	var client: Dictionary = run[&"client"]

	_require_complete(host, "host")
	_require_complete(client, "client")
	_require_full_rate(client, "client")

	var starved_phase := _throttle_phase(host, 6.0)
	assert_dict(starved_phase).override_failure_message(
		"the host never entered its starved phase, so this arm measured a "
		+ "healthy run wearing the starved arm's name",
	).is_not_empty()
	var measured_fps := float(starved_phase.get("measured_fps", 0.0))
	assert_float(measured_fps).override_failure_message(
		(
				"the throttle asked for 6 fps and the main loop ran %.1f, "
				+ "outside the band where physics catch-up degrades"
		) % measured_fps,
	).is_between(3.0, 9.5)
	var physics_p10 := float(
		(host.get("condition", { }) as Dictionary).get("physics_hz_p10", 0.0),
	)
	assert_float(physics_p10).override_failure_message(
		(
				"a 6 fps main loop caps physics catch-up near 48 Hz, but the "
				+ "slowest sampled second ran %.1f Hz, so the starvation "
				+ "never reached the physics pump"
		) % physics_p10,
	).is_less(55.0)

	var authority := _authority_car(host)
	assert_int(_stat(authority, "consumed")).override_failure_message(
		"a starved authority still consumes on every frame its world steps",
	).is_greater(0)
	_print_consume_record("starved-host", authority, client)


func test_the_symptom_gesture_drives_and_settles() -> void:
	var run := await _run_arm(
		"symptom",
		{ "regime-gesture": "hold" },
		{ "regime-gesture": "symptom" },
	)
	var host: Dictionary = run[&"host"]
	var client: Dictionary = run[&"client"]

	_require_complete(host, "host")
	_require_complete(client, "client")
	var travel := float(
		(client.get("condition", { }) as Dictionary).get("gesture_travel", 0.0),
	)
	assert_float(travel).override_failure_message(
		(
				"the symptom gesture moved the car %.2f m, so the arm "
				+ "recorded an idle session wearing the symptom's name"
		) % travel,
	).is_greater(5.0)
	_require_contact_free(client, "symptom")

	_print_consume_record("symptom", _authority_car(host), client)

# --- the arm runner ---


# Spawns host then client with a shared port and per-peer artifact dirs,
# waits both out, and returns their parsed summaries. Stale summaries are
# deleted first, so a wedged child can never pass on a previous run's file.
func _run_arm(
		arm: String,
		host_args: Dictionary,
		client_args: Dictionary,
) -> Dictionary:
	var port := 40000 + int(Time.get_ticks_usec() % 9000)
	var host_dir := _arm_dir(arm, "host")
	var client_dir := _arm_dir(arm, "client")
	DirAccess.remove_absolute(host_dir.path_join("summary.json"))
	DirAccess.remove_absolute(client_dir.path_join("summary.json"))

	var rig := NetwProcessRig.new()
	# The host outlives the client by construction: the client boots a lead
	# later and the host carries extra horizon, so the session is still alive
	# for the client's whole gesture window. A host that quit first would end
	# the client's session mid-window, freezing its clock and its entities.
	var host_run := { "regime-seconds": str(_seconds() + STARTUP_LEAD + 4.0) }
	host_run.merge(host_args, true)
	var host_pid := rig.spawn(
		SCENE,
		_peer_args(
			"host",
			port,
			host_dir,
			host_run,
		),
	)
	assert_int(host_pid).override_failure_message(
		"the host process failed to spawn",
	).is_greater(0)
	await get_tree().create_timer(STARTUP_LEAD).timeout
	var client_pid := rig.spawn(
		SCENE,
		_peer_args(
			"client",
			port,
			client_dir,
			client_args,
		),
	)
	assert_int(client_pid).override_failure_message(
		"the client process failed to spawn",
	).is_greater(0)

	var budget := STARTUP_LEAD * 2.0 + _seconds() + 4.0 + EXIT_MARGIN
	var exited := await rig.await_exit(self, budget)
	assert_bool(exited).override_failure_message(
		"the arm's children outlived their %.0fs budget and were killed" \
				% budget,
	).is_true()

	return {
		&"host": NetwProcessRig.read_summary(host_dir),
		&"client": NetwProcessRig.read_summary(client_dir),
	}


func _peer_args(
		role: String,
		port: int,
		dir: String,
		extra: Dictionary,
) -> Dictionary:
	var args := {
		"regime-role": role,
		"regime-port": str(port),
		"regime-seconds": str(_seconds()),
		"regime-dir": dir,
	}
	args.merge(extra, true)
	return args


func _seconds() -> float:
	var override := OS.get_environment("NETW_REGIME_SECONDS").to_float()
	return override if override > 0.0 else 30.0


func _arm_dir(arm: String, role: String) -> String:
	return ProjectSettings.globalize_path(
		"res://tmp/regime/%s/%s" % [arm, role],
	)

# --- reading a summary ---


# A summary that is empty or ended on the watchdog fails here, before any
# number from it is trusted.
func _require_complete(summary: Dictionary, peer: String) -> void:
	assert_dict(summary).override_failure_message(
		"the %s left no summary.json, so its run cannot be judged" % peer,
	).is_not_empty()
	assert_str(String(summary.get("exit", ""))).override_failure_message(
		"the %s did not complete its run" % peer,
	).is_equal("complete")


# A contact-free arm's divergence numbers are only readable if the gesture
# actually stayed off the walls and the other car: contact opens out-of-domain
# windows whose corrections come from a different generator entirely. The
# first symptom gesture measured 72% wall contact, which is why this is an
# assertion and not an assumption.
func _require_contact_free(client_summary: Dictionary, arm: String) -> void:
	var condition: Dictionary = client_summary.get("condition", { })
	var wall := float(condition.get("wall_contact_fraction", 0.0))
	var car := float(condition.get("car_contact_fraction", 0.0))
	assert_float(wall + car).override_failure_message(
		(
				"the %s gesture spent %.1f%% of its frames in contact, so "
				+ "its divergence numbers measure contact recovery rather "
				+ "than the consume regime"
		) % [arm, 100.0 * (wall + car)],
	).is_less(0.10)


# Headless and unthrottled, both pumps hold the physics rate. A peer outside
# this band is a struggling machine, which is a condition failure rather than
# an engine finding.
func _require_full_rate(summary: Dictionary, peer: String) -> void:
	var condition: Dictionary = summary.get("condition", { })
	var physics_hz := float(condition.get("physics_hz_mean", 0.0))
	assert_float(physics_hz).override_failure_message(
		(
				"the %s ran physics at %.1f Hz, outside the healthy band, "
				+ "so this machine cannot hold the arm's regime"
		) % [peer, physics_hz],
	).is_between(50.0, 70.0)


# The consume-side car on the host: the entity that consumed the most, which
# is the client's car. The host's own car is HOST_LOCAL and consumes nothing.
func _authority_car(host_summary: Dictionary) -> Dictionary:
	return _car_by_stat(host_summary, "consumed")


# The owner-side car on the client: the entity that sent the command lane.
func _local_car(client_summary: Dictionary) -> Dictionary:
	return _car_by_stat(client_summary, "command_frames_sent")


func _car_by_stat(summary: Dictionary, key: String) -> Dictionary:
	var best := { }
	var best_value := -1
	var entities: Dictionary = summary.get("entities", { })
	for id: Variant in entities:
		var entity: Dictionary = entities[id]
		var value := _stat(entity, key)
		if value > best_value:
			best_value = value
			best = entity
	return best


func _stat(entity: Dictionary, key: String) -> int:
	return int((entity.get("stats", { }) as Dictionary).get(key, 0))


# Mean owner-lane transitions delivered per authority frame, off the arrival
# histogram: total arrivals over frames observed.
func _arrival_mean(entity: Dictionary) -> float:
	var histogram: Dictionary = \
			(entity.get("stats", { }) as Dictionary).get("arrivals", { })
	var frames := 0
	var arrivals := 0
	for bucket: Variant in histogram:
		var count := int(histogram[bucket])
		frames += count
		arrivals += int(String(bucket).to_int()) * count
	return float(arrivals) / frames if frames > 0 else 0.0


func _throttle_phase(summary: Dictionary, target_fps: float) -> Dictionary:
	var condition: Dictionary = summary.get("condition", { })
	for phase: Variant in condition.get("throttle", []):
		if is_equal_approx(float(phase.get("target_fps", -1.0)), target_fps):
			return phase
	return { }


# The band-setting record: what the consume side did, printed in the shape
# the campaign's captures use, so a repair is judged against numbers this rig
# itself produced.
func _print_consume_record(
		arm: String,
		authority: Dictionary,
		client_summary: Dictionary,
) -> void:
	var stats: Dictionary = authority.get("stats", { })
	print(
		(
				"[regime] %-12s authority: consumed=%-5d held=%-4d "
				+ "starved=%-4d faults=%-4d corrections=%-4d"
		) % [
			arm,
			int(stats.get("consumed", 0)),
			int(stats.get("held", 0)),
			int(stats.get("starved", 0)),
			int(stats.get("quantum_faults", 0)),
			int(stats.get("corrections", 0)),
		],
	)
	print("[regime]   arrivals: %s" % _sorted_histogram(stats.get("arrivals", { })))
	print("[regime]   depth:    %s" % _sorted_histogram(stats.get("replay_depth", { })))
	print("[regime]   shape:    %s" % _sorted_histogram(stats.get("consume_shape", { })))
	var local := _local_car(client_summary)
	var local_stats: Dictionary = local.get("stats", { })
	print(
		"[regime]   owner: sent=%d clamped=%d faults=%d corrections=%d" % [
			int(local_stats.get("command_frames_sent", 0)),
			int(local_stats.get("authoring_clamped", 0)),
			int(local_stats.get("quantum_faults", 0)),
			int(local_stats.get("corrections", 0)),
		],
	)
	var condition: Dictionary = client_summary.get("condition", { })
	print(
		(
				"[regime]   gesture: travel=%.1fm wall_contact=%.1f%% "
				+ "car_contact=%.1f%%"
		) % [
			float(condition.get("gesture_travel", 0.0)),
			100.0 * float(condition.get("wall_contact_fraction", 0.0)),
			100.0 * float(condition.get("car_contact_fraction", 0.0)),
		],
	)


func _sorted_histogram(histogram: Variant) -> String:
	if not histogram is Dictionary:
		return "(none)"
	var keys: Array = (histogram as Dictionary).keys()
	keys.sort()
	var parts := PackedStringArray()
	for key: Variant in keys:
		parts.append("%s x%d" % [key, int(histogram[key])])
	return ", ".join(parts) if not parts.is_empty() else "(none)"
