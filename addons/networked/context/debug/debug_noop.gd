## Release-oriented [NetwDbg] implementation with inert trace/log behavior.
##
## Warning and error calls still surface through [NetwLog] so production
## failures are not silently dropped.
class_name NetwDbgNoop
extends NetwDbg

## Drops an [code]INFO[/code] message.
func info(
		_arg1: Variant,
		_arg2: Variant = null,
		_arg3: Variant = null,
		_arg4: Variant = null,
) -> void:
	pass


## Drops a [code]DEBUG[/code] message.
func debug(
		_arg1: Variant,
		_arg2: Variant = null,
		_arg3: Variant = null,
		_arg4: Variant = null,
) -> void:
	pass


## Drops a [code]TRACE[/code] message.
func trace(
		_arg1: Variant,
		_arg2: Variant = null,
		_arg3: Variant = null,
		_arg4: Variant = null,
) -> void:
	pass


## Returns a no-op [NetwSpan].
func span(
		_context: Object,
		label: String,
		_meta: Dictionary = { },
		_follows_from: CheckpointToken = null,
) -> NetwSpan:
	return NetwSpan.new(&"", label)


## Returns a no-op [NetwPeerSpan].
func peer_span(
		_context: Object,
		label: String,
		_peers: Array = [],
		_meta: Dictionary = { },
		_token: CheckpointToken = null,
) -> NetwPeerSpan:
	return NetwPeerSpan.new(&"", label)
