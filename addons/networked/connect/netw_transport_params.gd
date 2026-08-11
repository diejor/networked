## The typed inputs one [NetwTransport] family needs to open a connection.
##
## A params resource and the scheme that selects its transport are one fact, so
## [method _scheme] derives the scheme rather than letting a caller author it
## beside the params and disagree. That is why [member NetwHostConfig.scheme] is
## a read-only reflection of [member NetwHostConfig.transport] instead of a
## settable field.
## [codeblock]
## authoring       typed      NetwSessionConfig.transport, NetwHostConfig.transport
## persistence     dict       a saved NetwConnectTarget, a lobby directory row
## conversion      owned by the recognizing NetwTransport._params_from_dict
## [/codeblock]
## Authoring is typed because it is written by hand and wants completion and an
## inspector. Persistence stays a [Dictionary] because a saved target must
## survive an addon reshuffle with no embedded script, and a lobby row arrives
## for schemes this build may not have registered. [member raw] carries such a
## row unchanged so the browser can still list it.
@abstract
class_name NetwTransportParams
extends Resource

## Persisted fields this build could not type, kept so an unrecognized row
## round-trips unchanged.
##
## Populated by [method from_dict] with whatever keys the concrete subclass did
## not claim, and merged back by [method to_dict]. A row for a scheme with no
## registered [NetwTransport] keeps every field it arrived with.
@export var raw: Dictionary = { }


## Returns the [member NetwHostConfig.scheme] these params are authored for.
@abstract func _scheme() -> StringName


## Returns the persistence form, the shape a saved target and a lobby row carry.
##
## Implementations return only their own typed fields. [method to_dict] merges
## [member raw] underneath them.
@abstract func _to_dict() -> Dictionary


## Reads the typed fields of [param source] into this resource.
##
## Implementations claim their own keys and return the ones they consumed, so
## [method from_dict] can park the remainder in [member raw].
@abstract func _from_dict(source: Dictionary) -> PackedStringArray

## The scheme these params select a [NetwTransport] with.
var scheme: StringName:
	get:
		return _scheme()


## Returns the persistence form: the typed fields over [member raw].
func to_dict() -> Dictionary:
	var out := raw.duplicate(true)
	out.merge(_to_dict(), true)
	return out


## Fills this resource from [param source], parking unclaimed keys in
## [member raw].
func from_dict(source: Dictionary) -> void:
	var claimed := _from_dict(source)
	raw = { }
	for key: Variant in source:
		if not claimed.has(String(key)):
			raw[key] = source[key]
