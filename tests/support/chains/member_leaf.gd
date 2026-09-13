extends "res://tests/support/chains/member_base.gd"
## Test fixture: the derived half of the chain, overriding
## [code]shared_call[/code] with a default argument so the most-derived
## declaration answers a different arity than the base one.

signal mike_event(value: float)

var yankee_leaf_field: Vector3 = Vector3.ZERO
var bravo_leaf_field: float = 0.0


@rpc("any_peer")
func yankee_leaf_call(label: String, count: int = 3) -> void:
	pass


@rpc("any_peer")
func shared_call(first: int, second: int = 5) -> void:
	pass
