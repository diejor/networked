@tool
## Extrapolates a value forward by its own time derivative.
##
## One trajectory point plus its velocity names where the point will be a short
## time later. [NetwInterpolationInterface] calls this every frame to project a
## remote display past its newest received sample, and [PredictionComponent] calls
## it once per correction to restore a solver-owned body to the present. Sharing
## the helper keeps the two extrapolations identical, so a forecasting display and
## a reconciled body land on the same predicted pose.
## [codeblock]
## var ahead := NetwProject.project(pos, velocity, age_sec)
## if NetwProject.supports(typeof(value)):
##     ...                            # value carries a meaningful derivative
## [/codeblock]
##
## The supported types are [float] (including angle radians), [Vector2],
## [Vector3], and [Quaternion]. A [Quaternion] takes a [Vector3] angular velocity
## in axis-scaled radians per second. Discrete and non-numeric values (integers,
## colors, strings) have no meaningful derivative and hold instead of projecting.
class_name NetwProject
extends Object


## Returns [param value] advanced by [param velocity] over [param age] seconds.
##
## For a [Quaternion] [param value] the [param velocity] is a [Vector3] angular
## velocity and the result is the orientation rotated by that rate over
## [param age]. Every other supported type adds [code]velocity * age[/code].
## An unsupported [param value] type returns [param value] unchanged.
static func project(value: Variant, velocity: Variant, age: float) -> Variant:
	match typeof(value):
		TYPE_FLOAT:
			return (value as float) + (velocity as float) * age
		TYPE_VECTOR2:
			return (value as Vector2) + (velocity as Vector2) * age
		TYPE_VECTOR3:
			return (value as Vector3) + (velocity as Vector3) * age
		TYPE_QUATERNION:
			var omega := velocity as Vector3
			var speed := omega.length()
			if speed < 0.0001 or age == 0.0:
				return value
			var delta := Quaternion(omega / speed, speed * age)
			return (delta * (value as Quaternion)).normalized()
	return value


## Returns [code]true[/code] when [param type] carries a meaningful derivative
## the projection can advance. This is the field filter the extrapolated restore
## reads to decide which state fields project and which restore verbatim.
static func supports(type: Variant.Type) -> bool:
	return type == TYPE_FLOAT \
			or type == TYPE_VECTOR2 \
			or type == TYPE_VECTOR3 \
			or type == TYPE_QUATERNION
