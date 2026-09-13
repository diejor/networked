extends Node
## Test fixture: the base half of a two-script chain whose declaration order
## and text order disagree, so an id minted from the wrong one is visible.

signal zulu_event(count: int)
signal alpha_event(flag: bool, label: String)

var zulu_base_field: int = 0
var alpha_base_field: String = ""


@rpc("any_peer")
func zulu_base_call(amount: int) -> void:
	pass


@rpc("any_peer")
func shared_call(first: int, second: int) -> void:
	pass
