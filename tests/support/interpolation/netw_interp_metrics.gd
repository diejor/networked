## Signal analysis of a displayed sequence, the numbers the properties bound.
##
## Everything a player reads as smooth or broken is a property of the displayed
## value sequence against the truth. This turns one run's per-frame output into
## those signals: how far it stepped, whether it ever moved backward or stalled,
## how fast it moved, and how far it trailed the truth. Each property in the
## calculus is one of these numbers compared to a named bound.
## [codeblock]
## var m := NetwInterpMetrics.analyze(harness, oracle, warmup_sec)
## assert(m.regressions == 0)                    # P4 steadiness
## assert(m.max_step <= v_max * frame_dt * slack) # P2 continuity
## assert(m.max_lag_sec <= lag_max)               # P3 bounded lag
## [/codeblock]
class_name NetwInterpMetrics
extends RefCounted

## Frames where the displayed value moved against the dominant motion direction.
var regressions := 0

## Frames where the display held still while the truth was moving.
var stalls := 0

## Frames where the display moved at all, the steadiness denominator.
var moved := 0

## Largest single-frame displacement, the continuity signal.
var max_step := 0.0

## Mean displayed speed over the analyzed window, in units per second.
var mean_speed := 0.0

## Whether the reconstructed playhead time never ran backward.
var playhead_monotonic := true

## Largest backward jump the playhead took, in seconds. Zero when monotonic.
var max_playhead_backstep := 0.0

## Smallest measured lag of the display behind the truth, in seconds.
var min_lag_sec := 0.0

## Largest measured lag of the display behind the truth, in seconds.
var max_lag_sec := 0.0

## Whether the display ever led the truth (negative lag) beyond tolerance.
var led_truth := false

## Largest second difference of the displayed sequence, the jerk signal.
var max_jerk := 0.0

## Frames the analysis actually covered after the warmup cut.
var samples := 0


## Analyzes the run in [param harness] against [param oracle], ignoring the first
## [param warmup_sec] seconds while the buffer primes and the lag settles.
##
## Lag is only meaningful for a constant-velocity trajectory, where the displayed
## point maps back to a time on the known line. Other families leave the lag
## fields at zero and lean on the motion signals.
static func analyze(
		harness: NetwInterpHarness,
		oracle: NetwInterpOracle,
		warmup_sec: float,
) -> NetwInterpMetrics:
	var m := NetwInterpMetrics.new()
	var frames := harness.frames
	var displayed := harness.displayed
	var playhead := harness.playhead_time
	var n := frames.size()

	# The dominant motion direction anchors "forward" for regressions.
	var dir := oracle.velocity.normalized()
	if oracle.kind == NetwInterpOracle.Kind.CIRCLE \
			or oracle.kind == NetwInterpOracle.Kind.STATIONARY:
		dir = Vector2.ZERO

	var speed_eps := maxf(oracle.speed_max() * 0.02, 0.001)
	var lag_speed := oracle.velocity.length()
	var first := true
	var lag_first := true
	var speed_sum := 0.0
	var step_prev := Vector2.ZERO
	var have_step_prev := false
	var prev_disp := Vector2.ZERO
	var prev_playhead := 0.0

	for i in n:
		var t := frames[i]
		var disp: Vector2 = displayed[i]
		var ph := playhead[i]
		if t < warmup_sec:
			prev_disp = disp
			prev_playhead = ph
			first = false
			continue

		if not first:
			var step := disp - prev_disp
			var step_len := step.length()
			m.max_step = maxf(m.max_step, step_len)
			m.samples += 1
			speed_sum += step_len / maxf(harness.frame_dt(), 0.0001)

			var truth_moving := oracle.speed_max() > speed_eps
			if truth_moving:
				if step_len <= speed_eps * harness.frame_dt():
					m.stalls += 1
				else:
					m.moved += 1
			if dir != Vector2.ZERO and step.dot(dir) < -speed_eps * harness.frame_dt():
				m.regressions += 1

			if ph < prev_playhead - 1e-6:
				m.playhead_monotonic = false
				m.max_playhead_backstep = maxf(
					m.max_playhead_backstep,
					prev_playhead - ph,
				)

			if have_step_prev:
				m.max_jerk = maxf(m.max_jerk, (step - step_prev).length())
			step_prev = step
			have_step_prev = true

		if oracle.is_constant_velocity() and lag_speed > 0.0:
			# displayed ~ origin + velocity * s  =>  lag = t - s
			var s := (disp - oracle.origin).dot(oracle.velocity) \
					/ oracle.velocity.length_squared()
			var lag := t - s
			if lag_first:
				m.min_lag_sec = lag
				m.max_lag_sec = lag
				lag_first = false
			else:
				m.min_lag_sec = minf(m.min_lag_sec, lag)
				m.max_lag_sec = maxf(m.max_lag_sec, lag)
			if lag < -0.01:
				m.led_truth = true

		prev_disp = disp
		prev_playhead = ph
		first = false

	if m.samples > 0:
		m.mean_speed = speed_sum / float(m.samples)
	return m
