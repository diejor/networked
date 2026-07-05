## A single detection result, carrying everything the finding pipeline needs.
##
## A detector builds its manifest (with domain fields already set) and wraps it
## in a finding. The pipeline ([code]_emit_finding[/code] on [DebugReporter])
## then owns rate-limiting, base-field filling, span failure, transport, and the
## [code]on_violation[/code] fan-out. The detector never touches any of that.
## [br][br]
## Findings are the payload delivered to [code]Netw.dbg.on_violation[/code]
## listeners, so a test can assert a detector fired without scraping the wire.
@tool
class_name NetwFinding
extends RefCounted

## The typed manifest for this finding, with its domain fields already
## populated. The pipeline fills the common base fields.
var manifest: NetwManifest

## Rate-limit and routing key. Matches the manifest's trigger.
var trigger: String = ""

## Correlation id. Defaults to [code]"N/A"[/code] when the finding has no span.
var cid: String = "N/A"

## When set, the pipeline drives this span to failure with [member reason] and
## [member data].
var span: NetwSpan = null

## Failure reason forwarded to [member span] when present.
var reason: String = ""

## Extra data forwarded to the span failure.
var data: Dictionary = { }


func _init(
		p_manifest: NetwManifest,
		p_trigger: String,
		p_span: NetwSpan = null,
		p_reason: String = "",
		p_data: Dictionary = { },
) -> void:
	manifest = p_manifest
	trigger = p_trigger
	span = p_span
	reason = p_reason
	data = p_data
	if p_span:
		cid = str(p_span.id)
