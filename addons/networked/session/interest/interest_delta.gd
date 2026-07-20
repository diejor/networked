## Value returned by [method InterestEngine.recompute].
##
## Rows and order keys are parallel arrays. [member shows] and
## [member hides] contain [code][entity_key, peer_bit][/code] pairs.
class_name InterestDelta
extends RefCounted

## Entity keys whose committed rows change.
var keys: Array = []
## Previous committed rows parallel to [member keys].
var old_rows: Array[PackedInt64Array] = []
## Desired rows parallel to [member keys].
var new_rows: Array[PackedInt64Array] = []
## [code][depth, route][/code] values parallel to [member keys].
var order_keys: Array = []
## Ordered [code][entity_key, peer_bit][/code] show transitions.
var shows: Array = []
## Ordered [code][entity_key, peer_bit][/code] hide transitions.
var hides: Array = []
## Ordered [code][layer_id, entity_key, peer_bit][/code] show transitions.
var layer_shows: Array = []
## Ordered [code][layer_id, entity_key, peer_bit][/code] hide transitions.
var layer_hides: Array = []
## Counters produced by this recompute.
var stats := InterestStats.new()

var layer_rows: Dictionary = { }
var memberships: Dictionary = { }
var removed_keys: Array = []
var commit_revision: int = 0


## Returns whether this delta has no row transitions.
func is_empty() -> bool:
	return keys.is_empty()


## Returns a parity-friendly snapshot containing only value arrays.
func to_array() -> Array:
	return [
		keys,
		old_rows,
		new_rows,
		order_keys,
		shows,
		hides,
		layer_shows,
		layer_hides,
		stats.to_array(),
	]
