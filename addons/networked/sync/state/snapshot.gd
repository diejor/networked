## Detached state sample returned by lag-compensation queries.
##
## [NetwSnapshot] shares the [DictionaryRecord] value API but represents a copy
## of state sampled from history. Mutating it never writes back to the timeline.
##
## [codeblock]
## var past := ctx.lag_compensation.sample(entity, tick)
## if past.has_value(&"position"):
##     print(past.position)
## [/codeblock]
class_name NetwSnapshot
extends DictionaryRecord

## Creates a [NetwSnapshot] populated from [param values].
static func from_dictionary(values: Dictionary) -> NetwSnapshot:
	var snapshot := NetwSnapshot.new()
	snapshot.from_dict(values)
	return snapshot
