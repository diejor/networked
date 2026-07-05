## Per-event finding sink handed to [method NetwValidator.inspect].
##
## Mirrors the span fail/warn duality: [method fail] emits a full crash manifest
## through the finding pipeline, while [method warn] records a non-fatal
## observation. A validator accumulates state in its own fields, then reports
## 0..N findings through this sink, so it never touches rate-limiting, transport,
## or the [code]on_violation[/code] fan-out.
## [codeblock]
## func inspect(event: NetwTreeEvent, report: NetwReport) -> void:
##     var m := MyManifest.new()
##     m.trigger = "MY_TRIGGER"
##     report.fail(m)          # ships the manifest and fails the active span
## [/codeblock]
## The report captures the span active when the event was dispatched, so a
## validator never resolves the span itself.
@tool
class_name NetwReport
extends RefCounted

var _reporter: DebugReporter
var _mt: MultiplayerTree
var _probe: TreeProbe
var _span: NetwSpan


func _init(
		reporter: DebugReporter,
		mt: MultiplayerTree,
		probe: TreeProbe = null,
		span: NetwSpan = null,
) -> void:
	_reporter = reporter
	_mt = mt
	_probe = probe
	_span = span


## Returns the [TreeProbe] observing the event's tree, for building enriched
## snapshots. Null when the tree has no probe.
func probe() -> TreeProbe:
	return _probe


## Returns the operation span the event carries, or null. A validator that needs
## the span's target node for a snapshot reads it here rather than resolving the
## global active span.
func span() -> NetwSpan:
	return _span


## Reports a fatal finding. Fills [param manifest]'s base fields, drives the
## active span to failure with [param reason] and [param data], ships the crash
## manifest, and notifies [code]on_violation[/code] listeners. [param manifest]
## must already carry its trigger and domain fields.
func fail(manifest: NetwManifest, reason: String = "", data: Dictionary = { }) -> void:
	if not _reporter:
		return
	_reporter._emit_finding(
		NetwFinding.new(manifest, manifest.trigger, _span, reason, data),
		_mt,
	)


## Reports a non-fatal finding: a suspicious-but-not-fatal observation. Records a
## warning step on the active span when present.
## [br][br]
## Not yet surfaced as a distinct editor status; it currently annotates the span
## only.
func warn(message: String, data: Dictionary = { }) -> void:
	if _span:
		_span.step_warn("validator_warn", message, data)
