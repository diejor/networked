## Base class for the Networked phased validation engine.
##
## Validators plug into [method NetworkedDebugReporter._execute_validators] and
## are invoked in phase order. If any validator in a phase returns errors, later
## phases are skipped — preventing cascading failures (e.g. heuristic checks on
## a structurally broken node).
## [br][br]
## Override [method execute] in subclasses to implement detection logic.
## Return an empty array to indicate the check passed.
## [br][br]
## [b]Note:[/b] The validator pattern is scoped to topology-style checks that
## return plain error strings. Detection logic that requires structured output
## (race data, zombie scans) lives in dedicated reporters, not validators.
class_name NetValidator
extends RefCounted

## Run before logical and heuristic checks. Structural checks verify that the
## required nodes and synchronizers are present and correctly configured.
const STRUCTURAL: int = 0

## Run only when structural checks pass. Logical checks verify runtime
## invariants (e.g. authority assignments, config consistency).
const LOGICAL: int = 100

## Run only when logical checks pass. Heavy heuristics may traverse large node
## trees or perform expensive comparisons.
const HEAVY_HEURISTIC: int = 200

## The phase this validator belongs to. Defaults to [constant STRUCTURAL].
var phase: int = STRUCTURAL


## Override to implement validation logic.
## [br][br]
## [param trigger] is the operation name (e.g. [code]"player_spawn"[/code]).
## [br][br]
## [param ctx] is a raw [Dictionary] whose keys depend on [param trigger]:
## [br]
## [codeblock]
## "player_spawn" -> { player: Node, mt: MultiplayerTree }
## [/codeblock]
## [br]
## Return an empty array to indicate the check passed.
func execute(_trigger: String, _ctx: Dictionary) -> Array[String]:
	return []
