## Lightweight causal link for the Networked span tracing system.
##
## A [CheckpointToken] captures a point in time within a [NetSpan] so that a
## subsequent, causally-related span can declare an explicit [code]follows_from[/code]
## relationship — without touching production node metadata.
## [br][br]
## [b]Usage:[/b]
## [codeblock]
## var token := write_span.checkpoint("cache_written")
## func load_data(token: CheckpointToken = null) -> void:
##     var span := Netw.dbg.span(self, "cache_read", {}, token)
## [/codeblock]
class_name CheckpointToken
extends RefCounted

## The [member NetSpan.id] of the span that produced this token.
var span_id: StringName

## Human-readable label of the originating span.
var span_label: String

## The step label at the point the token was captured, or empty if not
## step-specific.
var step_label: String

## Engine frame counter at capture time.
var frame: int

## Microsecond timestamp at capture time.
var usec: int


## Serializes this token into a [Dictionary].
func to_dict() -> Dictionary:
	return {
		"span_id": str(span_id),
		"span_label": span_label,
		"step_label": step_label,
		"frame": frame,
		"usec": usec,
	}
