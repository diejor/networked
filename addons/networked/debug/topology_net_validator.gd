## Structural validator that delegates to [TopologyValidator].
##
## Checks that the synchronizer topology of a spawned player node is correct:
## expected count, cache/live consistency, [SaveComponent], [ClientComponent],
## and authority assignments.
class_name TopologyNetValidator
extends NetValidator


func _init() -> void:
	phase = NetValidator.STRUCTURAL


## Executes the validation check.
func execute(trigger: String, ctx: Dictionary) -> Array[String]:
	if trigger != "player_spawn":
		return []
	
	var player: Node = ctx.get("player")
	if not is_instance_valid(player):
		return []
	
	return TopologyValidator.validate_node(player).errors
