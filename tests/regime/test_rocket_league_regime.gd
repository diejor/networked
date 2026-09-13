class_name TestRocketLeagueRegime
extends NetwTestSuite

const SCENE := "res://tests/regime/rocket_league_regime_main.tscn"
const STARTUP_LEAD := 6.0
const EXIT_MARGIN := 45.0


@warning_ignore("unused_parameter")
func before(
		do_skip = OS.get_environment("NETW_REGIME").is_empty(),
		skip_reason = "Two-process regime tier. Set NETW_REGIME=1 to run it.",
) -> void:
	pass


func test_healthy_lane_reaches_the_consume_regime() -> void:
	var run := await run_arm(
		"healthy",
		{ "regime-gesture": "chase" },
		{ "regime-gesture": "chase" },
	)
	var host: Dictionary = run[&"host"]
	var client: Dictionary = run[&"client"]

	require_complete(host, "host")
	require_complete(client, "client")
	require_full_rate(host, "host")
	require_full_rate(client, "client")
	require_play(host, client, "healthy")

	var authority := authority_car(host)
	assert_int(stat(authority, "consumed")).override_failure_message(
		"authority consumed almost nothing, so the arm never reached the "
		+ "consume regime and its counters describe no run",
	).is_greater(int((seconds() - STARTUP_LEAD) * 40.0))

	var arrivals := arrival_mean(authority)
	assert_float(arrivals).override_failure_message(
		(
				"the lane delivered %.3f transitions per authority frame "
				+ "against the ~1:1 a healthy loopback carries"
		) % arrivals,
	).is_between(0.8, 1.2)

	print_consume_record("healthy", authority, host, client)


func test_a_starved_host_still_delivers_the_lane() -> void:
	var starved_seconds := maxf(4.0, seconds() - 8.0)
	var run := await run_arm(
		"starved-host",
		{
			"regime-gesture": "chase",
			"regime-throttle": "0:4,6:%s,0:4" % starved_seconds,
		},
		{ "regime-gesture": "chase" },
	)
	var host: Dictionary = run[&"host"]
	var client: Dictionary = run[&"client"]

	require_complete(host, "host")
	require_complete(client, "client")
	require_full_rate(client, "client")

	var starved_phase := throttle_phase(host, 6.0)
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

	var authority := authority_car(host)
	assert_int(stat(authority, "consumed")).override_failure_message(
		"a starved authority still consumes on every frame its world steps",
	).is_greater(0)

	print_consume_record("starved-host", authority, host, client)


func run_arm(
		arm: String,
		host_args: Dictionary,
		client_args: Dictionary,
) -> Dictionary:
	var port := 40000 + int(Time.get_ticks_usec() % 9000)
	var host_dir := arm_dir(arm, "host")
	var client_dir := arm_dir(arm, "client")
	DirAccess.remove_absolute(host_dir.path_join("summary.json"))
	DirAccess.remove_absolute(client_dir.path_join("summary.json"))

	var rig := NetwProcessRig.new()
	var host_run := { "regime-seconds": str(seconds() + STARTUP_LEAD + 4.0) }
	host_run.merge(host_args, true)
	var host_pid := rig.spawn(SCENE, peer_args("host", port, host_dir, host_run))
	assert_int(host_pid).override_failure_message(
		"the host process failed to spawn",
	).is_greater(0)

	await get_tree().create_timer(STARTUP_LEAD).timeout
	var client_pid := rig.spawn(
		SCENE,
		peer_args("client", port, client_dir, client_args),
	)
	assert_int(client_pid).override_failure_message(
		"the client process failed to spawn",
	).is_greater(0)

	var budget := STARTUP_LEAD * 2.0 + seconds() + 4.0 + EXIT_MARGIN
	var exited := await rig.await_exit(self, budget)
	assert_bool(exited).override_failure_message(
		"the arm's children outlived their %.0fs budget and were killed" \
				% budget,
	).is_true()

	return {
		&"host": NetwProcessRig.read_summary(host_dir),
		&"client": NetwProcessRig.read_summary(client_dir),
	}


func peer_args(
		role: String,
		port: int,
		dir: String,
		extra: Dictionary,
) -> Dictionary:
	var args := {
		"regime-role": role,
		"regime-port": str(port),
		"regime-seconds": str(seconds()),
		"regime-dir": dir,
	}
	args.merge(extra, true)
	return args


func seconds() -> float:
	var override := OS.get_environment("NETW_REGIME_SECONDS").to_float()
	return override if override > 0.0 else 30.0


func arm_dir(arm: String, role: String) -> String:
	return ProjectSettings.globalize_path(
		"res://tmp/regime/%s/%s" % [arm, role],
	)


func require_complete(summary: Dictionary, peer: String) -> void:
	assert_dict(summary).override_failure_message(
		"the %s left no summary.json, so its run cannot be judged" % peer,
	).is_not_empty()
	assert_str(String(summary.get("exit", ""))).override_failure_message(
		"the %s did not complete its run" % peer,
	).is_equal("complete")


func require_full_rate(summary: Dictionary, peer: String) -> void:
	var condition: Dictionary = summary.get("condition", { })
	var physics_hz := float(condition.get("physics_hz_mean", 0.0))
	assert_float(physics_hz).override_failure_message(
		(
				"the %s ran physics at %.1f Hz, outside the healthy band, "
				+ "so this machine cannot hold the arm's regime"
		) % [peer, physics_hz],
	).is_between(50.0, 70.0)


func require_play(
		host_summary: Dictionary,
		client_summary: Dictionary,
		arm: String,
) -> void:
	var car_travel := float(
		(client_summary.get("condition", { }) as Dictionary).get(
			"gesture_travel",
			0.0,
		),
	)
	var ball_travel := float(
		(host_summary.get("condition", { }) as Dictionary).get(
			"ball_travel",
			0.0,
		),
	)
	assert_float(car_travel).override_failure_message(
		(
				"the %s gesture drove the car %.2f m, so the arm recorded an "
				+ "idle session wearing the gesture's name"
		) % [arm, car_travel],
	).is_greater(5.0)
	assert_float(ball_travel).override_failure_message(
		(
				"the %s arm moved the ball %.2f m, so the cars never reached "
				+ "it and the contact this tier exists to carry never happened"
		) % [arm, ball_travel],
	).is_greater(1.0)


func authority_car(host_summary: Dictionary) -> Dictionary:
	return car_by_stat(host_summary, "consumed")


func local_car(client_summary: Dictionary) -> Dictionary:
	return car_by_stat(client_summary, "command_frames_sent")


func car_by_stat(summary: Dictionary, key: String) -> Dictionary:
	var best := { }
	var best_value := -1
	var entities: Dictionary = summary.get("entities", { })
	for id: Variant in entities:
		var entity: Dictionary = entities[id]
		var value := stat(entity, key)
		if value > best_value:
			best_value = value
			best = entity
	return best


func stat(entity: Dictionary, key: String) -> int:
	return int((entity.get("stats", { }) as Dictionary).get(key, 0))


func histogram_of(value: Variant) -> Dictionary:
	var counts: Dictionary = { }
	if value is Array:
		var dense: Array = value
		for bucket in dense.size():
			counts[bucket] = int(dense[bucket])
	elif value is Dictionary:
		var sparse: Dictionary = value
		for bucket: Variant in sparse:
			counts[String(bucket).to_int()] = int(sparse[bucket])
	return counts


func arrival_mean(entity: Dictionary) -> float:
	var histogram := histogram_of(
		(entity.get("stats", { }) as Dictionary).get("arrivals", { }),
	)
	var frames := 0
	var arrivals := 0
	for bucket: int in histogram:
		var count: int = histogram[bucket]
		frames += count
		arrivals += bucket * count
	return float(arrivals) / frames if frames > 0 else 0.0


func throttle_phase(summary: Dictionary, target_fps: float) -> Dictionary:
	var condition: Dictionary = summary.get("condition", { })
	for phase: Variant in condition.get("throttle", []):
		if is_equal_approx(float(phase.get("target_fps", -1.0)), target_fps):
			return phase
	return { }


func print_consume_record(
		arm: String,
		authority: Dictionary,
		host_summary: Dictionary,
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
	print("[regime]   arrivals: %s" % sorted_histogram(stats.get("arrivals", { })))
	print("[regime]   depth:    %s" % sorted_histogram(stats.get("replay_depth", { })))
	print("[regime]   shape:    %s" % sorted_histogram(stats.get("consume_shape", { })))

	var local_stats: Dictionary = local_car(client_summary).get("stats", { })
	print(
		"[regime]   owner: sent=%d clamped=%d faults=%d corrections=%d" % [
			int(local_stats.get("command_frames_sent", 0)),
			int(local_stats.get("authoring_clamped", 0)),
			int(local_stats.get("quantum_faults", 0)),
			int(local_stats.get("corrections", 0)),
		],
	)

	print(
		"[regime]   play: car=%.1fm ball=%.1fm" % [
			float(
				(client_summary.get("condition", { }) as Dictionary).get(
					"gesture_travel",
					0.0,
				),
			),
			float(
				(host_summary.get("condition", { }) as Dictionary).get(
					"ball_travel",
					0.0,
				),
			),
		],
	)


func sorted_histogram(value: Variant) -> String:
	var histogram := histogram_of(value)
	var keys: Array = histogram.keys()
	keys.sort()
	var parts := PackedStringArray()
	for key: Variant in keys:
		var count := int(histogram[key])
		if count == 0:
			continue
		parts.append("%s x%d" % [key, count])
	return ", ".join(parts) if not parts.is_empty() else "(none)"
