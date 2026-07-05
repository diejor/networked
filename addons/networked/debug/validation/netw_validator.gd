## Base class for Networked detection.
##
## A validator reacts to a [NetwTreeEvent] the [TreeProbe] dispatches and reports
## findings through a [NetwReport]. It declares the event kinds it wants through
## [method interests], so the registry only calls [method inspect] for matching
## events and a validator never branches on a kind it ignores.
## [codeblock]
## NetwTreeEvent  --(matches interests())-->  inspect(event, report)
##                                                └─> report.fail(manifest)
## [/codeblock]
## Instances only, never statics. Routing needs a live object whose intermediate
## state the Godot debugger can step into.
@tool
class_name NetwValidator
extends RefCounted

## Lower runs first when several validators match one event. Replaces the former
## structural/logical/heuristic phase engine.
var priority: int = 0


## Returns the [enum NetwTreeEvent.Kind] values this validator reacts to. The
## registry filters events by this set before calling [method inspect]. An empty
## array means the validator never runs.
func interests() -> Array:
	return []


## Inspects one [param event] and reports 0..N findings through [param report].
## Accumulate intermediate state in self-fields so each step is steppable.
func inspect(_event: NetwTreeEvent, _report: NetwReport) -> void:
	pass
