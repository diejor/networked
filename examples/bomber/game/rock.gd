extends CharacterBody2D

var _exploding := false


@rpc("call_local")
func exploded(by_who: int) -> void:
	if _exploding:
		return
	_exploding = true
	$"../../Score".increase_score(by_who)
	$"AnimationPlayer".play(&"explode")
