## One scripted peer of a multi-process regime capture.
##
## The rendered regime that decides lag-compensation questions is several OS
## processes with independent wall clocks speaking real sockets. This node is
## the child-process half: it connects a session, waits for its entity, runs
## a scripted gesture for a fixed horizon, and exits with a
## [code]summary.json[/code] that carries every number an orchestrator
## asserts. The game contributes what only it knows through a small adapter
## surface of [Callable]s, so the lifecycle, the throttle, the watchdog, and
## the summary stay game-agnostic.
## [codeblock]
## # the game's session root, when NetwRegimePeer.armed():
## var peer := NetwRegimePeer.new()
## peer.api = api
## peer.connect_session = _connect       # async (role, port) -> Error
## peer.await_local_ready = _wait_car    # async () -> bool
## peer.gestures = { "laps": _drive_laps }   # async (seconds) -> void
## peer.summary_handles = _handles       # () -> { id: entity.prediction }
## add_child(peer)
## peer.configure_from_args()
## peer.run()
## [/codeblock]
## Configuration rides CLI user args first and environment variables second,
## because a spawned process inherits its parent's environment and two peers
## of one capture must never share a config by accident.
## [codeblock]
## --regime-role=host|client1     NETW_REGIME_ROLE
## --regime-peers=3               NETW_REGIME_PEERS
## --regime-port=31201            NETW_REGIME_PORT
## --regime-seconds=30            NETW_REGIME_SECONDS
## --regime-gesture=laps          NETW_REGIME_GESTURE
## --regime-throttle=0:5,6:20     NETW_REGIME_THROTTLE   (fps:seconds,...)
## --regime-dir=C:/caps/host      NETW_REGIME_DIR
## --regime-quiesce=1.0           NETW_REGIME_QUIESCE
## --regime-expect=k>=v,k<=v      NETW_REGIME_EXPECT
## [/codeblock]
## The summary's [code]condition[/code] block records what actually held
## (wall seconds, measured physics and poll rates, throttle phases, gesture
## travel), so an arm asserts its regime was reached before trusting any
## number from the run. A run that ends on the watchdog says so in
## [code]exit[/code] instead of passing as a short capture, and one that misses
## [member expect] exits [constant EXIT_CONDITION] rather than publishing a
## full-length capture of a regime it never entered.
class_name NetwRegimePeer
extends Node

const Async := preload("res://addons/networked/gdscript/async.gd")


const ROLE_HOST := "host"
const ROLE_CLIENT := "client"


## True when [param p_role] names the peer that hosts. Every other spelling
## joins, so a capture of more than two processes names its clients apart
## without teaching this node a second vocabulary.
static func role_is_host(p_role: String) -> bool:
	return p_role == ROLE_HOST


# Exit codes an orchestrator reads. Zero is a completed run.
const EXIT_WATCHDOG := 2
const EXIT_CONNECT := 3
const EXIT_READY := 4
const EXIT_GESTURE := 5
const EXIT_CONDITION := 6

# Seconds granted beyond the run itself for connect, spawn, and quiesce
# before the watchdog declares the child wedged. A capture that gathers its
# peers in a lobby before anything starts spends most of it there.
const WATCHDOG_MARGIN := 45.0

# Seconds between cadence samples. Interval rates come from consecutive
# samples, so this is also the resolution of the p10 the summary reports.
const CADENCE_SAMPLE_SECONDS := 1.0

## The session API this peer drives. Set by the adapter before [method run].
var api: NetwMultiplayer

## Async [code](role: String, port: int) -> Error[/code]. Hosts or joins the
## session, whichever [member role] asks for.
var connect_session: Callable

## Async [code]() -> bool[/code]. Resolves once the local entity exists and
## the clock is calibrated, or reports failure after its own deadline.
var await_local_ready: Callable

## Gesture name to async [code](seconds: float) -> void[/code]. The gesture
## named by [member gesture] drives the whole horizon.
var gestures: Dictionary[String, Callable] = { }

## [code]() -> Dictionary[/code] of display id to prediction handle, the
## entities whose [member
## NetwPredictionHandle.stats] the summary carries.
var summary_handles: Callable

## Optional [code]() -> Dictionary[/code] merged into the summary's condition
## block: gesture travel, contact fractions, whatever else proves the gesture
## did what its arm assumes rather than leaving the assumption unmeasured.
var condition_evidence: Callable = Callable()

var role := ROLE_HOST
## How many processes the whole capture runs, the host included. A host sizes
## its transport and its advertised capacity from it, so a third peer is
## admitted rather than refused at the door.
var peers := 2
var port := 31201
var seconds := 30.0
var gesture := "laps"
var throttle_spec := ""
var artifact_dir := ""
var quiesce := 1.0

## Comparisons the condition block must satisfy for the run to exit
## [constant @GlobalScope.OK], written [code]key:min:value[/code] or
## [code]key:max:value[/code] and separated by commas. Empty accepts any run.
##
## A gesture that ran without reaching its regime is the failure this exists
## for. It produces a full-length capture whose every number is valid and
## whose regime is absent, so nothing downstream can tell it from a good run
## and it is read as evidence about a regime it never entered.
## [codeblock]
## --regime-expect=wall_contact_fraction:min:0.15
## --regime-expect=wall_contact_fraction:min:0.15,car_contact_fraction:max:0.0
## [/codeblock]
## The comparator is spelled [code]min[/code] and [code]max[/code] rather than
## [code]>=[/code] and [code]<=[/code] because a launcher may reach the engine
## through a shell wrapper, where an angle bracket in an argument is a
## redirection operator and the process dies before it parses anything.
var expect := ""

var _completed := false
var _throttle: NetwRegimeThrottle
var _cadence_samples: Array[Dictionary] = []
# Cadence counters at gesture start, so the condition block measures the
# gesture window rather than diluting it with process boot and asset load.
var _cadence_baseline: Dictionary = { }
# Last-known evidence per entity, refreshed every cadence sample. The other
# peer's exit despawns entities before this peer's summary writes, so the
# summary reads these snapshots and a despawned entity keeps its final state.
var _entity_snapshots: Dictionary = { }


## True when any regime argument or environment variable names a role, which
## is the signal a game session should hand itself to a peer instead of its
## normal UI.
static func armed() -> bool:
	return not _setting("regime-role", "NETW_REGIME_ROLE", "").is_empty()


## Reads [member role], [member port], and the rest of the contract from CLI
## user args with environment fallback. Call before [method run].
func configure_from_args() -> void:
	role = _setting("regime-role", "NETW_REGIME_ROLE", role).to_lower()
	peers = int(_setting("regime-peers", "NETW_REGIME_PEERS", str(peers)))
	port = int(_setting("regime-port", "NETW_REGIME_PORT", str(port)))
	seconds = float(_setting("regime-seconds", "NETW_REGIME_SECONDS", str(seconds)))
	gesture = _setting("regime-gesture", "NETW_REGIME_GESTURE", gesture)
	throttle_spec = _setting("regime-throttle", "NETW_REGIME_THROTTLE", throttle_spec)
	artifact_dir = _setting("regime-dir", "NETW_REGIME_DIR", artifact_dir)
	quiesce = float(_setting("regime-quiesce", "NETW_REGIME_QUIESCE", str(quiesce)))
	expect = _setting("regime-expect", "NETW_REGIME_EXPECT", expect)


## Runs the whole peer lifecycle and exits the process when it is done. The
## watchdog is armed first, so no failure mode leaves a silent orphan.
func run() -> void:
	_arm_watchdog.call_deferred()
	if _throttle == null and not throttle_spec.is_empty():
		_throttle = NetwRegimeThrottle.new()
		_throttle.phases = NetwRegimeThrottle.parse(throttle_spec)
		add_child(_throttle)
	var error: int = await connect_session.call(role, port)
	if error != OK:
		push_error("regime %s: connect failed with %s" % [role, error_string(error)])
		_finish(EXIT_CONNECT, "connect_failed")
		return
	var ready: bool = await await_local_ready.call()
	if not ready:
		push_error("regime %s: local entity never became ready" % role)
		_finish(EXIT_READY, "ready_timeout")
		return
	if not gestures.has(gesture):
		push_error("regime %s: unknown gesture %s" % [role, gesture])
		_finish(EXIT_GESTURE, "unknown_gesture")
		return
	print("[regime] %s ready on port %d, driving %s for %.1fs" % [
		role,
		port,
		gesture,
		seconds,
	])
	if api and api.clock_is_configured():
		_cadence_baseline = _cadence()
	if api:
		api.attribution_set_armed(true)
	_sample_cadence_loop.call_deferred()
	await gestures[gesture].call(seconds)
	_snapshot_entities()
	if quiesce > 0.0:
		await get_tree().create_timer(quiesce).timeout
	var unmet := _unmet_expectations()
	if not unmet.is_empty():
		push_error("regime %s: regime not reached, %s" % [role, unmet])
		_finish(EXIT_CONDITION, "condition_unmet")
		return
	_finish(OK, "complete")


# The expectations the condition block failed, empty when the run may be
# trusted. Reported as text rather than a bool so a failing run says which
# number missed and by how much, in the process that measured it.
func _unmet_expectations() -> PackedStringArray:
	var unmet := PackedStringArray()
	if expect.strip_edges().is_empty():
		return unmet
	var condition := _condition_report()
	for clause in expect.split(",", false):
		var parts := clause.strip_edges().split(":", false)
		if parts.size() != 3 or not (parts[1] in ["min", "max"]):
			unmet.append("%s is not key:min:value or key:max:value" % clause)
			continue
		var key := parts[0].strip_edges()
		if not condition.has(key):
			unmet.append("%s is absent from the condition block" % key)
			continue
		var at_least := parts[1] == "min"
		var actual := float(condition[key])
		var wanted := parts[2].strip_edges().to_float()
		if (actual < wanted) if at_least else (actual > wanted):
			unmet.append("%s is %.4f, wanted %s %.4f" % [
				key,
				actual,
				parts[1],
				wanted,
			])
	return unmet


func _finish(code: int, exit_kind: String) -> void:
	if _completed:
		return
	_completed = true
	_write_summary(exit_kind)
	if api and api:
		api.predict_flush_tap()
	print("[regime] %s %s" % [role, exit_kind])
	get_tree().quit(code)


func _arm_watchdog() -> void:
	var budget := seconds + quiesce + WATCHDOG_MARGIN
	await get_tree().create_timer(budget).timeout
	if _completed:
		return
	push_error("regime %s: watchdog fired after %.1fs" % [role, budget])
	_finish(EXIT_WATCHDOG, "watchdog")


# Snapshots the clock's cadence counters every sample interval. Interval
# rates derived from consecutive snapshots are what the summary's p10 reads,
# so a throttle phase shows up as its own slow intervals rather than being
# averaged away by the healthy ones. Entity evidence rides the same cadence,
# so a despawn costs at most one sample interval of staleness.
func _sample_cadence_loop() -> void:
	while not _completed:
		await get_tree().create_timer(CADENCE_SAMPLE_SECONDS).timeout
		if api and api.clock_is_configured():
			_cadence_samples.append(_cadence())
		_snapshot_entities()


# Refreshes the last-known evidence for every entity still alive. An entity
# the session already despawned keeps its previous snapshot, which is its
# final state.
func _snapshot_entities() -> void:
	if not summary_handles.is_valid():
		return
	var handles: Dictionary = summary_handles.call()
	for id: Variant in handles:
		var handle: Variant = handles[id]
		if handle == null:
			continue
		_entity_snapshots[String(id)] = {
			"stats": handle.stats.to_dictionary(),
			"episode": handle.episode_digest(),
			"last_field_divergence": handle.last_field_divergence,
			# The divergence row says how far each field is out now; this says
			# which fields the run's recoveries actually reached. A field that
			# triggers and is never repaired is answered by writing another one,
			# and neither the stats block nor a single-tick divergence can show
			# that.
			"field_recovery": _field_recovery_report(handle),
		}


func _field_recovery_report(handle: Variant) -> Dictionary:
	var out: Dictionary = { }
	for field: StringName in handle.field_recovery:
		var row: Variant = handle.field_recovery[field]
		out[String(field)] = {
			"triggered": row.triggered,
			"repaired": row.repaired,
			"contracted": row.contracted,
			"carried": row.carried,
			"declined": row.declined,
			"infidelity": row.infidelity,
		}
	return out


func _write_summary(exit_kind: String) -> void:
	if artifact_dir.is_empty():
		return
	DirAccess.make_dir_recursive_absolute(artifact_dir)
	_snapshot_entities()
	var summary := {
		"role": role,
		"exit": exit_kind,
		"config": {
			"peers": peers,
			"port": port,
			"seconds": seconds,
			"gesture": gesture,
			"throttle": throttle_spec,
			"quiesce": quiesce,
		},
		"condition": _condition_report(),
		"clock": _clock_report(),
		"entities": _entity_snapshots,
		"attribution": api.attribution_snapshot() if api else { },
		"stats": api.stats_snapshot() if api else { },
		"instrument_cost": { "tap": api.predict_get_tap_cost() },
	}
	var file := FileAccess.open(
		artifact_dir.path_join("summary.json"),
		FileAccess.WRITE,
	)
	if file == null:
		push_error("regime %s: could not write summary.json" % role)
		return
	file.store_string(JSON.stringify(summary, "  "))
	file.close()


# What actually held during the gesture window, the block every assertion
# reads first. Rates are measured from the gesture-start baseline, because a
# whole-process mean dilutes the run with boot and asset load.
func _condition_report() -> Dictionary:
	var cadence := _cadence() if api and api.clock_is_configured() else { }
	var wall := float(cadence.get(&"wall_seconds", 0.0)) \
			- float(_cadence_baseline.get(&"wall_seconds", 0.0))
	var physics_frames := int(cadence.get(&"physics_frames", 0)) \
			- int(_cadence_baseline.get(&"physics_frames", 0))
	var polls := int(cadence.get(&"polls", 0)) \
			- int(_cadence_baseline.get(&"polls", 0))
	var physics_rates := _interval_rates(&"physics_frames")
	var poll_rates := _interval_rates(&"polls")
	var out := {
		"wall_seconds": wall,
		"physics_hz_mean": physics_frames / wall if wall > 0.0 else 0.0,
		"poll_hz_mean": polls / wall if wall > 0.0 else 0.0,
		"physics_hz_p10": _percentile(physics_rates, 0.1),
		"poll_hz_p10": _percentile(poll_rates, 0.1),
	}
	if _throttle:
		out["throttle"] = _throttle.report()
	if condition_evidence.is_valid():
		out.merge(condition_evidence.call(), true)
	return out


# The cadence counters the condition block measures its rates from, read off
# the monitor door rather than a fixed-key dictionary the session hands over.
func _cadence() -> Dictionary:
	return {
		&"wall_seconds": api.clock_get_monitor(
			NetwMultiplayer.CLOCK_MONITOR_WALL_SECONDS
		),
		&"physics_frames": int(api.clock_get_monitor(
			NetwMultiplayer.CLOCK_MONITOR_PHYSICS_FRAMES
		)),
		&"polls": int(api.clock_get_monitor(
			NetwMultiplayer.CLOCK_MONITOR_POLLS
		)),
	}


func _clock_report() -> Dictionary:
	if not api or not api.clock_is_configured():
		return { }
	return {
		"tick": api.clock_get_tick(),
		"synchronized": api.clock_is_synchronized(),
		"behind": api.clock_get_simulation_behind_count(),
		"rtt_avg": api.clock_get_monitor(
			NetwMultiplayer.CLOCK_MONITOR_RTT_AVG
		),
		"rtt_jitter": api.clock_get_monitor(
			NetwMultiplayer.CLOCK_MONITOR_RTT_JITTER
		),
	}


# Per-interval rates between consecutive cadence samples, in Hz.
func _interval_rates(counter: StringName) -> Array[float]:
	var rates: Array[float] = []
	for i in range(1, _cadence_samples.size()):
		var wall: float = _cadence_samples[i].get(&"wall_seconds", 0.0) \
				- _cadence_samples[i - 1].get(&"wall_seconds", 0.0)
		if wall <= 0.0:
			continue
		var count: int = int(_cadence_samples[i].get(counter, 0)) \
				- int(_cadence_samples[i - 1].get(counter, 0))
		rates.append(count / wall)
	return rates


func _percentile(values: Array[float], fraction: float) -> float:
	if values.is_empty():
		return 0.0
	var sorted := values.duplicate()
	sorted.sort()
	return sorted[clampi(
		int(fraction * sorted.size()),
		0,
		sorted.size() - 1,
	)]


# One setting: the CLI user arg wins, the environment variable is the
# fallback, the default stands when neither speaks.
static func _setting(arg: String, env: String, fallback: String) -> String:
	var prefix := "--%s=" % arg
	for value in OS.get_cmdline_user_args():
		if value.begins_with(prefix):
			return value.trim_prefix(prefix)
	var from_env := OS.get_environment(env)
	return from_env if not from_env.is_empty() else fallback
