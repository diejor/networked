## Value-only materialization plan produced by [NetwSpawnReconciler].
class_name NetwSpawnPlan
extends RefCounted

## Ordered operations. Each row carries action, route, peer, and leave policy.
var operations: Array[Dictionary] = []


## Appends one operation to the plan.
func append(operation: Dictionary) -> void:
	operations.append(operation)


## Returns whether the plan has no materialization changes.
func is_empty() -> bool:
	return operations.is_empty()


## Returns the number of operations with [param action].
func count_action(action: StringName) -> int:
	var count := 0
	for operation: Dictionary in operations:
		if operation.get(&"action") == action:
			count += 1
	return count
