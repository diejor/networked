## Degrades a truth stream into the jittery, lossy arrivals a receiver sees.
##
## The engine never sees the truth. It sees snapshots that left the sender at a
## tick cadence and arrived late, out of order, or not at all. This model turns a
## trajectory plus a link profile into that arrival schedule, seeded so the same
## seed always yields the same stream. Determinism is what makes replay a
## property: two runs of the same scenario feed byte-identical records.
## [codeblock]
## var delivery := NetwInterpDelivery.new(NetwInterpDelivery.wifi(), 1234)
## var schedule := delivery.build(oracle, 60.0, 1, 5.0)
## for d in schedule:            # each is an Arrival: arrival_sec, tick, value
##     history.record(d.tick, d.value, false)
## [/codeblock]
class_name NetwInterpDelivery
extends RefCounted

## A numeric twin of one [code]LocalLoopbackSession.LinkConditions[/code] profile.
##
## Latency and jitter are milliseconds, loss and reorder are ratios, matching the
## presets the transport ships so a calculus cell names the same conditions a
## live soak would.
class Preset:
	extends RefCounted

	var name: StringName = &"perfect"
	var latency_ms := 0.0
	var jitter_ms := 0.0
	var packet_loss := 0.0
	var reorder := 0.0


	func _init(
			p_name: StringName = &"perfect",
			p_latency := 0.0,
			p_jitter := 0.0,
			p_loss := 0.0,
			p_reorder := 0.0,
	) -> void:
		name = p_name
		latency_ms = p_latency
		jitter_ms = p_jitter
		packet_loss = p_loss
		reorder = p_reorder


## One snapshot that reaches the receiver.
class Arrival:
	extends RefCounted

	var arrival_sec := 0.0
	var tick := 0
	var value: Variant


## The clean link: instant, lossless.
static func perfect() -> Preset:
	return Preset.new(&"perfect", 0.0, 0.0, 0.0, 0.0)


## Twin of the Wi-Fi profile: 35 ms, 8 ms jitter, 1% loss.
static func wifi() -> Preset:
	return Preset.new(&"wifi", 35.0, 8.0, 0.01, 0.005)


## Twin of the mobile 4G profile: 85 ms, 25 ms jitter, 3% loss.
static func mobile_4g() -> Preset:
	return Preset.new(&"mobile_4g", 85.0, 25.0, 0.03, 0.02)


## Twin of the poor 3G profile: 220 ms, 90 ms jitter, 8% loss.
static func poor_3g() -> Preset:
	return Preset.new(&"poor_3g", 220.0, 90.0, 0.08, 0.05)


## Twin of the satellite profile: 650 ms, 120 ms jitter, 4% loss.
static func satellite() -> Preset:
	return Preset.new(&"satellite", 650.0, 120.0, 0.04, 0.03)


## Every preset, worst-first order held stable for matrix iteration.
static func all() -> Array[Preset]:
	return [perfect(), wifi(), mobile_4g(), poor_3g(), satellite()]


var preset: Preset
var _rng := RandomNumberGenerator.new()


func _init(p_preset: Preset, p_seed: int) -> void:
	preset = p_preset
	_rng.seed = p_seed


## Builds the arrival schedule for [param oracle] sampled every
## [param send_period] ticks at [param tickrate], out to [param duration_sec].
##
## Each sent snapshot rolls loss, then a jittered arrival time. Reorder pushes an
## occasional packet a whole interval later so it lands behind a younger one. The
## receiver keys history by tick, so out-of-order arrival is a timing fact, never
## a corruption. Arrivals are returned sorted by arrival time.
func build(
		oracle: NetwInterpOracle,
		tickrate: float,
		send_period: int,
		duration_sec: float,
) -> Array[Arrival]:
	var out: Array[Arrival] = []
	var ticktime := 1.0 / tickrate
	var latency_sec := preset.latency_ms * 0.001
	var jitter_sec := preset.jitter_ms * 0.001
	var period := maxi(1, send_period)
	var tick := 0
	while float(tick) * ticktime <= duration_sec:
		if _rng.randf() < preset.packet_loss:
			tick += period
			continue
		var jitter := 0.0
		if jitter_sec > 0.0:
			jitter = _rng.randf_range(-jitter_sec, jitter_sec)
		var arrival := float(tick) * ticktime + latency_sec + jitter
		if preset.reorder > 0.0 and _rng.randf() < preset.reorder:
			arrival += float(period) * ticktime
		arrival = maxf(arrival, float(tick) * ticktime)
		var a := Arrival.new()
		a.arrival_sec = arrival
		a.tick = tick
		a.value = oracle.value_at(float(tick) * ticktime)
		out.append(a)
		tick += period
	out.sort_custom(func(x: Arrival, y: Arrival) -> bool:
		return x.arrival_sec < y.arrival_sec
	)
	return out
