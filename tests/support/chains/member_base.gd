extends Node

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
