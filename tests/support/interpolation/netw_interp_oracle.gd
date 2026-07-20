## Parametric ground truth for the display calculus.
##
## A trajectory is a closed-form function of wall time. The calculus samples it
## at tick cadence to build the snapshot stream the engine consumes, then checks
## the displayed output against the same function. Because the truth is analytic,
## every property has an exact reference: the displayed value is smooth or broken
## relative to a known curve, not relative to another noisy measurement.
## [codeblock]
## var truth := NetwInterpOracle.linear(Vector2.ZERO, Vector2(120, 0))
## var p := truth.value_at(0.5)     # position half a second in
## var v := truth.speed             # constant speed for the continuity bound
## [/codeblock]
class_name NetwInterpOracle
extends RefCounted

## The family of curve a trajectory draws.
enum Kind {
	## Constant velocity along a straight line. The exact-forecast family.
	LINEAR,
	## Constant angular speed around a circle. Bounded curvature, non-zero jerk.
	CIRCLE,
	## Straight drift with a transverse sine, a smooth non-constant speed.
	SINE,
	## A single teleport step at [member step_time]. Declares a snap.
	STEP,
	## No motion. The sleep family.
	STATIONARY,
}

var kind: Kind = Kind.LINEAR
var origin := Vector2.ZERO
var velocity := Vector2(120.0, 0.0)
var radius := 100.0
var omega := 2.0
var amplitude := Vector2(0.0, 40.0)
var step_time := 1.0
var step_delta := Vector2(200.0, 0.0)


## A straight line through [param origin] at constant [param vel] per second.
static func linear(origin: Vector2, vel: Vector2) -> NetwInterpOracle:
	var o := NetwInterpOracle.new()
	o.kind = Kind.LINEAR
	o.origin = origin
	o.velocity = vel
	return o


## A circle of [param radius] about [param center] at [param omega] rad/s.
static func circle(center: Vector2, radius: float, omega: float) -> NetwInterpOracle:
	var o := NetwInterpOracle.new()
	o.kind = Kind.CIRCLE
	o.origin = center
	o.radius = radius
	o.omega = omega
	return o


## A drift at [param vel] with a transverse [param amplitude] sine at [param omega].
static func sine(
		origin: Vector2,
		vel: Vector2,
		amplitude: Vector2,
		omega: float,
) -> NetwInterpOracle:
	var o := NetwInterpOracle.new()
	o.kind = Kind.SINE
	o.origin = origin
	o.velocity = vel
	o.amplitude = amplitude
	o.omega = omega
	return o


## A teleport from [param origin] to [param origin] + [param delta] at [param at].
static func step(origin: Vector2, delta: Vector2, at: float) -> NetwInterpOracle:
	var o := NetwInterpOracle.new()
	o.kind = Kind.STEP
	o.origin = origin
	o.step_delta = delta
	o.step_time = at
	return o


## A point that never moves from [param origin].
static func stationary(origin: Vector2) -> NetwInterpOracle:
	var o := NetwInterpOracle.new()
	o.kind = Kind.STATIONARY
	o.origin = origin
	return o


## The truth position at wall time [param t] seconds.
func value_at(t: float) -> Vector2:
	match kind:
		Kind.LINEAR:
			return origin + velocity * t
		Kind.CIRCLE:
			return origin + Vector2(cos(omega * t), sin(omega * t)) * radius
		Kind.SINE:
			return origin + velocity * t + amplitude * sin(omega * t)
		Kind.STEP:
			return origin if t < step_time else origin + step_delta
		Kind.STATIONARY:
			return origin
	return origin


## The largest instantaneous speed the truth reaches, in units per second. The
## continuity bound multiplies this by the frame time.
func speed_max() -> float:
	match kind:
		Kind.LINEAR:
			return velocity.length()
		Kind.CIRCLE:
			return radius * absf(omega)
		Kind.SINE:
			return velocity.length() + amplitude.length() * absf(omega)
		_:
			return 0.0


## The largest instantaneous acceleration the truth reaches, in units per second
## squared. The jerk bound adds this curvature term to the motion allowance.
func accel_max() -> float:
	match kind:
		Kind.CIRCLE:
			return radius * omega * omega
		Kind.SINE:
			return amplitude.length() * omega * omega
		_:
			return 0.0


## True when this trajectory is a pure constant-velocity drift, the family the
## bounded-lag property compares against a shifted copy of itself.
func is_constant_velocity() -> bool:
	return kind == Kind.LINEAR
